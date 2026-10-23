/*
 * modbus_map_json.c
 *
 *  Created on: Oct 15, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Serialize RO metadata from the same layouts used by Modbus readers.
 */
#include "modbus_map_json.h"
#include "modbus_map.h"
#include "modbus_system_stats.h"
#include "modbus_power_stats.h"
#include "modbus_bms_stats.h"
#include "modbus_gsm_stats.h"
#include "modbus_rtu_slave.h"
#include "web_json.h"
#include <stdint.h>
#include <string.h>

typedef struct
{
    const char *name;
    uint16_t address;
    uint16_t count;
    const char *type;
    const char *unit;
} register_field_t;

#define FIELD(base, layout, member, type, unit)                                \
    {                                                                          \
        #member, MODBUS_REG(base, layout, member),                             \
            (uint16_t)(sizeof(((layout *)0)->member) / sizeof(uint16_t)),      \
            type, unit                                                         \
    }

static const register_field_t phase_fields[] = {
    FIELD(0U, modbus_phase_map_t, kalite, "UINT16", ""),
    FIELD(0U, modbus_phase_map_t, anlik_akim, "FLOAT32", "A"),
    FIELD(0U, modbus_phase_map_t, anlik_zaman, "UINT32", "Unix s"),
    FIELD(0U, modbus_phase_map_t, enerji_varyok, "UINT16", "0/1"),
    FIELD(0U, modbus_phase_map_t, enerji_zaman, "UINT32", "Unix s"),
    FIELD(0U, modbus_phase_map_t, yuk_akimi_varyok, "UINT16", "0/1"),
    FIELD(0U, modbus_phase_map_t, yuk_zaman, "UINT32", "Unix s"),
    FIELD(0U, modbus_phase_map_t, rf_haberlesme_varyok, "UINT16", "0/1"),
    FIELD(0U, modbus_phase_map_t, rf_zaman, "UINT32", "Unix s"),
    FIELD(0U, modbus_phase_map_t, trip_failed, "UINT16", "0/1"),
    FIELD(0U, modbus_phase_map_t, trip_zaman, "UINT32", "Unix s"),
    FIELD(0U, modbus_phase_map_t, trip_gerilimi, "FLOAT32", "V"),
    FIELD(0U, modbus_phase_map_t, harvest_gerilimi, "FLOAT32", "V"),
    FIELD(0U, modbus_phase_map_t, sicaklik, "INT16", "degC"),
    FIELD(0U, modbus_phase_map_t, rssi, "INT16", ""),
    FIELD(0U, modbus_phase_map_t, fault_count, "UINT16", ""),
};

static const register_field_t record_fields[] = {
    FIELD(0U, modbus_fault_record_map_t, ariza_akimi, "FLOAT32", "A"),
    FIELD(0U, modbus_fault_record_map_t, ariza_suresi_ms, "UINT32", "ms"),
    FIELD(0U, modbus_fault_record_map_t, enerji_varyok, "UINT16", "0/1"),
    FIELD(0U, modbus_fault_record_map_t, yuk_akimi_varyok, "UINT16", "0/1"),
    FIELD(0U, modbus_fault_record_map_t, zaman, "UINT32", "Unix s"),
};

static const register_field_t system_fields[] = {
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_sec, "UINT16", "s"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_min, "UINT16", ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_hour, "UINT16", ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_day, "UINT16", ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_month, "UINT16", ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_year, "UINT16", ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, rtc_unix, "UINT32",
          "Unix s"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, uptime_sec, "UINT16",
          "min"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, reset_reason, "UINT16",
          ""),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, mcu_temp, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, v5v, "UINT16", "mV"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, v3v3, "UINT16", "mV"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, v3v8, "UINT16", "mV"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, uptime_raw, "UINT32",
          "ms"),
    FIELD(MODBUS_MAP_SYS_BASE, modbus_sys_stats_map_t, dinput_states, "UINT16",
          ""),
};

