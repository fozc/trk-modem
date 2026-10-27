/*
 * test_rf_alarm_log.c
 *
 *  Created on: Oct 13, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Observe the real rf_alarm deliver() announcement through CSLOG macros
 * compiled IN (per-file -UNO_CONSOLE_LOG): text, color, silence on sync
 * failure and on log level OFF, and no duplication on retry.
 */

#include "unity.h"
#include "mock_elog.h"
/* Must precede every include that reaches console_logger_config.h:
 * drops NO_CONSOLE_LOG for this translation unit so the real CSLOG
 * macros (and the announcement guard) compile in. */
#include "console_log_enabled.h"
#include "mock_rf_inventory.h"
#include "mock_iec104_event_log.h"
#include "mock_iec104_replay.h"
#include "mock_rf.h"
#include "mock_bsp.h"
#include "rf_scp_codec.h"
#include "cp56time2a.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")

/* Production source compiled inside this translation unit so the
 * -UNO_CONSOLE_LOG flag of this file also applies to rf_alarm.c. */
#include "rf_alarm.c"


bool console_logger_is_enabled_flag = true;

static uint32_t tick;
static bool binding_ok;
static bool log_enabled;
static char log_lines[8][96];
static const char *log_colors[8];
static size_t log_count;
static bool link_up;
static size_t ack_calls;

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

bool iec104_is_link_active(void);
bool iec104_is_link_active(void)
{
    return link_up;
}

bool iec104_alarm_ack_range_available(void);
bool iec104_alarm_ack_range_available(void)
{
    return true;
}

/* Console seam: the module-bound CSLOG macros land here. */
bool console_logger_level_enabled(log_mod_t module, log_lvl_t min_level);
bool console_logger_level_enabled(log_mod_t module, log_lvl_t min_level)
{
    (void)module;
    (void)min_level;
    return log_enabled;
}

unsigned int xcprintf(const char *color, const char *fmt, ...);
unsigned int xcprintf(const char *color, const char *fmt, ...)
{
    va_list args;

    if (8U > log_count)
    {
        log_colors[log_count] = color;
        va_start(args, fmt);
        (void)vsnprintf(log_lines[log_count], sizeof(log_lines[0]),
                        fmt, args);
        va_end(args);
    }
    log_count++;
    return 0U;
}

unsigned int xsnprintf(char *buff, unsigned int len, const char *fmt, ...);
unsigned int xsnprintf(char *buff, unsigned int len, const char *fmt, ...)
{
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buff, (size_t)len, fmt, args);
    va_end(args);
    return (0 < written) ? (unsigned int)written : 0U;
}

unsigned int xprintf(const char *fmt, ...);
unsigned int xprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0U;
}

void rtc_print_now(void);
void rtc_print_now(void)
{
}

/* rf_config boundary hand-stub: mocking the whole header trips the
 * array-parameter warnings under the strict test flags. */
const rf_feeder_t *rf_store_get(feeder_id_t index);
const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    (void)index;
    return NULL;
}

static bool ack_counter(const uint8_t *eui64, int call_count)
{
    (void)eui64;
    (void)call_count;
    ack_calls++;
    return true;
}

static bool binding_cb(uint8_t source, rf_inventory_entry_t *entry,
                       int call_count)
{
    (void)source;
    (void)call_count;
    if (!binding_ok)
    {
        return false;
    }
    *entry = (rf_inventory_entry_t)
    {
        .zone = 1U, .feeder = 1U, .phase = 1U, .eui64 = {1U},
        .line_index = 2U
    };
    return true;
}

static bool read_cleared_source(uint8_t source, uint32_t now_ms,
                                rf_phase_data_t *out, int call_count)
{
    (void)call_count;
    if (5U != source)
    {
        return false;
    }
    TEST_ASSERT_EQUAL_UINT32(tick, now_ms);
    TEST_ASSERT_NOT_NULL(out);
    *out = (rf_phase_data_t){.has_live = true};
    return true;
}

static bool add_alarm_ok;
static bool add_alarm_result(const iec104_alarm_record_t *record,
                             uint16_t *seq, int call_count)
{
    (void)record;
    (void)call_count;
    *seq = 43U;
    return add_alarm_ok;
}

static rf_scp_event_t alarm_event(void)
{
    rf_scp_event_t event = {0};
    const cp56time2a_t ts = cp56time2a_make(10000U, 0U, 12U, 7U, 3U,
                                            10U, 26U);

    event.event = 101U;
    event.zone = 1U;
    event.feeder = 1U;
    event.phase = 1U;
    event.boot_counter = 1U;
    event.uptime_sec = 1000U;
    event.clock_quality = 1U;
    event.src_eui_hash = rf_scp_eui_hash((const uint8_t[8]){1U});
    (void)memcpy(event.timestamp, &ts, sizeof(ts));
    return event;
}

