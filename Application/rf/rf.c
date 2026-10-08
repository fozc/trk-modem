/*
 * rf.c
 *
 *  Created on: Nov 9, 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "rf.h"
#include "rf_config.h"
#include "rf_inventory.h"
#include "stm32u3xx_hal.h"
#include <math.h>
#include <string.h>

#define RF_ACTIVE_FEEDERS 4U
#define RF_PHASE_SLOTS (RF_ACTIVE_FEEDERS * 3U)
#define RF_LIVE_TIMEOUT_MS 30000U
#define RF_ALARM_KEY_COUNT 128U

typedef struct
{
    uint32_t uptime_sec;
    uint16_t boot_counter;
    uint16_t crc;
    uint8_t event;
    uint8_t zone;
    uint8_t source;
    bool acknowledged;
    uint8_t eui64[8];
} rf_alarm_key_t;

typedef struct
{
    rf_phase_data_t data;
    bool session_live;
} rf_phase_cache_t;

/* Owned by cooperative process/shell callers; UART ISR only fills rx_ring. */
static rf_phase_cache_t phase_cache[RF_PHASE_SLOTS];
static rf_anomaly_data_t anomalies[RF_PHASE_SLOTS][2];
static rf_anomaly_data_t global_anomalies[2];
static rf_alarm_key_t alarm_keys[RF_ALARM_KEY_COUNT];
static size_t alarm_key_count;
static size_t alarm_key_next;

static bool get_source_index(uint8_t source, size_t *index)
{
    uint8_t feeder = (source >> 2U) & 0x07U;
    uint8_t phase = source & 0x03U;

    if ((0U == feeder) || (RF_ACTIVE_FEEDERS < feeder) || (0U == phase))
    {
        return false;
    }
    *index = ((size_t)feeder - 1U) * 3U + (size_t)phase - 1U;
    return true;
}

static rf_phase_cache_t *get_bound_cache(uint8_t source)
{
    size_t index;
    rf_inventory_entry_t entry;

    if (!get_source_index(source, &index) ||
        !rf_inventory_get_binding(source, &entry) ||
        rf_eui64_is_zero(entry.eui64))
    {
        return NULL;
    }
    rf_phase_cache_t *cache = &phase_cache[index];

    if (0 != memcmp(cache->data.eui64, entry.eui64, 8U))
    {
        bool found = false;

        for (size_t old = 0U; old < RF_PHASE_SLOTS; old++)
        {
            if (0 == memcmp(phase_cache[old].data.eui64, entry.eui64, 8U))
            {
                *cache = phase_cache[old];
                (void)memset(&phase_cache[old], 0, sizeof(phase_cache[old]));
                cache->session_live = false;
                found = true;
                break;
            }
        }
        if (!found)
        {
            /* New identity never inherits another device's measurements. */
            (void)memset(cache, 0, sizeof(*cache));
            (void)memcpy(cache->data.eui64, entry.eui64, 8U);
        }
    }
    return cache;
}

void rf_init(void)
{
    (void)memset(phase_cache, 0, sizeof(phase_cache));
    (void)memset(anomalies, 0, sizeof(anomalies));
    (void)memset(global_anomalies, 0, sizeof(global_anomalies));
    alarm_key_count = 0U;
    alarm_key_next = 0U;
}

bool rf_open_trip_failure_alarm(uint8_t zone, uint8_t feeder,
                                uint8_t phase)
{
    if ((1U > feeder) || (4U < feeder) || (1U > phase) || (3U < phase))
    {
        return false;
    }
    const uint8_t source =
        (uint8_t)(((uint8_t)(feeder << 2) | phase) & 0x1FU);
    rf_phase_cache_t *cache = get_bound_cache(source);

    if (NULL == cache)
    {
        return false;
    }
    rf_inventory_entry_t entry;

    if (!rf_inventory_get_binding(source, &entry) ||
        (entry.zone != zone))
    {
        return false;
    }
    /* BQ-01/BQ-16: the event opens an alarm without requiring LIVE. */
    cache->data.trip_failed = true;
    cache->data.trip_failure_latched = true;
    return true;
}

