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
#include "stm32u3xx_hal.h"
#include "iec104.h"
#include "iec104_replay.h"
#include <string.h>

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
        (delivery->has_report && (delivery->active == data.trip_failed) &&
         !data.trip_failure_latched))
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

/*** end of file ***/