void setUp(void)
{
    elog_log_rf_trip_failure_Ignore();
    rf_alarm_init();
    tick = 0U;
    binding_ok = true;
    log_enabled = true;
    log_count = 0U;
    ack_calls = 0U;
    (void)memset(log_lines, 0, sizeof(log_lines));
    (void)memset(log_colors, 0, sizeof(log_colors));
    link_up = false;
    rf_get_source_data_IgnoreAndReturn(false);
    add_alarm_ok = true;
    rf_inventory_get_binding_StubWithCallback(binding_cb);
    rf_handle_alarm_event_IgnoreAndReturn(true);
    rf_ack_stored_alarm_StubWithCallback(ack_counter);
    rf_open_trip_failure_alarm_IgnoreAndReturn(true);
    iec104_event_log_add_alarm_StubWithCallback(add_alarm_result);
    iec104_event_log_sync_IgnoreAndReturn(0);
    iec104_replay_send_record_IgnoreAndReturn(true);
    bsp_get_datetime_IgnoreAndReturn((bsp_rtc_t){0});
}

void tearDown(void)
{
}

void test_deliver_announces_active_once_in_red(void)
{
    rf_scp_event_t event = alarm_event();

    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(1U, log_count);
    TEST_ASSERT_EQUAL_STRING("[RF] ALARM ACTIVE: feeder 3 phase 1\r\n",
                             log_lines[0]);
    TEST_ASSERT_EQUAL_STRING(XCOLOR_RED, log_colors[0]);
    TEST_ASSERT_EQUAL_size_t(1U, ack_calls);
}

void test_sync_failure_stays_silent_until_retry_succeeds_once(void)
{
    rf_scp_event_t event = alarm_event();

    elog_log_rf_trip_failure_StopIgnore();
    iec104_event_log_sync_StopIgnore();
    iec104_event_log_sync_ExpectAndReturn(-1);
    iec104_event_log_sync_ExpectAndReturn(0);
    elog_log_rf_trip_failure_Expect(1U, 1U, true);
    TEST_ASSERT_FALSE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(0U, log_count);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    tick = 60000U;
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(1U, log_count);
    TEST_ASSERT_EQUAL_STRING("[RF] ALARM ACTIVE: feeder 3 phase 1\r\n",
                             log_lines[0]);
}

void test_cleared_announcement_is_yellow_and_level_off_is_silent(void)
{
    rf_scp_event_t event = alarm_event();
    const cp56time2a_t ts = cp56time2a_make(20000U, 0U, 12U, 7U, 3U,
                                            10U, 26U);

    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    rf_get_source_data_StopIgnore();
    rf_get_source_data_StubWithCallback(read_cleared_source);
    rf_alarm_live(5U, &ts);
    TEST_ASSERT_FALSE(rf_alarm_is_idle());
    rf_alarm_process(tick);
    TEST_ASSERT_EQUAL_size_t(2U, log_count);
    TEST_ASSERT_EQUAL_STRING("[RF] ALARM CLEARED: feeder 3 phase 1\r\n",
                             log_lines[1]);
    TEST_ASSERT_EQUAL_STRING(XCOLOR_YELLOW, log_colors[1]);

    /* With the console level OFF the same flow prints nothing. */
    rf_alarm_init();
    log_enabled = false;
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    TEST_ASSERT_EQUAL_size_t(2U, log_count);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

void test_elog_records_physical_alarm_edges_once_when_logical_row_differs(void)
{
    rf_scp_event_t event = alarm_event();
    const cp56time2a_t ts = cp56time2a_make(20000U, 0U, 12U, 7U, 3U, 10U, 26U);

    elog_log_rf_trip_failure_StopIgnore();
    elog_log_rf_trip_failure_Expect(1U, 1U, true);
    elog_log_rf_trip_failure_Expect(1U, 1U, false);
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    event.uptime_sec++;
    TEST_ASSERT_TRUE(rf_alarm_record(&event));
    rf_get_source_data_StopIgnore();
    rf_get_source_data_StubWithCallback(read_cleared_source);
    rf_alarm_live(5U, &ts);
    rf_alarm_process(tick);
    rf_alarm_live(5U, &ts);
    rf_alarm_process(tick);
    TEST_ASSERT_TRUE(rf_alarm_is_idle());
}

/*** end of file ***/
