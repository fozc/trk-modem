/*
 * rf_alarm.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Persist alarms before receipt acknowledgement; retain unsent records.
 */
#include "rf_alarm.h"
#include "rf.h"
#include "rf_config.h"
#include "rf_inventory.h"
#include "elog.h"
#include "stm32u3xx_hal.h"
#include "iec104.h"
#include "iec104_replay.h"
#include "xprintf.h"
#include <string.h>

#define CSLOG_MODULE LOG_MOD_RF
#include "console_logger_config.h"

#define RF_ALARM_SOURCES 12U
#define ALARM_RETRY_MS 60000U

typedef struct
{
    uint8_t eui64[8];
    uint8_t source;
    bool has_report;
    bool active;
    bool pending;
    bool added;
    uint16_t seq;
    iec104_alarm_record_t record;
} alarm_delivery_t;

/* RF process ownership only; no ISR accesses. */
static alarm_delivery_t deliveries[RF_ALARM_SOURCES];
static bool retry_waiting;
static uint32_t retry_ms;

void rf_alarm_init(void)
{
    (void)memset(deliveries, 0, sizeof(deliveries));
    retry_waiting = false;
}

static bool can_deliver(uint32_t now_ms)
{
    if (retry_waiting && (ALARM_RETRY_MS > (uint32_t)(now_ms - retry_ms)))
    {
        return false;
    }
    retry_waiting = false;
    return true;
}

static bool storage_failed(void)
{
    retry_waiting = true;
    retry_ms = HAL_GetTick();
    return false;
}

static alarm_delivery_t *get_delivery(uint8_t source)
{
    const uint8_t feeder = (source >> 2U) & 7U;
    const uint8_t phase = source & 3U;
    rf_inventory_entry_t binding;
    if ((1U > feeder) || (4U < feeder) || (1U > phase) ||
        !rf_inventory_get_binding(source, &binding))
    {
        return NULL;
    }
    alarm_delivery_t *delivery =
        &deliveries[((size_t)feeder - 1U) * 3U + phase - 1U];
    if (0 != memcmp(delivery->eui64, binding.eui64, 8U))
    {
        if (delivery->pending)
        {
            return NULL;
        }
        *delivery = (alarm_delivery_t){.source = source};
        (void)memcpy(delivery->eui64, binding.eui64, 8U);
    }
    return delivery;
}

static bool set_pending(alarm_delivery_t *delivery, bool active,
                         const cp56time2a_t *time)
{
    rf_inventory_entry_t binding;
    if (!rf_inventory_get_binding(delivery->source, &binding))
    {
        return false;
    }
    if (MAX_POWER_LINE_COUNT > binding.line_index)
    {
        delivery->record = (iec104_alarm_record_t)
        {
            .time = *time, .feeder = binding.line_index,
            .phase = (uint8_t)(binding.phase - 1U),
            .active = active ? 1U : 0U
        };
        delivery->pending = true;
        delivery->added = false;
        return true;
    }
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        const rf_feeder_t *feeder = rf_store_get((feeder_id_t)line);
        if ((NULL != feeder) && feeder->in_use &&
            (feeder->config.zone_id == binding.zone) &&
            (feeder->config.fider_id == binding.feeder))
        {
            delivery->record = (iec104_alarm_record_t)
            {
                .time = *time, .feeder = (uint8_t)line,
                .phase = (uint8_t)(binding.phase - 1U),
                .active = active ? 1U : 0U
            };
            delivery->pending = true;
            delivery->added = false;
            return true;
        }
    }
    return false;
}

rf_alarm_announce_t rf_alarm_announcement(char *out, unsigned int out_len,
                                          const iec104_alarm_record_t *record,
                                          bool previous_reported,
                                          bool previous_active)
{
    if ((NULL == out) || (NULL == record) ||
        (!previous_reported && (0U == record->active)))
    {
        return RF_ALARM_ANNOUNCE_NONE;
    }
    const bool active = (0U != record->active);
    if (previous_reported && (previous_active == active))
    {
        return RF_ALARM_ANNOUNCE_NONE;
    }
    const rf_alarm_announce_t kind = active ? RF_ALARM_ANNOUNCE_ACTIVE :
                                              RF_ALARM_ANNOUNCE_CLEARED;
    (void)xsnprintf(out, out_len, "[RF] ALARM %s: feeder %u phase %u",
                    active ? "ACTIVE" : "CLEARED",
                    (unsigned)(record->feeder + 1U),
                    (unsigned)(record->phase + 1U));
    return kind;
}