static const register_field_t power_fields[] = {
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, has_summary, "UINT16",
          ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, valid_fields, "UINT16",
          ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, seq, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, flags, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, flags2, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, telemetry_age_sec,
          "UINT16", "s"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, source, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, charge_phase, "UINT16",
          ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, session, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, pv_mv, "UINT16", "mV"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, dc_mv, "UINT16", "mV"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, input_ma, "INT16", "mA"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, system_mv, "UINT16",
          "mV"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, battery_mv, "UINT16",
          "mV"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, battery_ma, "INT16",
          "mA"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, input_power_10mw,
          "INT16", "10 mW"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, battery_power_10mw,
          "INT16", "10 mW"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, system_power_10mw,
          "INT16", "10 mW"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, soc_tenths, "INT16",
          "0.1 %"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, soh_percent, "UINT16",
          "%"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, battery_temperature,
          "INT16", "degC"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, board_temperature,
          "INT16", "degC"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, capacity_ah, "UINT16",
          "Ah"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, charge_rate_permille,
          "UINT16", "0.001 C"),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, last_gasp_count,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, active_alarms_hi,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, active_alarms_lo,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, soc_flags, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, alarm_seq, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, suppressed, "UINT16",
          ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, has_last_gasp, "UINT16",
          ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, last_gasp_cancelled,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, last_gasp_session,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, last_gasp_cause,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, summary_age_ms_hi,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, summary_age_ms_lo,
          "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t,
          supported_summary_version, "UINT16", ""),
    FIELD(MODBUS_MAP_PWR_BASE, modbus_pwr_stats_map_t, map_version, "UINT16",
          ""),
};

static const register_field_t bms_fields[] = {
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, valid, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, soh_valid, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, total_voltage_dv,
          "UINT16", "0.1 V"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, current_da, "INT16",
          "0.1 A"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, soc_x10, "UINT16",
          "0.1 %"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, soh_x10, "UINT16",
          "0.1 %"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, life_heartbeat, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, active_cell_count,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, temp_sensor_count,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, max_cell_mv, "UINT16",
          "mV"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, max_cell_index, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, min_cell_mv, "UINT16",
          "mV"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, min_cell_index, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, cell_diff_mv, "UINT16",
          "mV"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, max_temp_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, max_temp_index, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, min_temp_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, min_temp_index, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, temp_diff_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, work_state, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, charger_status, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, load_status, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, remaining_cap_dah,
          "UINT16", "0.1 Ah"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, cycle_count, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, balance_state, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, mos_status_flags,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, average_voltage_mv,
          "UINT16", "mV"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, power_w, "UINT16", "W"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, energy_wh, "UINT16",
          "Wh"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, mos_temp_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, ambient_temp_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, heating_temp_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, heating_current_a,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, current_limit_state,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, current_limit_da,
          "INT16", "0.1 A"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_year, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_month, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_day, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_hour, "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_minute, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, rtc_second, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, remaining_charge_min,
          "UINT16", "min"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, dido_status, "UINT16",
          ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, wake_source_flags,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, comm_interface_type,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, cell_voltage_mv,
          "UINT16", "mV"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, temperatures_c, "INT16",
          "degC"),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, balance_position,
          "UINT16", ""),
    FIELD(MODBUS_MAP_BMS_BASE, modbus_bms_stats_map_t, fault_codes, "UINT16",
          ""),
};

static const register_field_t gsm_fields[] = {
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, gsm_state, "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, gsm_signal_csq, "UINT16",
          ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, gsm_rat, "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, mb_last_error_code,
          "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, mb_last_error_time_hi,
          "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, mb_last_error_time_lo,
          "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, socket_state_web,
          "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, socket_state_iec104,
          "UINT16", ""),
    FIELD(MODBUS_MAP_GSM_BASE, modbus_gsm_stats_map_t, socket_state_dialer,
          "UINT16", ""),
};

static void emit_fields(json_writer_t *writer, const char *name,
                        const register_field_t *fields, size_t count)
{

    (void)json_write_key(writer, web_json_span(name));
    (void)json_write_array_begin(writer);
    for (size_t index = 0U; count > index; index++)
    {
        const register_field_t *field = &fields[index];
        (void)json_write_array_begin(writer);
        (void)json_write_string(writer, web_json_span(field->name));
        (void)json_write_uint32(writer,
                                (uint32_t)((unsigned int)field->address));
        (void)json_write_uint32(writer, (uint32_t)((unsigned int)field->count));
        (void)json_write_string(writer, web_json_span(field->type));
        (void)json_write_string(writer, web_json_span(field->unit));
        (void)json_write_array_end(writer);
    }
    (void)json_write_array_end(writer);
}

