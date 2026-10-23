/*
 * device_tests.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Temporary signed test image: real Unity tests and DWT comparisons.
 */
#include "json.h"
#include "unity.h"
#include "shell.h"
#include "bsp.h"
#include "stack_monitor.h"
#include "bsp/cycle_counter.h"
#include "fixtures.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
    const char *data;
    size_t length;
    uint32_t test_case;
    bool modern;
} compare_request_t;

extern uint32_t json_compare_once(const compare_request_t *request,
                                   char *output, size_t capacity);
extern int json_hw_run_unity(void);
extern void __real_shell_init(const char *prompt, const char *password,
                               shell_putchar_fn_t putchar_fn);
void __wrap_shell_init(const char *prompt, const char *password,
                        shell_putchar_fn_t putchar_fn);

static char result_buffer[8192];
static char expected_buffer[8192];
static uint32_t float_calls;

/* Only unit test objects redirect their float calls through this watchdog
 * boundary. Benchmark objects call the unmodified production API directly.
 */
json_error_t json_hw_token_float(const json_token_t *token, float *value)
{
    if ((++float_calls & 255U) == 0U)
    {
        bsp_kick_wdt();
    }
    return json_token_float(token, value);
}

void json_hw_putchar(int byte)
{
    shell_putchr(byte);
    if (byte == '\n')
    {
        bsp_kick_wdt();
    }
}

static bool check_fixture(size_t index)
{
    compare_request_t request = {fixtures[index].data, fixtures[index].length,
                                  fixtures[index].test_case, false};
    uint32_t expected = json_compare_once(&request, expected_buffer,
                                          sizeof(expected_buffer));
    request.modern = true;
    uint32_t length = json_compare_once(&request, result_buffer,
                                        sizeof(result_buffer));
    return length != 0U && length == expected &&
           memcmp(result_buffer, expected_buffer, length) == 0;
}

static void benchmark(size_t index, bool modern)
{
    compare_request_t request = {fixtures[index].data, fixtures[index].length,
                                  fixtures[index].test_case, modern};
    uint32_t samples[9];
    uint32_t checksum = 0U;
    stack_monitor_reset();
    for (size_t round = 0U; round < 9U; ++round)
    {
        bsp_kick_wdt();
        uint32_t before = cycle_counter_read();
        for (uint32_t iteration = 0U; iteration < 128U; ++iteration)
        {
            uint32_t length = json_compare_once(&request, result_buffer,
                                                 sizeof(result_buffer));
            checksum += length + (uint8_t)result_buffer[0];
        }
        samples[round] = (cycle_counter_read() - before) / 128U;
    }
    stack_monitor_stats_t stats = stack_monitor_get_stats();
    for (size_t item = 1U; item < 9U; ++item)
    {
        uint32_t value = samples[item];
        size_t position = item;
        while (position != 0U && samples[position - 1U] > value)
        {
            samples[position] = samples[position - 1U];
            --position;
        }
        samples[position] = value;
    }
    SHELL_LOG("JSONHW case=%u new=%u cycles=%u min=%u max=%u "
              "stack=%u checksum=%u\r\n", (unsigned int)index,
              (unsigned int)modern, (unsigned int)samples[4],
              (unsigned int)samples[0], (unsigned int)samples[8],
              (unsigned int)stats.used_bytes, (unsigned int)checksum);
}

static int json_hw_command(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "unit") == 0)
    {
        bsp_kick_wdt();
        stack_monitor_reset();
        int failed = json_hw_run_unity();
        stack_monitor_stats_t stats = stack_monitor_get_stats();
        SHELL_LOG("JSONHW unit failed=%u stack=%u total=%u\r\n",
                  (unsigned int)failed, (unsigned int)stats.used_bytes,
                  (unsigned int)stats.total_bytes);
        return failed;
    }
    if (argc != 2 || strcmp(argv[1], "bench") != 0)
    {
        SHELL_LOG("jsonhw unit|bench\r\n");
        return 1;
    }
    if (!cycle_counter_start())
    {
        cycle_counter_stop();
        SHELL_LOG("JSONHW DWT unavailable\r\n");
        return 1;
    }
    SHELL_LOG("JSONHW hz=%u irq=enabled iterations=128 rounds=9\r\n",
              (unsigned int)SystemCoreClock);
    for (size_t index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]);
         ++index)
    {
        if (!check_fixture(index))
        {
            cycle_counter_stop();
            SHELL_LOG("JSONHW parity FAIL case=%u\r\n", (unsigned int)index);
            return 1;
        }
        benchmark(index, false);
        benchmark(index, true);
    }
    cycle_counter_stop();
    SHELL_LOG("JSONHW bench PASS\r\n");
    return 0;
}

void __wrap_shell_init(const char *prompt, const char *password,
                        shell_putchar_fn_t putchar_fn)
{
    __real_shell_init(prompt, password, putchar_fn);
    const shell_cmd_t command = {"jsonhw", "Temporary JSON hardware tests",
                                  SHELL_LVL_SUPER_USER, json_hw_command};
    if (shell_register_command(&command) < 0)
    {
        SHELL_LOG("JSONHW command registration failed\r\n");
    }
}

/*** end of file ***/
