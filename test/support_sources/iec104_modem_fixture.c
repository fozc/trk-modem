/**
 * @file iec104_modem_fixture.c
 * @brief Fakes only device/transport services; protocol and IOA math are real.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_modem_fixture.h"

#include "cp56time2a.h"
#include "iec104_util.h"
#include "nvram.h"
#include "rtc.h"

#include <string.h>

iec104_modem_fixture_t iec104_modem_fixture;

static bool capture(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    iec104_modem_fixture_t *fixture = &iec104_modem_fixture;
    if (!fixture->writable || (fixture->count == 256U) || (length > 255U))
    {
        return false;
    }
    size_t tail = (fixture->head + fixture->count) % 256U;
    memcpy(fixture->frames[tail], data, length);
    fixture->lengths[tail] = length;
    fixture->count++;
    return true;
}

static void request_close(void *context)
{
    (void)context;
    iec104_modem_fixture.close_count++;
}

static void request_reset(void *context)
{
    (void)context;
    iec104_modem_fixture.reset_count++;
}

bool iec104_fixture_start(uint8_t window)
{
    iec104_modem_fixture_t *fixture = &iec104_modem_fixture;
    memset(fixture, 0, sizeof(*fixture));
    fixture->writable = true;
    fixture->has_live = true;
    fixture->config = (iec104_config_t){.k_max = window, .w_max = 8U,
        .t1_max = 15U, .t2_max = 1U, .t3_max = 20U,
        .originator_address = 0x5AU, .common_address = 1U,
        .ioa_aku_uyarisi = {0x10U, 0x27U, 0U},
        .ioa_modem_reset = {0x11U, 0x27U, 0U}};
    for (size_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
    {
        iec104_line_config_t *line = &fixture->breaker.line[feeder].iec104;
        line->in_use = 1U;
        for (size_t phase = 0U; phase < PHASE_MAX; phase++)
        {
            uint32_t offset = (uint32_t)(feeder * 10U + phase);
            line->anlik_akim[phase] = iec104_make_ioa_3byte(100U + offset);
            line->enerji_varyok[phase] = iec104_make_ioa_3byte(200U + offset);
            line->yuk_akimi_varyok[phase] =
                iec104_make_ioa_3byte(300U + offset);
            line->rf_haberlesme_varyok[phase] =
                iec104_make_ioa_3byte(400U + offset);
            line->trip_failed[phase] = iec104_make_ioa_3byte(500U + offset);
        }
        line->temporary_fault = iec104_make_ioa_3byte(2000U);
        line->permanent_fault = iec104_make_ioa_3byte(5000U);
    }
    fixture->phase = (rf_phase_data_t){.has_live = true, .is_online = true,
        .current_valid = true, .live = {.current_amps = -2.5F, .flags = 3U},
        .received_time = {.milliseconds = 58987U, .minute = 57U,
            .hour = 23U, .day = 25U, .dow = 7U, .month = 10U, .year = 26U}};
    fixture->fault = (fault_log_t){.tm = fixture->phase.received_time,
        .fault_current = 125.0F, .fault_duration_ms = 123456U,
        .info = {.feeder = 0U, .phase = 0U, .power_status = 1U}};
    fixture->has_fault = true;
    fixture->power.has_summary = true;
    fixture->power.valid_fields = POWER_VALID_ALARMS;
    iec104_modem_io_t io = {.write = capture, .request_close = request_close,
                            .request_reset = request_reset};
    if (!iec104_modem_init(&fixture->modem, &io))
    {
        return false;
    }
    iec104_modem_open(&fixture->modem, 0U);
    return true;
}

size_t iec104_fixture_feed(const uint8_t *data, size_t length, uint32_t time)
{
    return iec104_modem_on_data_received(&iec104_modem_fixture.modem,
        data, length, time);
}

void iec104_fixture_poll(uint32_t time)
{
    iec104_modem_poll(&iec104_modem_fixture.modem, time);
}

void iec104_fixture_close(void)
{
    iec104_modem_close(&iec104_modem_fixture.modem);
}

size_t iec104_fixture_pop(uint8_t *data, size_t capacity)
{
    iec104_modem_fixture_t *fixture = &iec104_modem_fixture;
    if ((fixture->count == 0U) || (data == NULL) ||
        (capacity < fixture->lengths[fixture->head]))
    {
        return 0U;
    }
    size_t length = fixture->lengths[fixture->head];
    memcpy(data, fixture->frames[fixture->head], length);
    fixture->head = (fixture->head + 1U) % 256U;
    fixture->count--;
    return length;
}

void iec104_fixture_event(uint8_t kind)
{
    iec104_modem_fixture.event = (iec104_event_record_t){.kind = kind};
    if (kind == IEC104_EVENT_TRIP_FAILURE)
    {
        iec104_modem_fixture.event.payload.alarm = (iec104_alarm_record_t){
            .time = iec104_modem_fixture.phase.received_time, .active = 1U};
    }
    else
    {
        iec104_modem_fixture.event.payload.fault = iec104_modem_fixture.fault;
    }
    iec104_modem_fixture.has_event = true;
}

uint32_t iec104_fixture_marked(void)
{
    return iec104_modem_fixture.marked;
}

void iec104_fixture_notify_event(void)
{
    iec104_modem_notify_event(&iec104_modem_fixture.modem);
}

uint32_t iec104_fixture_reset_count(void)
{
    return iec104_modem_fixture.reset_count;
}

uint32_t iec104_fixture_clock_count(void)
{
    return iec104_modem_fixture.clock_count;
}

uint32_t iec104_fixture_state(void)
{
    return (uint32_t)iec104_modem_fixture.modem.server.core.state;
}

const iec104_config_t *nvram_get_iec104_config(void)
{
    return &iec104_modem_fixture.config;
}

iec104_config_t *nvram_get_iec104_config_rw(void)
{
    return &iec104_modem_fixture.config;
}

breaker_t *nvram_get_breaker_rw(void)
{
    return &iec104_modem_fixture.breaker;
}

bool nvram_is_busy(void)
{
    return false;
}

int nvram_sync(bool force)
{
    (void)force;
    return 0;
}

const power_line_t *breaker_get_power_line_by_idx(uint32_t index)
{
    return index < MAX_POWER_LINE_COUNT ?
        &iec104_modem_fixture.breaker.line[index] : NULL;
}

bool rf_get_phase_data(size_t feeder, phase_id_t phase, uint32_t now,
                        rf_phase_data_t *out)
{
    (void)now;
    if ((feeder >= MAX_POWER_LINE_COUNT) || (phase >= PHASE_MAX))
    {
        return false;
    }
    *out = iec104_modem_fixture.phase;
    out->has_live = iec104_modem_fixture.has_live;
    return true;
}

bool power_board_get_snapshot(uint32_t now, power_board_snapshot_t *out)
{
    (void)now;
    *out = iec104_modem_fixture.power;
    return true;
}

cp56time2a_t cp56time2a_now(void)
{
    return iec104_modem_fixture.phase.received_time;
}

bool rtc_sync(const rtc_t *time)
{
    iec104_modem_fixture.clock = *time;
    iec104_modem_fixture.clock_count++;
    return true;
}

uint8_t fault_log_get_temp_count(uint8_t feeder, uint8_t phase)
{
    return ((feeder == 0U) && (phase == 0U) &&
            iec104_modem_fixture.has_fault) ? 1U : 0U;
}

uint8_t fault_log_get_perm_count(uint8_t feeder, uint8_t phase)
{
    return fault_log_get_temp_count(feeder, phase);
}

bool fault_log_read_nth(uint8_t feeder, uint8_t phase, fault_log_type_t type,
                        uint8_t index, fault_log_t *out)
{
    if ((feeder != 0U) || (phase != 0U) || (index != 0U) ||
        !iec104_modem_fixture.has_fault || iec104_modem_fixture.storage_failure)
    {
        return false;
    }
    *out = iec104_modem_fixture.fault;
    out->info.type = type == FAULT_LOG_TYPE_PERMANENT ? 1U : 0U;
    return true;
}

bool iec104_event_log_read_newest_unsent(iec104_event_record_t *out,
                                        uint16_t *sequence)
{
    if (!iec104_modem_fixture.has_event)
    {
        return false;
    }
    *out = iec104_modem_fixture.event;
    *sequence = 42U;
    return true;
}

void iec104_event_log_mark_sent(uint16_t sequence)
{
    if ((sequence == 42U) && iec104_modem_fixture.has_event)
    {
        iec104_modem_fixture.marked++;
        iec104_modem_fixture.has_event = false;
    }
}

int iec104_event_log_sync(void)
{
    iec104_modem_fixture.sync_count++;
    return 0;
}
/*** end of file ***/