static void emit_layout(json_writer_t *writer)
{

    (void)json_write_object_begin(writer);
    web_json_uint32(writer, "AddressBase",
                    (uint32_t)((unsigned int)MODBUS_HOLDING_REG_BASE));
    web_json_uint32(writer, "FeederBase",
                    (uint32_t)((unsigned int)MODBUS_MAP_LINE_BASE));
    web_json_uint32(writer, "FeederStride",
                    (uint32_t)((unsigned int)MODBUS_MAP_LINE_STRIDE));
    web_json_uint32(writer, "FeederCount",
                    (uint32_t)((unsigned int)MODBUS_MAP_LINE_COUNT));
    web_json_uint32(writer, "PhaseOffset",
                    (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(faz)));
    web_json_uint32(writer, "PhaseStride",
                    (uint32_t)((unsigned int)MODBUS_MAP_PHASE_REGS));
    (void)json_write_key(writer, web_json_span("PhaseCount"));
    (void)json_write_uint32(
        writer,
        (uint32_t)((unsigned int)(sizeof(((modbus_feeder_map_t *)0)->faz) /
                                  sizeof(modbus_phase_map_t))));
    web_json_uint32(writer, "FaultRecords",
                    (uint32_t)((unsigned int)MODBUS_MAP_FAULT_RECORDS));
    web_json_uint32(writer, "RecordStride",
                    (uint32_t)((unsigned int)MODBUS_MAP_RECORD_REGS));
    web_json_uint32(
        writer, "TempOffset",
        (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(temp_fault)));
    web_json_uint32(
        writer, "PermOffset",
        (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(perm_fault)));
    web_json_uint32(
        writer, "TempCountOffset",
        (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(temp_fault_count)));
    web_json_uint32(
        writer, "PermCountOffset",
        (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(perm_fault_count)));
    web_json_uint32(
        writer, "FeederQualityOffset",
        (uint32_t)((unsigned int)MODBUS_MAP_FEEDER_OFF(fider_kalite)));
    (void)json_write_key(writer, web_json_span("Control"));
    (void)json_write_array_begin(writer);
    (void)json_write_array_begin(writer);
    (void)json_write_string(writer, web_json_span("battery_warning"));
    (void)json_write_uint32(
        writer, (uint32_t)((unsigned int)MODBUS_MAP_CTRL_AKU_UYARISI));
    (void)json_write_int32(writer, 1);
    (void)json_write_string(writer, web_json_span("UINT16"));
    (void)json_write_string(writer, web_json_span(""));
    (void)json_write_array_end(writer);
    (void)json_write_array_begin(writer);
    (void)json_write_string(writer, web_json_span("modem_reset"));
    (void)json_write_uint32(
        writer, (uint32_t)((unsigned int)MODBUS_MAP_CTRL_MODEM_RESET));
    (void)json_write_int32(writer, 1);
    (void)json_write_string(writer, web_json_span("UINT16"));
    (void)json_write_string(writer, web_json_span("1"));
    (void)json_write_array_end(writer);
    (void)json_write_array_begin(writer);
    (void)json_write_string(writer, web_json_span("relay1"));
    (void)json_write_uint32(writer,
                            (uint32_t)((unsigned int)MODBUS_MAP_CTRL_RELAY1));
    (void)json_write_int32(writer, 1);
    (void)json_write_string(writer, web_json_span("UINT16"));
    (void)json_write_string(writer, web_json_span("0/1"));
    (void)json_write_array_end(writer);
    (void)json_write_array_begin(writer);
    (void)json_write_string(writer, web_json_span("relay2"));
    (void)json_write_uint32(writer,
                            (uint32_t)((unsigned int)MODBUS_MAP_CTRL_RELAY2));
    (void)json_write_int32(writer, 1);
    (void)json_write_string(writer, web_json_span("UINT16"));
    (void)json_write_string(writer, web_json_span("0/1"));
    (void)json_write_array_end(writer);
    (void)json_write_array_end(writer);
}

void modbus_map_json_write(json_writer_t *writer)
{
    emit_layout(writer);
    emit_fields(writer, "Phase", phase_fields,
                sizeof(phase_fields) / sizeof(phase_fields[0]));
    emit_fields(writer, "Record", record_fields,
                sizeof(record_fields) / sizeof(record_fields[0]));
    emit_fields(writer, "System", system_fields,
                sizeof(system_fields) / sizeof(system_fields[0]));
    emit_fields(writer, "Power", power_fields,
                sizeof(power_fields) / sizeof(power_fields[0]));
    emit_fields(writer, "Bms", bms_fields,
                sizeof(bms_fields) / sizeof(bms_fields[0]));
    emit_fields(writer, "Gsm", gsm_fields,
                sizeof(gsm_fields) / sizeof(gsm_fields[0]));
    (void)json_write_object_end(writer);
}

bool modbus_map_json_build(char *buffer, size_t capacity, size_t *length)
{
    if (NULL != length)
    {
        *length = 0U;
    }
    if ((NULL == buffer) || (0U == capacity))
    {
        return false;
    }
    buffer[0] = '\0';
    if (NULL == length)
    {
        return false;
    }
    json_writer_t writer;
    if (!web_json_init(&writer, buffer, capacity))
    {
        return false;
    }
    modbus_map_json_write(&writer);
    return web_json_finish(&writer, buffer, length);
}

/*** end of file ***/
