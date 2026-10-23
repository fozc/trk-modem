/*
 * compare.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host/ARM comparison adapters. Not linked into application firmware.
 */
#include "json.h"
#include "json_config.h"
#include "web_json.h"
#include "system_status.h"
#include "xprintf.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static const modem_config_t device_snapshot =
{
    .serial_number = 123456U, .production_date = 1700000000U
};

static const modem_config_t *modem_config_get(void)
{
    return &device_snapshot;
}

/* Regenerated from current production sources for every comparison. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "compare_legacy.inc"
#pragma GCC diagnostic pop

typedef uint32_t (*compare_job_t)(const char *, size_t, char *, size_t);

uint32_t compare_old_uint(const char *input, size_t length,
                           char *output, size_t capacity)
{
    uint32_t value;
    config_reader_t context;
    if (capacity < sizeof(value) || !config_reader_init(&context, input) ||
        json_read(&context.reader, &context.token) != JSON_OK ||
        context.reader.consumed != length ||
        json_token_uint32(&context.token, &value) != JSON_ERROR_NONE)
    {
        return 0U;
    }
    memcpy(output, &value, sizeof(value));
    return (uint32_t)sizeof(value);
}

uint32_t compare_new_uint(const char *input, size_t length,
                           char *output, size_t capacity)
{
    uint32_t value;
    json_token_t token = {JSON_NUMBER, {input, length}, 0U, false};
    if (capacity < sizeof(value) ||
        json_token_uint32(&token, &value) != JSON_ERROR_NONE)
    {
        return 0U;
    }
    memcpy(output, &value, sizeof(value));
    return (uint32_t)sizeof(value);
}

uint32_t compare_old_float(const char *input, size_t length,
                           char *output, size_t capacity)
{
    float value;
    config_reader_t context;
    if (capacity < sizeof(value) || !config_reader_init(&context, input) ||
        json_read(&context.reader, &context.token) != JSON_OK ||
        context.reader.consumed != length ||
        json_token_float(&context.token, &value) != JSON_ERROR_NONE)
    {
        return 0U;
    }
    memcpy(output, &value, sizeof(value));
    return (uint32_t)sizeof(value);
}

uint32_t compare_new_float(const char *input, size_t length,
                            char *output, size_t capacity)
{
    float value;
    json_token_t token = {JSON_NUMBER, {input, length}, 0U, false};
    if (capacity < sizeof(value) ||
        json_token_float(&token, &value) != JSON_ERROR_NONE)
    {
        return 0U;
    }
    memcpy(output, &value, sizeof(value));
    return (uint32_t)sizeof(value);
}

uint32_t compare_old_modbus(const char *input, size_t length,
                             char *output, size_t capacity)
{
    jmodbus_configs_t config = {0};
    (void)length;
    config.device_addr = 1U;
    config.stop_bits = 1U;
    config.baud_rate = 9600U;
    if (capacity < sizeof(config) || parse_modbus_config(input, &config) == 0)
    {
        return 0U;
    }
    memcpy(output, &config, sizeof(config));
    return (uint32_t)sizeof(config);
}

enum
{
    MODBUS_DEVICE_ID, MODBUS_STOP_BITS, MODBUS_LAST_ERROR,
    MODBUS_BAUD_RATE, MODBUS_FIELD_COUNT
};

enum
{
    DEVICE_WEB_PORT, DEVICE_WEB_FIRST_TIMEOUT, DEVICE_WEB_IDLE_TIMEOUT,
    DEVICE_IEC_FIRST_TIMEOUT, DEVICE_IEC_IDLE_TIMEOUT, DEVICE_PIN,
    DEVICE_APN, DEVICE_APN_PASSWORD, DEVICE_APN_USERNAME, DEVICE_NTP_SERVER,
    DEVICE_NTP_PORT, DEVICE_TIME, DEVICE_TIMEZONE, DEVICE_RESET_PERIOD,
    DEVICE_COMMISSIONING_TIME, DEVICE_FIELD_COUNT
};

static const char *const modbus_keys[] =
{
    "CihazID", "StopBits", "SonHataKodu", "BaudRate"
};

static const char *const device_keys[] =
{
    "WebArayuzuPortu", "WebIlkVeriZamanAsimi", "WebBostaKalmaZamanAsimi",
    "IEC104IlkVeriZamanAsimi", "IEC104BostaKalmaZamanAsimi", "SimKartPin",
    "SimKartAPN", "SimKartAPNSifresi", "SimKartAPNUsername", "NtpServer",
    "NtpServerPortu", "Time", "TimeZone", "PeriyodikModemResetPeriyodu",
    "DevreyeAlinmaZamani"
};

_Static_assert(sizeof(modbus_keys) / sizeof(modbus_keys[0]) ==
               MODBUS_FIELD_COUNT, "Modbus benchmark field count");
_Static_assert(sizeof(device_keys) / sizeof(device_keys[0]) ==
               DEVICE_FIELD_COUNT, "Device benchmark field count");

static bool open_reader(json_reader_t *reader, char *scratch,
                        json_span_t input)
{
    json_token_t token;
    return json_reader_init(reader, scratch, 256U) == JSON_OK &&
           json_reader_feed(reader, input, true) == JSON_OK &&
           json_read(reader, &token) == JSON_OK &&
           token.type == JSON_OBJECT_BEGIN;
}

static json_status_t read_config_field(json_reader_t *reader,
                                        const char *const *names,
                                        size_t count, json_token_t *token,
                                        size_t *field)
{
    for (;;)
    {
        if (json_read(reader, token) != JSON_OK)
        {
            return JSON_ERROR;
        }
        if (token->type == JSON_OBJECT_END)
        {
            return json_read(reader, token) == JSON_DONE ?
                   JSON_DONE : JSON_ERROR;
        }
        if (token->type != JSON_KEY)
        {
            return JSON_ERROR;
        }
        *field = 0U;
        while (*field < count && !json_token_equals(token, names[*field]))
        {
            ++*field;
        }
        if (*field != count)
        {
            return json_read(reader, token);
        }
        if (json_skip_value(reader) != JSON_OK)
        {
            return JSON_ERROR;
        }
    }
}

uint32_t compare_new_modbus(const char *input, size_t length,
                             char *output, size_t capacity)
{
    jmodbus_configs_t config = {0};
    json_reader_t reader;
    json_token_t token;
    char scratch[256];
    json_span_t text = {input, length};
    config.device_addr = 1U;
    config.stop_bits = 1U;
    config.baud_rate = 9600U;
    if (capacity < sizeof(config) || !open_reader(&reader, scratch, text))
    {
        return 0U;
    }
    size_t field;
    json_status_t status = read_config_field(&reader, modbus_keys,
        MODBUS_FIELD_COUNT, &token, &field);
    while (status == JSON_OK)
    {
        uint8_t value;
        if (field < MODBUS_BAUD_RATE)
        {
            if (json_token_uint8(&token, &value) != JSON_ERROR_NONE)
            {
                return 0U;
            }
            if (field == MODBUS_DEVICE_ID)
            {
                config.device_addr = value;
            }
            else if (field == MODBUS_STOP_BITS)
            {
                config.stop_bits = value;
            }
            else
            {
                config.last_error_code = value;
            }
        }
        else if (json_token_uint32(&token, &config.baud_rate) !=
                 JSON_ERROR_NONE)
        {
            return 0U;
        }
        else
        {
            /* The baud rate was stored. */
        }
        status = read_config_field(&reader, modbus_keys,
                                   MODBUS_FIELD_COUNT, &token, &field);
    }
    if (status != JSON_DONE ||
        !validate_modbus_device_id(config.device_addr) ||
        !validate_baud_rate(config.baud_rate) ||
        (config.stop_bits != 1U && config.stop_bits != 2U))
    {
        return 0U;
    }
    memcpy(output, &config, sizeof(config));
    return (uint32_t)sizeof(config);
}

