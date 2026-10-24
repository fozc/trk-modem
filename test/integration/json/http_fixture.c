/*
 * http_fixture.c
 *
 *  Created on: Oct 23, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Actual HTTP JSON handlers with storage, clock and terminal boundaries.
 */
#include "json_config.h"
#include "modem_config.h"
#include "web_json.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "modbus_config.h"
#include "modbus_map_json.h"
#include "boot.h"
#include "version.h"
#include "elog.h"
#include "fault_log.h"
#include "cp56time2a.h"
#include "datetime.h"
#include "web_shell.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char canvas[8194];
static char response[8192];
static int response_status;
static modem_config_t device;
static iec104_config_t iec;
static modbus_configs_t modbus;
static iec104_line_config_t lines[MAX_ARRAYS];
static fw_info_t firmware;
static struct
{
    char *tx_buffer;
    int tx_buffer_size;
    const char *current_query_string;
} handler_state;
static json_writer_t *terminal_writer;
static size_t terminal_bytes;
static uint32_t terminal_calls;

uint32_t modem_config_get_lifetime(void)
{
    return 123456789U;
}

const modem_config_t *get_device_config(void)
{
    return &device;
}
const iec104_config_t *iec104_config_get(void)
{
    return &iec;
}
const modbus_configs_t *modbus_config_get(void)
{
    return &modbus;
}
const fw_info_t *boot_get_installed_fw_info(void)
{
    return &firmware;
}
const iec104_line_config_t *iec104_get_line_config(uint32_t index)
{
    return index < MAX_ARRAYS ? &lines[index] : NULL;
}
uint32_t modbus_config_get_last_error_time(void)
{
    return 42U;
}
static uint32_t rtc_get_epoch(void)
{
    return 123456U;
}
uint32_t bsp_get_run_time(void)
{
    return 30000U;
}
bsp_rtc_t bsp_get_datetime(void)
{
    bsp_rtc_t date = {0};
    return date;
}
static bool nvram_retain_output_states(void)
{
    return true;
}
static int xstrtoi(const char *text)
{
    return (int)strtol(text, NULL, 10);
}
static void http_send_json(const char *text, int length)
{
    assert(length >= 0 && (size_t)length < sizeof(response));
    memcpy(response, text, (size_t)length);
    response[length] = '\0';
    response_status = 200;
}
static void http_send_error(int status, const char *text)
{
    (void)text;
    response_status = status;
    response[0] = '\0';
}
static void http_send_bad_request(void)
{
    http_send_error(400, "bad request");
}

uint32_t elog_get_stored_count(void)
{
    return 20U;
}
int elog_read_recent(uint32_t skip, uint32_t count, elog_entry_t *entries,
                     uint32_t *read_count)
{
    *read_count = 0U;
    while (*read_count < count && skip + *read_count < 20U)
    {
        elog_entry_t *entry = &entries[*read_count];
        memset(entry, 0, sizeof(*entry));
        entry->timestamp = 1000U + skip + *read_count;
        entry->level = 1U;
        ++*read_count;
    }
    return 0;
}
const char *elog_info_to_text(const elog_entry_t *entry)
{
    (void)entry;
    return "quote\" slash\\ control\n UTF8:\xC4\xB0";
}
void dt_conv_from_epoch(time32_t epoch, datetime_t *date)
{
    (void)epoch;
    memset(date, 0, sizeof(*date));
    date->date.year = 2026U;
    date->date.month = 10U;
    date->date.day = 23U;
}
uint8_t fault_log_get_temp_count(uint8_t feeder, uint8_t phase)
{
    (void)feeder;
    (void)phase;
    return 15U;
}
uint8_t fault_log_get_perm_count(uint8_t feeder, uint8_t phase)
{
    return fault_log_get_temp_count(feeder, phase);
}
bool fault_log_read_nth(uint8_t feeder, uint8_t phase, fault_log_type_t type,
                        uint8_t index, fault_log_t *entry)
{
    (void)feeder;
    (void)phase;
    (void)type;
    assert(index < 15U);
    memset(entry, 0, sizeof(*entry));
    entry->fault_current = 1234.0F;
    entry->fault_duration_ms = 100U;
    return true;
}
void web_shell_capture_begin(json_writer_t *writer)
{
    terminal_writer = writer;
    terminal_bytes = 0U;
    assert(json_write_string_begin(writer) == JSON_OK);
}
void web_shell_on_rx(const uint8_t *data, uint16_t length)
{
    assert(length == 8U && memcmp(data, "rf \"x\"\\y", length) == 0);
    ++terminal_calls;
    web_json_text(terminal_writer, "ok\r\n");
    terminal_bytes = 4U;
}
int web_shell_capture_end(void)
{
    assert(json_write_string_end(terminal_writer) == JSON_OK);
    return (int)terminal_bytes;
}