static bool deliver(alarm_delivery_t *delivery)
{
    if (!delivery->added)
    {
        if (!iec104_event_log_add_alarm(&delivery->record, &delivery->seq))
        {
            return storage_failed();
        }
        delivery->added = true;
        const iec104_event_record_t record =
        {
            .kind = IEC104_EVENT_TRIP_FAILURE,
            .payload.alarm = delivery->record
        };
        if (iec104_is_link_active())
        {
            (void)iec104_replay_send_record(&record, delivery->seq);
        }
    }
    if (0 != iec104_event_log_sync())
    {
        return storage_failed();
    }
#if defined(BSP_CONSOLE_LOG)
    /* Rising-edge console announcement. Guarded so no formatting
     * runs when logging is compiled out or the level is below
     * NORMAL. */
    if (console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_NORMAL))
    {
        char announcement[48];
        const rf_alarm_announce_t kind =
            rf_alarm_announcement(announcement, sizeof(announcement),
                                  &delivery->record,
                                  delivery->has_report,
                                  delivery->active);
        if (RF_ALARM_ANNOUNCE_ACTIVE == kind)
        {
            CSLOG_ERR_NODT("%s\r\n", announcement);
        }
        else if (RF_ALARM_ANNOUNCE_CLEARED == kind)
        {
            CSLOG_WARN_NODT("%s\r\n", announcement);
        }
        else
        {
            /* Unchanged state: stay silent. */
        }
    }
#endif
    if (!delivery->has_report || (delivery->active != (delivery->record.active != 0U)))
    {
        elog_log_rf_trip_failure((uint8_t)(delivery->source >> 2U),
                                (uint8_t)(delivery->source & 3U), delivery->record.active != 0U);
    }
    delivery->has_report = true;
    delivery->active = (0U != delivery->record.active);
    delivery->pending = false;
    delivery->added = false;
    if (delivery->active)
    {
        (void)rf_ack_stored_alarm(delivery->eui64);
    }
    return true;
}

void rf_alarm_live(uint8_t source, const cp56time2a_t *time)
{
    rf_phase_data_t data;
    alarm_delivery_t *delivery = get_delivery(source);
    if ((NULL == time) || (NULL == delivery) || delivery->pending ||
        !rf_get_source_data(source, HAL_GetTick(), &data) ||
        (!delivery->has_report && !data.trip_failed) ||
        (delivery->has_report && (delivery->active == data.trip_failed)))
    {
        return;
    }
    (void)set_pending(delivery, data.trip_failed, time);
}

bool rf_alarm_record(const rf_scp_event_t *event)
{
    if (NULL == event)
    {
        return false;
    }
    if ((101U != event->event) && (105U != event->event))
    {
        return true;
    }
    if (!can_deliver(HAL_GetTick()))
    {
        return false;
    }
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    alarm_delivery_t *delivery = get_delivery(source);
    if (NULL == delivery)
    {
        return true; /* Unknown assignment stays in the full raw log. */
    }
    if (delivery->pending && !deliver(delivery))
    {
        return false;
    }
    if (!rf_handle_alarm_event(event))
    {
        return true;
    }
    cp56time2a_t time;
    (void)memcpy(&time, event->timestamp, sizeof(time));
    if (0U == event->clock_quality)
    {
        time = cp56time2a_now();
    }
    time.iv_bit = (1U != event->clock_quality) || !cp56time2a_is_valid(&time);
    return set_pending(delivery, true, &time) && deliver(delivery);
}

void rf_alarm_process(uint32_t now_ms)
{
    if (!can_deliver(now_ms))
    {
        return;
    }
    for (size_t index = 0U; index < RF_ALARM_SOURCES; index++)
    {
        alarm_delivery_t *delivery = &deliveries[index];
        if (delivery->pending)
        {
            if (!deliver(delivery))
            {
                return;
            }
            continue;
        }
        const uint8_t source =
            (uint8_t)(((index / 3U + 1U) << 2U) | (index % 3U + 1U));
        rf_phase_data_t data;
        if (rf_get_source_data(source, now_ms, &data))
        {
            const cp56time2a_t time = data.has_live ? data.received_time :
                                                     cp56time2a_now();
            rf_alarm_live(source, &time);
        }
    }
}