static bool set_device_u16(modem_config_t *config, size_t field,
                            const json_token_t *token)
{
    uint16_t value;
    if (json_token_uint16(token, &value) != JSON_ERROR_NONE ||
        ((field == DEVICE_WEB_PORT || field == DEVICE_NTP_PORT) &&
         !validate_port(value)))
    {
        return false;
    }
    switch (field)
    {
        case DEVICE_WEB_PORT:
            config->web_interface_port = value;
            break;
        case DEVICE_WEB_FIRST_TIMEOUT:
            config->web_first_data_timeout_sec = value;
            break;
        case DEVICE_WEB_IDLE_TIMEOUT:
            config->web_idle_timeout_sec = value;
            break;
        case DEVICE_IEC_FIRST_TIMEOUT:
            config->iec104_first_data_timeout_sec = value;
            break;
        case DEVICE_IEC_IDLE_TIMEOUT:
            config->iec104_idle_timeout_sec = value;
            break;
        case DEVICE_PIN:
            config->sim_card_pin = value;
            break;
        case DEVICE_NTP_PORT:
            config->ntp_server_port = value;
            break;
        default: return false;
    }
    return true;
}

static bool set_device_u32(modem_config_t *config, size_t field,
                            const json_token_t *token)
{
    uint32_t value;
    if (json_token_uint32(token, &value) != JSON_ERROR_NONE)
    {
        return false;
    }
    switch (field)
    {
        case DEVICE_TIME:
            config->time = value;
            break;
        case DEVICE_RESET_PERIOD:
            config->periodic_modem_reset_period = value;
            break;
        case DEVICE_COMMISSIONING_TIME:
            config->commissioning_time = value;
            break;
        default: return false;
    }
    return true;
}