bool rf_handle_alarm_event(const rf_scp_event_t *event)
{
    rf_inventory_entry_t entry;
    if ((NULL == event) ||
        ((101U != event->event) && (105U != event->event)) ||
        (1U > event->feeder) || (4U < event->feeder) ||
        (1U > event->phase) || (3U < event->phase))
    {
        return false;
    }
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    if (!rf_inventory_get_binding(source, &entry) ||
        (entry.zone != event->zone) || rf_eui64_is_zero(entry.eui64))
    {
        return false;
    }
    for (size_t index = 0U; index < alarm_key_count; index++)
    {
        const rf_alarm_key_t *key = &alarm_keys[index];
        if ((key->event == event->event) && (key->zone == event->zone) &&
            (key->source == source) && (key->crc == event->crc) &&
            (key->boot_counter == event->boot_counter) &&
            (key->uptime_sec == event->uptime_sec) &&
            (0 == memcmp(key->eui64, entry.eui64, 8U)))
        {
            return !key->acknowledged &&
                rf_open_trip_failure_alarm(event->zone, event->feeder,
                                             event->phase);
        }
    }
    if (!rf_open_trip_failure_alarm(event->zone, event->feeder, event->phase))
    {
        return false;
    }
    alarm_keys[alarm_key_next] = (rf_alarm_key_t)
    {
        .uptime_sec = event->uptime_sec, .boot_counter = event->boot_counter,
        .crc = event->crc, .event = event->event, .zone = event->zone,
        .source = source, .acknowledged = false
    };
    (void)memcpy(alarm_keys[alarm_key_next].eui64, entry.eui64, 8U);
    alarm_key_next = (alarm_key_next + 1U) % RF_ALARM_KEY_COUNT;
    if (RF_ALARM_KEY_COUNT > alarm_key_count)
    {
        alarm_key_count++;
    }
    return true;
}

void rf_hub_restarted(void)
{
    for (size_t index = 0U; index < RF_PHASE_SLOTS; index++)
    {
        /* Keep AY uptime and alarm history; BOOT is not an AY reset. */
        phase_cache[index].session_live = false;
    }
}

bool rf_handle_live(const rf_scp_live_t *live, uint32_t now_ms,
                     const cp56time2a_t *received_time)
{
    if ((NULL == live) || (6U < live->state) || (100U < live->log_pending))
    {
        return false;
    }
    rf_phase_cache_t *cache = get_bound_cache(live->source);

    if (NULL == cache)
    {
        return false;
    }
    rf_phase_data_t *data = &cache->data;
    bool restarted = data->has_live &&
                     (live->uptime_sec < data->live.uptime_sec);

    if (restarted && data->trip_failed)
    {
        data->trip_failure_latched = true;
    }
    if (live->trip_failed)
    {
        if (restarted || (!data->trip_failed &&
            (!data->has_live || !data->live.trip_failed)))
        {
            data->trip_failure_latched = true;
        }
        data->trip_failed = true;
    }
    else if (data->has_live && data->live.trip_failed &&
             (live->uptime_sec > data->live.uptime_sec))
    {
        data->trip_failed = false;
        data->trip_failure_latched = false;
    }
    else
    {
        /* A zero flag after restart or without advancing uptime is not
         * evidence that a previously observed failure was resolved.
         */
    }
    data->uptime_stalled = data->has_live &&
                          (live->uptime_sec == data->live.uptime_sec);
    data->live = *live;
    data->received_time = (NULL != received_time) ? *received_time :
                         (cp56time2a_t){.iv_bit = 1U};
    data->last_live_ms = now_ms;
    data->has_live = true;
    data->current_valid = !data->uptime_stalled &&
                          isfinite(live->current_amps) &&
                          (0.0f <= live->current_amps);
    data->trip_voltage_valid = !data->uptime_stalled &&
                              isfinite(live->trip_voltage);
    data->harvest_voltage_valid = !data->uptime_stalled &&
                                 isfinite(live->harvest_voltage);
    cache->session_live = true;
    return true;
}

bool rf_handle_trip(const rf_scp_trip_t *trip, uint32_t now_ms)
{
    rf_inventory_entry_t entry;

    if ((NULL == trip) || (1U != trip->event) ||
        !rf_inventory_get_binding(trip->source, &entry) ||
        (entry.zone != trip->zone))
    {
        return false;
    }
    rf_phase_cache_t *cache = get_bound_cache(trip->source);

    if (NULL == cache)
    {
        return false;
    }
    cache->data.trip = *trip;
    cache->data.last_trip_ms = now_ms;
    cache->data.has_trip = true;
    /* TRIP does not refresh LIVE freshness or prove switch position. */
    return true;
}

bool rf_get_source_data(uint8_t source, uint32_t now_ms, rf_phase_data_t *out)
{
    size_t index;
    rf_inventory_entry_t entry;

    if ((NULL == out) || !get_source_index(source, &index) ||
        !rf_inventory_get_binding(source, &entry))
    {
        return false;
    }
    const rf_phase_cache_t *cache = &phase_cache[index];

    if (0 != memcmp(cache->data.eui64, entry.eui64, 8U))
    {
        return false;
    }
    *out = cache->data;
    out->source = source & 0x1FU;
    out->is_online = out->has_live && cache->session_live &&
                     ((now_ms - out->last_live_ms) < RF_LIVE_TIMEOUT_MS);
    return true;
}