#include "http_production.inc"

static void reset_response(void)
{
    memset(canvas, '*', sizeof(canvas));
    handler_state.tx_buffer = canvas + 1;
    handler_state.tx_buffer_size = 8192;
    response_status = 0;
}

static void emit_response(void)
{
    assert(response_status == 200);
    assert(canvas[0] == '*' && canvas[8193] == '*');
    puts(response);
}

int main(void)
{
    (void)strcpy(device.apn.apn, "a\"b\\c\n");
    (void)strcpy(device.ntp_server, "pool.ntp.org");
    firmware.year = 2026U;
    (void)memcpy(firmware.short_commit_hash, "abc1234", 8U);
    reset_response();
    handle_get_device_config_json();
    assert(strstr(response, "\"LifeTime\":123456789") != NULL);
    emit_response();

    iec.scada_ip_address = 0xC0A80101U;
    iec.scada_port = 2404U;
    for (uint32_t index = 0U; index < MAX_ARRAYS; ++index)
    {
        lines[index].in_use = 1U;
        lines[index].anlik_akim[0] = iec104_make_ioa_3byte(100U + index);
        lines[index].trip_failed[2] = iec104_make_ioa_3byte(200U + index);
        lines[index].temporary_fault = iec104_make_ioa_3byte(300U + index);
    }
    reset_response();
    handle_get_iec_config_json();
    emit_response();

    modbus.device_addr = 7U;
    modbus.baud_rate = 115200U;
    reset_response();
    handle_get_modbus_config_json();
    emit_response();

    reset_response();
    handler_state.current_query_string = "offset=8&limit=3";
    handle_get_syslogs_json();
    emit_response();
    reset_response();
    handler_state.current_query_string = "offset=99&limit=3";
    handle_get_syslogs_json();
    emit_response();

    reset_response();
    handler_state.current_query_string = "feeder=0";
    handle_get_fault_records_json();
    emit_response();

    reset_response();
    handle_post_web_shell("{\"tx\":\"rf \\\"x\\\"\\\\y\"}");
    emit_response();
    assert(terminal_calls == 1U);
    handle_post_web_shell("{\"tx\":\"rf a\"} trailing");
    assert(response_status == 400 && terminal_calls == 1U);
    handle_post_web_shell("{\"tx\":\"rf a\\u0000b\"}");
    assert(response_status == 400 && terminal_calls == 1U);

    uint32_t size = 0U, chunks = 0U, hash = 0U;
    assert(parse_fw_metadata("{\"s\":4096,\"c\":1,\"h\":42}", &size, &chunks,
                             &hash));
    assert(size == 4096U && chunks == 1U && hash == 42U);
    assert(!parse_fw_metadata("{\"s\":-1}", &size, &chunks, &hash));
    assert(!parse_fw_metadata("{\"s\":4294967296}", &size, &chunks, &hash));
    assert(!parse_fw_metadata("{\"s\":4096} trailing", &size, &chunks, &hash));
    reset_response();
    handler_state.tx_buffer_size = 8;
    handle_get_device_config_json();
    assert(response_status == 500 && canvas[9] == '*');
    return 0;
}
/*** end of file ***/