static bool set_device_field(modem_config_t *config, size_t field,
                              const json_token_t *token)
{
    if (field <= DEVICE_PIN || field == DEVICE_NTP_PORT)
    {
        return set_device_u16(config, field, token);
    }
    if (field == DEVICE_TIME || field == DEVICE_RESET_PERIOD ||
        field == DEVICE_COMMISSIONING_TIME)
    {
        return set_device_u32(config, field, token);
    }
    int32_t value;
    switch (field)
    {
        case DEVICE_APN:
            return json_token_copy_string(token, config->apn.apn,
                                            MAX_APN_LEN) == JSON_ERROR_NONE;
        case DEVICE_APN_PASSWORD:
            return json_token_copy_string(token, config->apn.user_pass,
                MAX_APN_PASSWORD_LEN) == JSON_ERROR_NONE;
        case DEVICE_APN_USERNAME:
            return json_token_copy_string(token, config->apn.user_name,
                MAX_APN_USER_NAME_LEN) == JSON_ERROR_NONE;
        case DEVICE_NTP_SERVER:
            return json_token_copy_string(token, config->ntp_server,
                MAX_NTP_SERVER_LEN) == JSON_ERROR_NONE;
        case DEVICE_TIMEZONE:
            if (json_token_int32(token, &value) != JSON_ERROR_NONE)
            {
                return false;
            }
            config->time_zone = value;
            return true;
        default:
            return false;
    }
}

uint32_t compare_old_device(const char *input, size_t length,
                             char *output, size_t capacity)
{
    modem_config_t config = {0};
    (void)length;
    if (capacity < sizeof(config) || parse_device_config(input, &config) == 0)
    {
        return 0U;
    }
    memcpy(output, &config, sizeof(config));
    return (uint32_t)sizeof(config);
}

uint32_t compare_new_device(const char *input, size_t length,
                             char *output, size_t capacity)
{
    modem_config_t config = *modem_config_get();
    json_reader_t reader;
    json_token_t token;
    char scratch[256];
    json_span_t text = {input, length};
    if (capacity < sizeof(config) || !open_reader(&reader, scratch, text))
    {
        return 0U;
    }
    size_t field;
    size_t count = DEVICE_FIELD_COUNT;
    json_status_t status = read_config_field(&reader, device_keys, count,
                                              &token, &field);
    while (status == JSON_OK)
    {
        if (!set_device_field(&config, field, &token))
        {
            return 0U;
        }
        status = read_config_field(&reader, device_keys, count, &token, &field);
    }
    if (status != JSON_DONE ||
        (config.sim_card_pin > 0U && !validate_sim_pin(config.sim_card_pin)) ||
        !validate_timezone(config.time_zone) ||
        (config.periodic_modem_reset_period > 0U &&
         !validate_timeout(config.periodic_modem_reset_period,
                            3600U, 2592000U, "Modem reset period")))
    {
        return 0U;
    }
    memcpy(output, &config, sizeof(config));
    return (uint32_t)sizeof(config);
}

static system_status_t status_fixture =
{
    .din = {1U, 0U, 1U, 0U}, .rly = {1U, 0U}, .dip_sw = {1U, 0U},
    .v3v3 = 3301U, .v3v8 = 3802U, .v5v = 4999U, .temp = -7,
    .temp_max = 35, .temp_min = -10, .panel_voltage = 18000U,
    .dc_voltage = 24000U, .input_current = -123, .input_power_10mw = -321,
    .battery_power_10mw = 1024, .power_source = 1U, .telemetry_age = 3U,
    .board_flags = 5U, .board_flags2 = 10U, .alarm_mask = 123456U,
    .battery_voltage = 12500U, .battery_charge_x10 = 987,
    .battery_capacity = 24U, .gsm_signal = -97, .gsm_rat = 4U,
    .battery_current = -550, .battery_temp_x10 = -123,
    .battery_soc_x10 = 987, .battery_soh_x10 = 995U,
    .power_valid_fields = UINT16_MAX, .adc_valid = true,
    .adc_sample_age_ms = 42U
};

uint32_t compare_old_status(const char *input, size_t length,
                             char *output, size_t capacity)
{
    size_t written = 0U;
    (void)input;
    (void)length;
    return system_status_json_build(&status_fixture, output, capacity,
                                      &written) ? (uint32_t)written : 0U;
}

