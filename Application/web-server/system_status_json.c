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
#include "xprintf.h"
#include <string.h>

typedef struct
{
    const char *name;
    int32_t value;
} json_field_t;

static bool has_fields(uint16_t mask, uint16_t fields)
{
    return fields == (mask & fields);
}

static bool append(char *buffer, size_t capacity, size_t *length,
                    const char *text)
{
    const size_t count = strlen(text);

    if (count >= capacity - *length)
    {
        return false;
    }
    (void)memcpy(&buffer[*length], text, count);
    *length += count;
    buffer[*length] = '\0';
    return true;
}

static bool emit_fields(const json_field_t *fields, size_t count,
                         char *buffer, size_t capacity, size_t *length)
{
    char text[128];
    bool ok = true;

    for (size_t index = 0U; ok && (index < count);
         index++)
    {
        /* Raw value always; reliability is decided on the client from
         * PowerValidFields so unreliable data stays visible but marked. */
        (void)xsnprintf(text, sizeof(text), ",\"%s\":%ld",
                        fields[index].name, (long)fields[index].value);
        ok = append(buffer, capacity, length, text);
    }
    return ok;
}

bool system_status_json_build(const system_status_t *status, char *buffer,
                              size_t capacity, size_t *length)
{
    char text[128];

    if ((NULL == status) || (NULL == buffer) || (NULL == length) ||
        (0U == capacity))
    {
        return false;
    }
    *length = 0U;
    buffer[0] = '\0';
    const uint16_t mask = status->power_valid_fields;
    const json_field_t fields[] =
    {
        {"3V3", status->v3v3}, {"3V8", status->v3v8},
        {"5V", status->v5v},
        {"ChargeState", status->charge_state},
        {"ChargeVerdict", status->charge_verdict},
        {"Temp", status->temp},
        {"TempMax", status->temp_max},
        {"TempMin", status->temp_min},
        {"PanelAkimi", 0}, {"PanelVoltaji", status->panel_voltage},
        {"DcVoltaji", status->dc_voltage},
        {"GirisAkimi", status->input_current},
        {"GirisGucu", status->input_power_10mw},
        {"AkuGucu", status->battery_power_10mw},
        {"Kaynak", status->power_source},
        {"TelemetriYasi", status->telemetry_age},
        {"KartDurum", status->board_flags},
        {"KartDurum2", status->board_flags2},
        {"AlarmMaskesi", (int32_t)status->alarm_mask},
        {"BataryaVoltaji", status->battery_voltage},
        {"TDIE", status->tdie_temp},
        {"TDIEMax", status->tdie_temp_max},
        {"TDIEMin", status->tdie_temp_min},
        {"ChargePertance", status->battery_charge_x10},
        {"Capacity", status->battery_capacity},
        {"BatteryCapacityUnknown", status->battery_capacity_unknown ? 1 : 0},
        {"GsmSig", status->gsm_signal}, {"GsmRAT", status->gsm_rat},
        {"GsmRxlev", gsm_info_get_signal_quality_2G()},
        {"GsmRscp", gsm_info_get_signal_quality_3G()},
        {"GsmRsrp", gsm_info_get_signal_quality_4G()},
        {"GsmRsrq", gsm_info_get_4G_rsrq()},
        {"GsmCREG", gsm_info_get_creg()},
        {"GsmCGREG", gsm_info_get_cgreg()},
        {"GsmCEREG", gsm_info_get_cereg()},
        {"BataryaAkimi", status->battery_current},
        {"BatteryTemp", status->battery_temp_x10},
        {"BatterySOC", status->battery_soc_x10},
        {"BatterySOH", status->battery_soh_x10},
        {"OrtamSicakligi", 0}, {"HeaterState", 0},
        {"HeaterPower", 0}, {"PowerValidFields", mask},
        {"BatterySOCAbsolute", has_fields(mask, POWER_SOC_ABSOLUTE) ? 1 : 0}
    };

    (void)xsnprintf(text, sizeof(text),
        "{\"DIN\":[%u,%u,%u,%u],\"RLY\":[%u,%u],\"DIP\":[%u,%u]",
        status->din[0], status->din[1], status->din[2], status->din[3],
        status->rly[0], status->rly[1],
        (unsigned int)status->dip_sw[0],
        (unsigned int)status->dip_sw[1]);
    bool ok = append(buffer, capacity, length, text);

    if (ok)
    {
        (void)xsnprintf(text, sizeof(text),
            ",\"ADCValid\":%u,\"ADCSampleAgeMs\":%lu",
            (unsigned)status->adc_valid,
            (unsigned long)status->adc_sample_age_ms);
        ok = append(buffer, capacity, length, text);
    }
    if (ok)
    {
        ok = emit_fields(fields, sizeof(fields) / sizeof(fields[0]),
                          buffer, capacity, length);
    }
    if (!ok || !append(buffer, capacity, length, "}"))
    {
        buffer[0] = '\0';
        *length = 0U;
        return false;
    }
    return true;
}

/*** end of file ***/
