/*
 * system_status_json.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Serialize valid system values; missing power measurements are null.
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
    bool valid;
} json_field_t;

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

static bool has_fields(uint16_t mask, uint16_t fields)
{
    return fields == (mask & fields);
}

static bool emit_fields(const json_field_t *fields, size_t count,
                         char *buffer, size_t capacity, size_t *length)
{
    char text[128];
    bool ok = true;

    for (size_t index = 0U; ok && (index < count);
         index++)
    {
        if (fields[index].valid)
        {
            (void)xsnprintf(text, sizeof(text), ",\"%s\":%ld",
                            fields[index].name, (long)fields[index].value);
        }
        else
        {
            (void)xsnprintf(text, sizeof(text), ",\"%s\":null",
                            fields[index].name);
        }
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
    const bool adc = has_fields(mask, POWER_VALID_ADC);
    const bool soc = has_fields(mask, POWER_VALID_SOC | POWER_VALID_DISCHARGE);
    const json_field_t fields[] =
    {
        {"3V3", status->v3v3, true}, {"3V8", status->v3v8, true},
        {"5V", status->v5v, true},
        {"ChargeState", status->charge_state,
         has_fields(mask, POWER_VALID_CHARGE_PHASE)},
        {"ChargeVerdict", status->charge_verdict,
         has_fields(mask, POWER_VALID_CHARGE_VERDICT)},
        {"Temp", status->temp, has_fields(mask, POWER_VALID_BOARD_TEMP)},
        {"TempMax", status->temp_max, status->board_temp_history_valid},
        {"TempMin", status->temp_min, status->board_temp_history_valid},
        {"PanelAkimi", 0, false}, {"PanelVoltaji", status->panel_voltage, adc},
        {"DcVoltaji", status->dc_voltage, adc},
        {"GirisAkimi", status->input_current,
         has_fields(mask, POWER_VALID_CHARGER)},
        {"GirisGucu", status->input_power_10mw,
         has_fields(mask, POWER_VALID_POWER)},
        {"AkuGucu", status->battery_power_10mw,
         has_fields(mask, POWER_VALID_POWER)},
        {"Kaynak", status->power_source,
         has_fields(mask, POWER_VALID_SOURCE)},
        {"TelemetriYasi", status->telemetry_age,
         has_fields(mask, POWER_VALID_SUMMARY)},
        {"AlarmMaskesi", (int32_t)status->alarm_mask,
         has_fields(mask, POWER_VALID_ALARMS)},
        {"BataryaVoltaji", status->battery_voltage, adc},
        {"TDIE", status->tdie_temp, true},
        {"TDIEMax", status->tdie_temp_max, true},
        {"TDIEMin", status->tdie_temp_min, true},
        {"ChargePertance", status->battery_charge_x10, soc},
        {"Capacity", status->battery_capacity,
         has_fields(mask, POWER_VALID_CAPACITY)},
        {"BatteryCapacityUnknown", status->battery_capacity_unknown ? 1 : 0,
         has_fields(mask, POWER_VALID_SUMMARY)},
        {"GsmSig", status->gsm_signal, true}, {"GsmRAT", status->gsm_rat, true},
        {"GsmRxlev", gsm_info_get_signal_quality_2G(), true},
        {"GsmRscp", gsm_info_get_signal_quality_3G(), true},
        {"GsmRsrp", gsm_info_get_signal_quality_4G(), true},
        {"GsmRsrq", gsm_info_get_4G_rsrq(), true},
        {"GsmCREG", gsm_info_get_creg(), true},
        {"GsmCGREG", gsm_info_get_cgreg(), true},
        {"GsmCEREG", gsm_info_get_cereg(), true},
        {"BataryaAkimi", status->battery_current,
         has_fields(mask, POWER_VALID_CHARGER | POWER_VALID_DISCHARGE)},
        {"BatteryTemp", status->battery_temp_x10,
         has_fields(mask, POWER_VALID_BATTERY_TEMP)},
        {"BatterySOC", status->battery_soc_x10, soc},
        {"BatterySOH", status->battery_soh_x10,
         has_fields(mask, POWER_VALID_SOH)},
        {"OrtamSicakligi", 0, false}, {"HeaterState", 0, false},
        {"HeaterPower", 0, false}, {"PowerValidFields", mask, true},
        {"BatterySOCAbsolute", has_fields(mask, POWER_SOC_ABSOLUTE) ? 1 : 0,
         has_fields(mask, POWER_VALID_SOC)}
    };

    (void)xsnprintf(text, sizeof(text),
        "{\"DIN\":[%u,%u,%u,%u],\"RLY\":[%u,%u]",
        status->din[0], status->din[1], status->din[2], status->din[3],
        status->rly[0], status->rly[1]);
    bool ok = append(buffer, capacity, length, text);

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