static bool write_status_arrays(json_writer_t *writer,
                                 const system_status_t *status)
{
    const char *names[] = {"DIN", "RLY", "DIP"};
    const uint8_t *values[] = {status->din, status->rly, status->dip_sw};
    const size_t counts[] = {4U, 2U, 2U};
    for (size_t field = 0U; field < MODBUS_BAUD_RATE; ++field)
    {
        json_span_t key = {names[field], strlen(names[field])};
        if (json_write_key(writer, key) != JSON_OK ||
            json_write_array_begin(writer) != JSON_OK)
        {
            return false;
        }
        for (size_t index = 0U; index < counts[field]; ++index)
        {
            if (json_write_uint32(writer, values[field][index]) != JSON_OK)
            {
                return false;
            }
        }
        if (json_write_array_end(writer) != JSON_OK)
        {
            return false;
        }
    }
    return true;
}

static bool write_signed_fields(json_writer_t *writer,
                                 const json_field_t *fields, size_t count)
{
    for (size_t index = 0U; index < count; ++index)
    {
        json_span_t key = {fields[index].name, strlen(fields[index].name)};
        if (json_write_key(writer, key) != JSON_OK ||
            json_write_int32(writer, fields[index].value) != JSON_OK)
        {
            return false;
        }
    }
    return true;
}

/* Field mapping below is regenerated from system_status_json_build. */
#include "compare_status.inc"

uint32_t compare_old_float_write(const char *input, size_t length,
                                  char *output, size_t capacity)
{
    float value;
    if (length != sizeof(value) || capacity > UINT_MAX)
    {
        return 0U;
    }
    memcpy(&value, input, sizeof(value));
    return xsnprintf(output, (unsigned int)capacity, "%.3f", (double)value);
}

uint32_t compare_new_float_write(const char *input, size_t length,
                                  char *output, size_t capacity)
{
    float value;
    json_writer_t writer;
    if (length != sizeof(value) || capacity == 0U)
    {
        return 0U;
    }
    memcpy(&value, input, sizeof(value));
    if (json_writer_init(&writer, output, capacity - 1U) != JSON_OK ||
        json_write_float(&writer, value) != JSON_OK ||
        json_writer_finish(&writer) != JSON_DONE)
    {
        return 0U;
    }
    output[writer.used] = '\0';
    return (uint32_t)writer.used;
}

static compare_job_t get_job(uint32_t test_case, bool modern)
{
    static const compare_job_t jobs[][2] =
    {
        {compare_old_uint, compare_new_uint},
        {compare_old_float, compare_new_float},
        {compare_old_modbus, compare_new_modbus},
        {compare_old_device, compare_new_device},
        {compare_old_status, compare_new_status},
        {compare_old_float_write, compare_new_float_write}
    };
    return test_case < sizeof(jobs) / sizeof(jobs[0]) ?
           jobs[test_case][modern ? 1U : 0U] : NULL;
}

typedef struct
{
    const char *data;
    size_t length;
    uint32_t test_case;
    bool modern;
} compare_request_t;

uint32_t json_compare_once(const compare_request_t *request,
                            char *output, size_t capacity)
{
    compare_job_t job = get_job(request->test_case, request->modern);
    return job != NULL ? job(request->data, request->length,
                              output, capacity) : 0U;
}

size_t json_compare_apn_offset(void)
{
    return offsetof(modem_config_t, apn.apn);
}

/* Expose source-state boundaries, not parser/formatter substitutes. */
void json_compare_status_pattern(uint32_t seed)
{
    uint8_t *bytes = (uint8_t *)&status_fixture;
    bool uniform = seed == 0U || seed == UINT32_MAX;
    uint8_t byte = seed == 0U ? 0U : UINT8_MAX;
    for (size_t index = 0U; index < sizeof(status_fixture); ++index)
    {
        seed = seed * 1664525U + 1013904223U;
        bytes[index] = uniform ? byte : (uint8_t)(seed >> 24U);
    }
    /* Normalize every bool representation before the serializer reads it. */
    status_fixture.board_temp_history_valid = (seed & 1U) != 0U;
    status_fixture.battery_capacity_unknown = (seed & 2U) != 0U;
    status_fixture.adc_valid = (seed & 4U) != 0U;
}

uint32_t json_compare_batch(const compare_request_t *request,
                             uint32_t iterations)
{
    char output[8192];
    uint32_t checksum = 0U;
    compare_job_t job = get_job(request->test_case, request->modern);
    if (job == NULL)
    {
        return 0U;
    }
    for (uint32_t index = 0U; index < iterations; ++index)
    {
        uint32_t length = job(request->data, request->length,
                               output, sizeof(output));
        if (length == 0U)
        {
            return 0U;
        }
        checksum += (uint8_t)output[index % length] + length;
    }
    return checksum;
}

/*** end of file ***/
