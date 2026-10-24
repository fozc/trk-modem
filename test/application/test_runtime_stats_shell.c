/*
 * test_runtime_stats_shell.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * Read-only counters through the registered production shell handler.
 */
#include "unity.h"
#undef NO_SHELL_LOG
#undef DISABLE_SHELL_LOG
#include "mock_shell.h"
#include "mock_runtime_stats.h"
#include "xprintf.h"
#include <string.h>
#define runtime_stats_shell_init runtime_stats_shell_register_real
#include "../../Application/runtime_stats_shell.c"
#undef runtime_stats_shell_init

static shell_cmd_t registered;
static char output[4096];
static size_t used;
static uint32_t read_calls;
static runtime_stats_t current;

static int register_command(const shell_cmd_t *command, int call)
{
    (void)call;
    registered = *command;
    return 0;
}

static void capture(int ch, int call)
{
    (void)call;
    TEST_ASSERT_TRUE(used + 1U < sizeof(output));
    output[used++] = (char)ch;
    output[used] = '\0';
}

static bool history(uint32_t skip, runtime_stats_t *out, int call)
{
    (void)call;
    read_calls++;
    (void)memset(out, 0, sizeof(*out));
    out->timestamp = skip;
    out->counters[RUNTIME_LIFETIME] = 123U;
    return true;
}

static void snapshot_copy(runtime_stats_t *out, int call)
{
    (void)call;
    *out = current;
}

static bool unavailable(uint32_t skip, runtime_stats_t *out, int call)
{
    (void)call;
    (void)out;
    TEST_ASSERT_EQUAL_UINT32(0U, skip);
    return false;
}

void setUp(void)
{
    used = 0U;
    output[0] = '\0';
    read_calls = 0U;
    shell_putchr_Stub(capture);
    shell_register_command_Stub(register_command);
    runtime_stats_shell_register_real();
    TEST_ASSERT_EQUAL_STRING("stats", registered.cmd);
    TEST_ASSERT_EQUAL_UINT8(0U, registered.level);
}

void tearDown(void)
{
}

void test_current_stats_uses_snapshot_and_shell_output(void)
{
    (void)memset(&current, 0, sizeof(current));
    current.timestamp = 100U;
    current.clock_valid = true;
    current.counters[RUNTIME_GSM_RX_BYTES] = UINT32_MAX;
    runtime_stats_snapshot_Stub(snapshot_copy);
    char *args[] = {"stats"};
    TEST_ASSERT_EQUAL_INT(0, registered.func(1, args));
    TEST_ASSERT_NOT_NULL(strstr(output, "UTC=100 valid=1 interval=900s"));
    TEST_ASSERT_NOT_NULL(strstr(output, "gsm_rx_bytes=4294967295"));
    TEST_ASSERT_NOT_NULL(strstr(output, "boots=0"));
}

void test_history_paginates_eight_rows_and_stops_at_capacity(void)
{
    runtime_stats_read_Stub(history);
    char *args[] = {"stats", "history", "0"};
    TEST_ASSERT_EQUAL_INT(0, registered.func(3, args));
    TEST_ASSERT_EQUAL_UINT32(8U, read_calls);
    TEST_ASSERT_NOT_NULL(strstr(output, "utc,clock_valid,lifetime_s"));
    TEST_ASSERT_NOT_NULL(strstr(output, "7,0,123,"));
    args[2] = "127";
    TEST_ASSERT_EQUAL_INT(0, registered.func(3, args));
    TEST_ASSERT_EQUAL_UINT32(9U, read_calls);
}

void test_invalid_history_arguments_do_not_read_or_modify_storage(void)
{
    static char *invalid[] = {"", "-1", "+1", "128", "999999999999", "1x"};
    char *args[] = {"stats", "history", ""};
    const size_t count = sizeof(invalid) / sizeof(invalid[0]);
    for (size_t index = 0U; index < count; index++)
    {
        args[2] = invalid[index];
        TEST_ASSERT_EQUAL_INT(-1, registered.func(3, args));
    }
    TEST_ASSERT_EQUAL_INT(-1, registered.func(2, args));
}

void test_unavailable_history_stops_without_printing_uninitialized_row(void)
{
    runtime_stats_read_Stub(unavailable);
    char *args[] = {"stats", "history", "0"};
    TEST_ASSERT_EQUAL_INT(0, registered.func(3, args));
    TEST_ASSERT_EQUAL_CHAR('\n', output[used - 1U]);
    TEST_ASSERT_NULL(strstr(output, "\r\n0,"));
}
/*** end of file ***/
