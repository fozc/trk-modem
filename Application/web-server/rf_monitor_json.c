/*
 * rf_monitor_json.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Bounded JSON serialization of R1 RF measurements and validity metadata.
 */

#include "rf_monitor_json.h"
#include "rf.h"
#include "rf_events.h"
#include "rf_config.h"
#include "rf_faults.h"
#include "rf_inventory.h"
#include "web_json.h"
#include <math.h>
#include <string.h>

static void emit_u32(json_writer_t *writer, const char *name, uint32_t value)
{

    web_json_uint32(writer, name, (uint32_t)((unsigned long)value));
}

static void emit_i32(json_writer_t *writer, const char *name, int32_t value)
{

    web_json_int32(writer, name, (int32_t)((long)value));
}

static void emit_bool(json_writer_t *writer, const char *name, bool value)
{

    web_json_bool(writer, name, value);
}

static void emit_float(json_writer_t *writer, const char *name, float value,
                       bool valid)
{
    web_json_key(writer, name);
    if (valid && isfinite(value))
    {
        web_json_float(writer, value, 8U);
    }
    else
    {
        (void)json_write_null(writer);
    }
}

static void emit_live(json_writer_t *writer, const rf_phase_data_t *data,
                      bool detailed)
{
    if (!data->has_live)
    {
        (void)json_write_key(writer, web_json_span("Live"));
        (void)json_write_null(writer);
        return;
    }
    (void)json_write_key(writer, web_json_span("Live"));
    (void)json_write_object_begin(writer);
    web_json_uint32(writer, "Source", (uint32_t)((unsigned)data->live.source));
    emit_u32(writer, "Seq", data->live.seq);
    emit_u32(writer, "Uptime", data->live.uptime_sec);
    if (detailed)
    {
        emit_u32(writer, "BootCounter", data->live.boot_counter);
    }
    emit_u32(writer, "State", data->live.state);
    emit_u32(writer, "FaultCount", data->live.fault_count);
    emit_float(writer, "Irms", data->live.current_amps, data->current_valid);
    emit_float(writer, "VTrip", data->live.trip_voltage,
               data->trip_voltage_valid);
    emit_float(writer, "VRec", data->live.harvest_voltage,
               data->harvest_voltage_valid);
    emit_i32(writer, "Temp", data->live.temperature);
    emit_i32(writer, "RSSI", data->live.rssi);
    emit_u32(writer, "Flags", data->live.flags);
    emit_bool(writer, "FsmError", data->live.fsm_error);
    emit_bool(writer, "TripFailed", data->live.trip_failed);
    emit_u32(writer, "LogPending", data->live.log_pending);
    emit_u32(writer, "LogWrapLow", data->live.log_wrap_low);
    (void)json_write_object_end(writer);
}

static void emit_trip(json_writer_t *writer, const rf_phase_data_t *data)
{
    if (!data->has_trip)
    {
        (void)json_write_key(writer, web_json_span("Trip"));
        (void)json_write_null(writer);
        return;
    }
    (void)json_write_key(writer, web_json_span("Trip"));
    (void)json_write_object_begin(writer);
    web_json_int32(writer, "Event", 1);
    emit_u32(writer, "FaultCount", data->trip.fault_count);
    emit_float(writer, "InstantAmps", data->trip.current_amps, true);
    emit_u32(writer, "RxMs", data->last_trip_ms);
    (void)json_write_object_end(writer);
}

static void emit_anomaly(json_writer_t *writer, uint8_t source, uint8_t path)
{
    rf_anomaly_data_t data = {0};

    (void)rf_get_anomaly(source, path, &data);
    web_json_object(writer, path == 0U ? "AnomalyLive" : "AnomalyTrip");
    web_json_key(writer, "Active");
    (void)json_write_bool(writer, data.is_active);
    emit_u32(writer, "Window", data.window);
    emit_u32(writer, "Total", data.total);
    (void)json_write_object_end(writer);
}

static void emit_phase(json_writer_t *writer, size_t line, size_t phase,
                       uint32_t now_ms, bool detailed)
{

    rf_phase_data_t data;

    (void)json_write_object_begin(writer);
    web_json_uint32(writer, "Phase", (uint32_t)((unsigned)(phase + 1U)));
    (void)json_write_key(writer, web_json_span("HasData"));
    if (!rf_get_phase_data(line, (phase_id_t)phase, now_ms, &data))
    {
        (void)json_write_bool(writer, false);
        (void)json_write_object_end(writer);
        return;
    }
    (void)json_write_bool(writer, true);
    (void)json_write_key(writer, web_json_span("Eui64"));
    {
        char hex[RF_EUI64_HEX_LEN];
        rf_eui64_to_hex(data.eui64, hex);
        (void)json_write_string(writer, web_json_span(hex));
    }
    emit_bool(writer, "Online", data.is_online);
    emit_bool(writer, "TripFailedAlarm", data.trip_failed);
    emit_bool(writer, "TripFailureLatched", data.trip_failure_latched);
    emit_bool(writer, "UptimeStalled", data.uptime_stalled);
    if (detailed && data.trip_failed)
    {
        rf_alarm_event_t event;
        emit_u32(writer, "AlarmRevision", data.alarm_revision);
        (void)json_write_key(writer, web_json_span("AlarmEvent"));
        if (rf_get_alarm_event(data.source, &event))
        {
            (void)json_write_object_begin(writer);
            emit_u32(writer, "Code", event.event);
            emit_u32(writer, "Boot", event.boot);
            emit_u32(writer, "Uptime", event.uptime);
            emit_u32(writer, "CRC", event.crc);
            (void)json_write_object_end(writer);
        }
        else
        {
            (void)json_write_null(writer);
        }
    }
    if (data.has_live)
    {
        emit_u32(writer, "LiveAgeMs", now_ms - data.last_live_ms);
    }
    else
    {
        (void)json_write_key(writer, web_json_span("LiveAgeMs"));
        (void)json_write_null(writer);
    }
    emit_live(writer, &data, detailed);
    emit_trip(writer, &data);
    uint8_t source = data.source;
    emit_anomaly(writer, source, 0U);
    emit_anomaly(writer, source, 1U);
    (void)json_write_object_end(writer);
}

