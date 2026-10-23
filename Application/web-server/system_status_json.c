/*
 * system_status_json.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Serialize system values; every field carries its raw value and the
 * client marks unreliable ones using the PowerValidFields bitmask.
 */
#include "system_status_json.h"
#include "power_board_scp.h"
#include "gsm_info.h"
#include "web_json.h"

static bool has_fields(uint16_t mask, uint16_t fields)
{
    return fields == (mask & fields);
}

bool system_status_json_build(const system_status_t *status, char *buffer,
                              size_t capacity, size_t *length)
{

    if ((NULL == status) || (NULL == buffer) || (NULL == length) ||
        (0U == capacity))
    {
        return false;
    }
    *length = 0U;
    buffer[0] = '\0';
    const uint16_t mask = status->power_valid_fields;

    json_writer_t writer;
    if (!web_json_init(&writer, buffer, capacity))
    {
        return false;
    }
    (void)json_write_object_begin(&writer);
    web_json_array(&writer, "DIN");
    for (size_t index = 0U; index < 4U; ++index)
    {
        (void)json_write_uint32(&writer, status->din[index]);
    }
    (void)json_write_array_end(&writer);
    web_json_array(&writer, "RLY");
    for (size_t index = 0U; index < 2U; ++index)
    {
        (void)json_write_uint32(&writer, status->rly[index]);
    }
    (void)json_write_array_end(&writer);
    web_json_array(&writer, "DIP");
    for (size_t index = 0U; index < 2U; ++index)
    {
        (void)json_write_uint32(&writer, status->dip_sw[index]);
    }
    (void)json_write_array_end(&writer);
    web_json_uint32(&writer, "ADCValid", status->adc_valid ? 1U : 0U);
    web_json_uint32(&writer, "ADCSampleAgeMs", status->adc_sample_age_ms);
    web_json_int32(&writer, "3V3", status->v3v3);
    web_json_int32(&writer, "3V8", status->v3v8);
    web_json_int32(&writer, "5V", status->v5v);
    web_json_int32(&writer, "ChargeState", status->charge_state);
    web_json_int32(&writer, "ChargeVerdict", status->charge_verdict);
    web_json_int32(&writer, "Temp", status->temp);
    web_json_int32(&writer, "TempMax", status->temp_max);
    web_json_int32(&writer, "TempMin", status->temp_min);
    web_json_int32(&writer, "PanelAkimi", 0);
    web_json_int32(&writer, "PanelVoltaji", status->panel_voltage);
    web_json_int32(&writer, "DcVoltaji", status->dc_voltage);
    web_json_int32(&writer, "GirisAkimi", status->input_current);
    web_json_int32(&writer, "GirisGucu", status->input_power_10mw);
    web_json_int32(&writer, "AkuGucu", status->battery_power_10mw);
    web_json_int32(&writer, "Kaynak", status->power_source);
    web_json_int32(&writer, "TelemetriYasi", status->telemetry_age);
    web_json_int32(&writer, "KartDurum", status->board_flags);
    web_json_int32(&writer, "KartDurum2", status->board_flags2);
    web_json_int32(&writer, "AlarmMaskesi", (int32_t)status->alarm_mask);
    web_json_int32(&writer, "BataryaVoltaji", status->battery_voltage);
    web_json_int32(&writer, "TDIE", status->tdie_temp);
    web_json_int32(&writer, "TDIEMax", status->tdie_temp_max);
    web_json_int32(&writer, "TDIEMin", status->tdie_temp_min);
    web_json_int32(&writer, "ChargePertance", status->battery_charge_x10);
    web_json_int32(&writer, "Capacity", status->battery_capacity);
    web_json_int32(&writer, "BatteryCapacityUnknown",
                   status->battery_capacity_unknown ? 1 : 0);
    web_json_int32(&writer, "GsmSig", status->gsm_signal);
    web_json_int32(&writer, "GsmRAT", status->gsm_rat);
    web_json_int32(&writer, "GsmRxlev", gsm_info_get_signal_quality_2G());
    web_json_int32(&writer, "GsmRscp", gsm_info_get_signal_quality_3G());
    web_json_int32(&writer, "GsmRsrp", gsm_info_get_signal_quality_4G());
    web_json_int32(&writer, "GsmRsrq", gsm_info_get_4G_rsrq());
    web_json_int32(&writer, "GsmCREG", gsm_info_get_creg());
    web_json_int32(&writer, "GsmCGREG", gsm_info_get_cgreg());
    web_json_int32(&writer, "GsmCEREG", gsm_info_get_cereg());
    web_json_int32(&writer, "BataryaAkimi", status->battery_current);
    web_json_int32(&writer, "BatteryTemp", status->battery_temp_x10);
    web_json_int32(&writer, "BatterySOC", status->battery_soc_x10);
    web_json_int32(&writer, "BatterySOH", status->battery_soh_x10);
    web_json_int32(&writer, "OrtamSicakligi", 0);
    web_json_int32(&writer, "HeaterState", 0);
    web_json_int32(&writer, "HeaterPower", 0);
    web_json_int32(&writer, "PowerValidFields", mask);
    web_json_int32(&writer, "BatterySOCAbsolute",
                   has_fields(mask, POWER_SOC_ABSOLUTE) ? 1 : 0);
    (void)json_write_object_end(&writer);
    return web_json_finish(&writer, buffer, length);
}

/*** end of file ***/
