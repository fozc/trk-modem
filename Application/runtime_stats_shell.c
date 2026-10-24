/*
 * runtime_stats_shell.c
 *
 * Created on: Oct 24, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Read-only operating counters through the existing serial/web shell.
 */
#include "runtime_stats.h"
#include "shell.h"
#include <stddef.h>
#include <string.h>

static const char *const names[RUNTIME_COUNTER_COUNT] =
{
    "lifetime_s", "gsm_rx_bytes", "gsm_tx_bytes", "web_sessions",
    "iec_server_sessions", "iec_client_sessions", "iec_active_s",
    "gsm_recoveries", "modbus_requests", "modbus_crc_errors",
    "modbus_uart_errors", "modbus_tx_errors", "boots"
};

static int stats_shell(int argc, char *argv[])
{
    runtime_stats_t record;
    if (argc == 1)
    {
        runtime_stats_snapshot(&record);
        SHELL_LOG("UTC=%lu valid=%u interval=900s\r\n",
                  (unsigned long)record.timestamp,
                  (unsigned int)record.clock_valid);
        for (size_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
        {
            SHELL_LOG("%s=%lu\r\n", names[index],
                      (unsigned long)record.counters[index]);
        }
        return 0;
    }
    if ((argc != 3) || (strcmp(argv[1], "history") != 0))
    {
        SHELL_LOG("Usage: stats | stats history <skip 0..127>\r\n");
        return -1;
    }
    uint32_t skip = 0U;
    const char *digit = argv[2];
    if (*digit == '\0')
    {
        return -1;
    }
    for (; *digit != '\0'; digit++)
    {
        if ((*digit < '0') || (*digit > '9') || (skip > 12U))
        {
            return -1;
        }
        skip = (skip * 10U) + (uint32_t)(*digit - '0');
    }
    if (skip >= RUNTIME_STATS_CAPACITY)
    {
        return -1;
    }
    SHELL_LOG("utc,clock_valid");
    for (size_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
    {
        SHELL_LOG(",%s", names[index]);
    }
    SHELL_LOG("\r\n");
    for (uint32_t row = skip; (row < skip + 8U) &&
         (row < RUNTIME_STATS_CAPACITY); row++)
    {
        if (!runtime_stats_read(row, &record))
        {
            break;
        }
        SHELL_LOG("%lu,%u", (unsigned long)record.timestamp,
                  (unsigned int)record.clock_valid);
        for (size_t index = 0U; index < RUNTIME_COUNTER_COUNT; index++)
        {
            SHELL_LOG(",%lu", (unsigned long)record.counters[index]);
        }
        SHELL_LOG("\r\n");
    }
    return 0;
}

void runtime_stats_shell_init(void)
{
    const shell_cmd_t command =
    {
        .cmd = "stats",
        .desc = "Operating counters; stats history <skip>\r\n",
        .level = 0U,
        .func = stats_shell
    };
    (void)shell_register_command(&command);
}

/*** end of file ***/