bool rf_get_phase_data(size_t line_index, phase_id_t phase, uint32_t now_ms,
                        rf_phase_data_t *out)
{
    if ((NULL == out) || (MAX_POWER_LINE_COUNT <= line_index) ||
        ((size_t)PHASE_MAX <= (size_t)phase))
    {
        return false;
    }
    const rf_feeder_t *feeder = rf_store_get((feeder_id_t)line_index);

    if ((NULL == feeder) || !feeder->in_use ||
        (0U == feeder->config.fider_id) ||
        (RF_ACTIVE_FEEDERS < feeder->config.fider_id))
    {
        return false;
    }
    const uint8_t *eui;

    switch (phase)
    {
        case PHASE_L1: eui = feeder->r_eui64; break;
        case PHASE_L2: eui = feeder->s_eui64; break;
        case PHASE_L3: eui = feeder->t_eui64; break;
        default: return false;
    }
    uint8_t source = (uint8_t)(((uint32_t)feeder->config.fider_id << 2U) |
                               ((uint32_t)phase + 1U));
    rf_phase_data_t data;

    if (!rf_get_source_data(source, now_ms, &data) ||
        (0 != memcmp(data.eui64, eui, 8U)))
    {
        return false;
    }
    *out = data;
    return true;
}

bool rf_handle_anomaly(const rf_scp_anomaly_t *report)
{
    size_t index;

    if ((NULL == report) || (2U < report->state) || (1U < report->path))
    {
        return false;
    }
    if ((0xFFU == report->source) && (2U == report->state))
    {
        (void)memset(anomalies, 0, sizeof(anomalies));
        (void)memset(global_anomalies, 0, sizeof(global_anomalies));
        return true;
    }
    rf_anomaly_data_t *data;

    if (0xFFU == report->source)
    {
        data = &global_anomalies[report->path];
    }
    else if (get_source_index(report->source, &index))
    {
        data = &anomalies[index][report->path];
    }
    else
    {
        return false;
    }
    data->is_active = (1U == report->state);
    data->window = report->window;
    data->total = report->total;
    data->has_report = true;
    return true;
}

bool rf_get_anomaly(uint8_t source, uint8_t path, rf_anomaly_data_t *out)
{
    size_t index;

    if ((NULL == out) || (1U < path))
    {
        return false;
    }
    if (0xFFU == source)
    {
        *out = global_anomalies[path];
    }
    else if (get_source_index(source, &index))
    {
        *out = anomalies[index][path];
    }
    else
    {
        return false;
    }
    return true;
}

bool rf_ack_stored_alarm(const uint8_t *eui64)
{
    if ((NULL == eui64) || rf_eui64_is_zero(eui64))
    {
        return false;
    }
    bool changed = false;
    for (size_t key = 0U; key < alarm_key_count; key++)
    {
        if ((0 == memcmp(alarm_keys[key].eui64, eui64, 8U)) &&
            !alarm_keys[key].acknowledged)
        {
            alarm_keys[key].acknowledged = true;
            changed = true;
        }
    }
    const uint32_t now_ms = HAL_GetTick();
    for (size_t index = 0U; index < RF_PHASE_SLOTS; index++)
    {
        rf_phase_cache_t *cache = &phase_cache[index];
        if ((0 == memcmp(cache->data.eui64, eui64, 8U)) &&
            cache->data.trip_failed && cache->data.trip_failure_latched)
        {
            cache->data.trip_failure_latched = false;
            cache->data.trip_failed = cache->data.has_live &&
                cache->session_live &&
                ((now_ms - cache->data.last_live_ms) < RF_LIVE_TIMEOUT_MS) &&
                cache->data.live.trip_failed;
            changed = true;
        }
    }
    return changed;
}

bool rf_ack_trip_failure(uint8_t source)
{
    size_t index;
    rf_inventory_entry_t entry;

    if (!get_source_index(source, &index) ||
        !rf_inventory_get_binding(source, &entry))
    {
        return false;
    }
    rf_phase_cache_t *cache = &phase_cache[index];

    if (0 != memcmp(cache->data.eui64, entry.eui64, 8U))
    {
        return false;
    }
    return rf_ack_stored_alarm(entry.eui64);
}

/*** end of file ***/