bool rf_alarm_is_idle(void)
{
    for (size_t index = 0U; index < RF_ALARM_SOURCES; index++)
    {
        if (deliveries[index].pending)
        {
            return false;
        }
    }
    return true;
}

static void log_operator_ack(const rf_phase_data_t *data, const iec104_alarm_ack_record_t *ack)
{
    uint8_t info[16] = {data->source >> 2U, data->source & 3U, ack->source, ack->user, ack->event,
        (uint8_t)((data->is_online ? 0U : 1U) | (data->trip_failed ? 2U : 0U))};
    info[6] = (uint8_t)ack->boot;
    info[7] = (uint8_t)(ack->boot >> 8U);
    for (size_t index = 0U; index < 4U; index++)
    {
        info[8U + index] = (uint8_t)(ack->uptime >> (index * 8U));
    }
    info[12] = (uint8_t)ack->event_crc;
    info[13] = (uint8_t)(ack->event_crc >> 8U);
    const uint16_t hash = rf_scp_eui_hash(data->eui64);
    info[14] = (uint8_t)hash;
    info[15] = (uint8_t)(hash >> 8U);
    elog_add(ELOG_RF_ALARM_ACK, ELOG_LEVEL_INFO, info, sizeof(info));
}

rf_alarm_ack_result_t rf_alarm_acknowledge(const rf_alarm_ack_request_t *request)
{
    rf_phase_data_t data;
    const uint32_t now_ms = HAL_GetTick();
    if ((request == NULL) || (request->line >= MAX_POWER_LINE_COUNT) || (request->phase >= PHASE_MAX))
    {
        return RF_ALARM_ACK_INVALID;
    }
    if (!rf_get_phase_data(request->line, (phase_id_t)request->phase, now_ms, &data) ||
        !data.trip_failed || !data.trip_failure_latched || (data.alarm_revision != request->revision) ||
        (memcmp(data.eui64, request->eui64, sizeof(data.eui64)) != 0))
    {
        return RF_ALARM_ACK_STALE;
    }
    if (!data.is_online && !request->confirm_offline)
    {
        return RF_ALARM_ACK_OFFLINE;
    }
    if (!rf_alarm_is_idle())
    {
        return RF_ALARM_ACK_BUSY;
    }
    if (!iec104_alarm_ack_range_available())
    {
        return RF_ALARM_ACK_IOA_CONFLICT;
    }
    rf_alarm_event_t event = {0};
    if (!rf_get_alarm_event(data.source, &event) && data.has_live)
    {
        event.boot = data.live.boot_counter;
        event.uptime = data.live.uptime_sec;
    }
    const iec104_alarm_ack_record_t ack = {.time = cp56time2a_now(), .feeder = request->line,
        .phase = request->phase, .source = IEC104_ALARM_ACK_SOURCE_WEB, .user = IEC104_ALARM_ACK_USER_ADMIN,
        .event = event.event, .boot = event.boot, .uptime = event.uptime, .event_crc = event.crc};
    uint16_t seq;
    if (!iec104_event_log_add_alarm_ack(&ack, &seq))
    {
        return RF_ALARM_ACK_STORAGE;
    }
    /* No cooperative yield between validation, persistence and latch update. */
    if (!rf_ack_trip_failure(data.source))
    {
        return RF_ALARM_ACK_STALE;
    }
    (void)rf_get_phase_data(request->line, (phase_id_t)request->phase, now_ms, &data);
    log_operator_ack(&data, &ack);
    const iec104_event_record_t record = {.kind = IEC104_EVENT_TRIP_FAILURE_ACK, .payload.alarm_ack = ack};
    if (iec104_is_link_active())
    {
        (void)iec104_replay_send_record(&record, seq);
    }
    rf_alarm_live(data.source, &ack.time);
    return RF_ALARM_ACK_OK;
}

/*** end of file ***/