static void emit_fault_counts(json_writer_t *writer, size_t line)
{
    const rf_feeder_t *feeder = rf_store_get((feeder_id_t)line);
    rf_fault_stats_t count;
    if ((NULL == feeder) || !feeder->in_use ||
        (0U == feeder->config.fider_id) ||
        !rf_faults_get_stats(feeder->config.zone_id, feeder->config.fider_id,
                             &count))
    {
        (void)json_write_key(writer, web_json_span("FaultCountsSinceStartup"));
        (void)json_write_null(writer);
        return;
    }
    (void)json_write_key(writer, web_json_span("FaultCountsSinceStartup"));
    (void)json_write_object_begin(writer);
    web_json_uint32(writer, "Permanent", (uint32_t)((unsigned)count.permanent));
    emit_u32(writer, "Temporary", count.temporary);
    emit_u32(writer, "Uncertain", count.uncertain);
    emit_u32(writer, "Pending", count.pending);
    web_json_bool(writer, "IsFinal", count.is_final);
    (void)json_write_object_end(writer);
}

static void emit_service_status(json_writer_t *writer)
{
    rf_service_status_t status;
    rf_get_service_status(&status);
    (void)json_write_key(writer, web_json_span("Maintenance"));
    (void)json_write_object_begin(writer);
    web_json_uint32(writer, "SourceMismatches",
                    (uint32_t)((unsigned)status.source_mismatches));
    emit_u32(writer, "StoreResetReason", status.store_reset_reason);
    emit_u32(writer, "CounterStatus", status.counter_status);
    emit_bool(writer, "EventStoreDegraded", rf_events_storage_degraded());
    emit_u32(writer, "EpochFeeder", status.epoch_feeder);
    emit_u32(writer, "EpochPhases", status.epoch_phases);
    emit_u32(writer, "EpochDurationMs", status.epoch_duration_ms);
    (void)json_write_key(writer, web_json_span("Epoch"));
    (void)json_write_array_begin(writer);
    for (uint8_t feeder = 1U; feeder <= 4U; feeder++)
    {
        rf_epoch_status_t epoch;
        if (!rf_inventory_get_epoch_status(feeder, &epoch) || (epoch.expected_phases == 0U))
        {
            continue;
        }
        (void)json_write_object_begin(writer);
        emit_u32(writer, "Feeder", feeder);
        emit_bool(writer, "Active", epoch.active);
        emit_bool(writer, "HasReport", epoch.has_report);
        emit_bool(writer, "RetryUsed", epoch.retry_used);
        emit_bool(writer, "Uncertain", epoch.uncertain);
        emit_u32(writer, "Expected", epoch.expected_phases);
        emit_u32(writer, "ReportPhases", epoch.report_phases);
        emit_u32(writer, "Received120", epoch.received_phases);
        emit_u32(writer, "Checked", epoch.checked_phases);
        emit_u32(writer, "Missing120", epoch.missing_phases);
        (void)json_write_object_end(writer);
    }
    (void)json_write_array_end(writer);
    (void)json_write_object_end(writer);
}

bool rf_json_monitor_build(char *buffer, size_t capacity, int32_t line_filter,
                           uint32_t now_ms, size_t *length)
{
    if ((NULL == buffer) || (NULL == length) || (0U == capacity) ||
        (-1 > line_filter) || (MAX_POWER_LINE_COUNT <= line_filter))
    {
        return false;
    }
    json_writer_t writer;
    if (!web_json_init(&writer, buffer, capacity))
    {
        return false;
    }
    size_t start = (0 > line_filter) ? 0U : (size_t)line_filter;
    size_t end = (0 > line_filter) ? (size_t)MAX_POWER_LINE_COUNT : start + 1U;

    buffer[0] = '\0';
    *length = 0U;
    (void)json_write_object_begin(&writer);
    (void)json_write_key(&writer, web_json_span("lines"));
    (void)json_write_array_begin(&writer);
    for (size_t line = start; line < end; line++)
    {
        (void)json_write_object_begin(&writer);
        web_json_uint32(&writer, "LineId", (uint32_t)((unsigned)(line + 1U)));
        (void)json_write_key(&writer, web_json_span("Phases"));
        (void)json_write_array_begin(&writer);
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            emit_phase(&writer, line, phase, now_ms, (0 <= line_filter));
        }
        (void)json_write_array_end(&writer);
        /* Per-feeder reads carry counters. Keep the bulk 21-phase
         * response within the existing HTTP buffer. */
        if (0 <= line_filter)
        {
            emit_fault_counts(&writer, line);
            emit_service_status(&writer);
        }
        (void)json_write_object_end(&writer);
    }
    (void)json_write_array_end(&writer);
    (void)json_write_object_end(&writer);
    return web_json_finish(&writer, buffer, length);
}

/*** end of file ***/
