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
#include "xprintf.h"
#include <math.h>
#include <string.h>

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    bool failed;
} json_writer_t;

static void append(json_writer_t *writer, const char *text)
{
    size_t length = strlen(text);

    if (writer->failed)
    {
        return;
    }
    if (length >= writer->capacity - writer->length)
    {
        writer->failed = true;
        return;
    }
    (void)memcpy(&writer->buffer[writer->length], text, length);
    writer->length += length;
    writer->buffer[writer->length] = '\0';
}

static void emit_u32(json_writer_t *writer, const char *name, uint32_t value)
{
    char text[96];

    (void)xsnprintf(text, sizeof(text), ",\"%s\":%lu", name,
                    (unsigned long)value);
    append(writer, text);
}

static void emit_i32(json_writer_t *writer, const char *name, int32_t value)
{
    char text[96];

    (void)xsnprintf(text, sizeof(text), ",\"%s\":%ld", name, (long)value);
    append(writer, text);
}

static void emit_bool(json_writer_t *writer, const char *name, bool value)
{
    char text[96];

    (void)xsnprintf(text, sizeof(text), ",\"%s\":%s", name,
                    value ? "true" : "false");
    append(writer, text);
}

static void emit_float(json_writer_t *writer, const char *name, float value,
                        bool valid)
{
    char text[96];

    if (valid && isfinite(value))
    {
        (void)xsnprintf(text, sizeof(text), ",\"%s\":%.8e", name,
                        (double)value);
    }
    else
    {
        (void)xsnprintf(text, sizeof(text), ",\"%s\":null", name);
    }
    append(writer, text);
}

static void emit_live(json_writer_t *writer, const rf_phase_data_t *data)
{
    if (!data->has_live)
    {
        append(writer, ",\"Live\":null");
        return;
    }
    append(writer, ",\"Live\":{\"Source\":");
    char text[16];
    (void)xsnprintf(text, sizeof(text), "%u", (unsigned)data->live.source);
    append(writer, text);
    emit_u32(writer, "Seq", data->live.seq);
    emit_u32(writer, "Uptime", data->live.uptime_sec);
    emit_u32(writer, "State", data->live.state);
    emit_u32(writer, "FaultCount", data->live.fault_count);
    emit_float(writer, "Irms", data->live.current_amps,
                data->current_valid);
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
    append(writer, "}");
}

static void emit_trip(json_writer_t *writer, const rf_phase_data_t *data)
{
    if (!data->has_trip)
    {
        append(writer, ",\"Trip\":null");
        return;
    }
    append(writer, ",\"Trip\":{\"Event\":1");
    emit_u32(writer, "FaultCount", data->trip.fault_count);
    emit_float(writer, "InstantAmps", data->trip.current_amps, true);
    emit_u32(writer, "RxMs", data->last_trip_ms);
    append(writer, "}");
}

static void emit_anomaly(json_writer_t *writer, uint8_t source, uint8_t path)
{
    rf_anomaly_data_t data = {0};

    (void)rf_get_anomaly(source, path, &data);
    append(writer, (0U == path) ? ",\"AnomalyLive\":{\"Active\":"
                                : ",\"AnomalyTrip\":{\"Active\":");
    append(writer, data.is_active ? "true" : "false");
    emit_u32(writer, "Window", data.window);
    emit_u32(writer, "Total", data.total);
    append(writer, "}");
}

static void emit_phase(json_writer_t *writer, size_t line, size_t phase,
                        uint32_t now_ms)
{
    char text[64];
    rf_phase_data_t data;

    (void)xsnprintf(text, sizeof(text), "{\"Phase\":%u,\"HasData\":",
                    (unsigned)(phase + 1U));
    append(writer, text);
    if (!rf_get_phase_data(line, (phase_id_t)phase, now_ms, &data))
    {
        append(writer, "false}");
        return;
    }
    append(writer, "true");
    (void)xsnprintf(text, sizeof(text),
                    ",\"Eui64\":\"%02X%02X%02X%02X%02X%02X%02X%02X\"",
                    data.eui64[0], data.eui64[1], data.eui64[2], data.eui64[3],
                    data.eui64[4], data.eui64[5], data.eui64[6], data.eui64[7]);
    append(writer, text);
    emit_bool(writer, "Online", data.is_online);
    emit_bool(writer, "TripFailedAlarm", data.trip_failed);
    emit_bool(writer, "TripFailureLatched", data.trip_failure_latched);
    emit_bool(writer, "UptimeStalled", data.uptime_stalled);
    if (data.has_live)
    {
        emit_u32(writer, "LiveAgeMs", now_ms - data.last_live_ms);
    }
    else
    {
        append(writer, ",\"LiveAgeMs\":null");
    }
    emit_live(writer, &data);
    emit_trip(writer, &data);
    uint8_t source = data.source;
    emit_anomaly(writer, source, 0U);
    emit_anomaly(writer, source, 1U);
    append(writer, "}");
}

bool rf_json_monitor_build(char *buffer, size_t capacity, int32_t line_filter,
                            uint32_t now_ms, size_t *length)
{
    if ((NULL == buffer) || (NULL == length) || (0U == capacity) ||
        (-1 > line_filter) || (MAX_POWER_LINE_COUNT <= line_filter))
    {
        return false;
    }
    json_writer_t writer = {buffer, capacity, 0U, false};
    size_t start = (0 > line_filter) ? 0U : (size_t)line_filter;
    size_t end = (0 > line_filter) ? (size_t)MAX_POWER_LINE_COUNT : start + 1U;

    buffer[0] = '\0';
    *length = 0U;
    append(&writer, "{\"lines\":[");
    for (size_t line = start; line < end; line++)
    {
        char text[48];

        if (start < line)
        {
            append(&writer, ",");
        }
        (void)xsnprintf(text, sizeof(text), "{\"LineId\":%u,\"Phases\":[",
                        (unsigned)(line + 1U));
        append(&writer, text);
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (0U < phase)
            {
                append(&writer, ",");
            }
            emit_phase(&writer, line, phase, now_ms);
        }
        append(&writer, "]}");
    }
    append(&writer, "]}");
    if (writer.failed)
    {
        buffer[0] = '\0';
        return false;
    }
    *length = writer.length;
    return true;
}

/*** end of file ***/
