/*
 * test_xmodem_update_scenario.c
 *
 *  Created on: Oct 14, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Drive real update callbacks and watchdog through transport boundaries.
 */
#include "unity.h"
#include "xmodem_process.h"
#include "contiki.h"
#include "spi_flash_organization.h"
#include "mock_xmodem.h"
#include "mock_bsp.h"
#include "mock_boot.h"
#include "mock_w25qxx.h"
#include "mock_app_ipc.h"
#include "mock_shell.h"
#include "mock_console_logger.h"
#include "mock_elog.h"
#include <string.h>

TEST_SOURCE_FILE("xprintf.c")

PROCESS_NAME(xmodem_watchdog_process);
static xmodem_config_t transport;
static shell_cmd_t command;
static uint32_t tick;
static uint32_t can_count;
static uint32_t failures;
static bool timer_ready;
bool console_logger_is_enabled_flag;

void process_start(struct process *process, process_data_t data)
{
    PT_INIT(&process->pt);
    process->state = 1U;
    (void)process->thread(&process->pt, PROCESS_EVENT_INIT, data);
}

void process_exit(struct process *process)
{
    process->state = 0U;
}

int process_is_running(struct process *process)
{
    return (0U != process->state) ? 1 : 0;
}

void etimer_set(struct etimer *timer, clock_time_t interval)
{
    timer->timer.interval = interval;
    timer_ready = false;
}

void etimer_reset(struct etimer *timer)
{
    (void)timer;
    timer_ready = false;
}

int etimer_expired(struct etimer *timer)
{
    (void)timer;
    return timer_ready ? 1 : 0;
}

static int init_transport(xmodem_config_t config, int call_count)
{
    (void)call_count;
    transport = config;
    return 0;
}

static int register_command(const shell_cmd_t *value, int call_count)
{
    (void)call_count;
    command = *value;
    return 0;
}

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return tick;
}

static void write_byte(int value, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_INT(0x18, value);
    can_count++;
}

static void set_logging(bool enabled, bool persist, int call_count)
{
    (void)call_count;
    TEST_ASSERT_FALSE(persist);
    console_logger_is_enabled_flag = enabled;
}

static void log_update(uint8_t source, uint8_t result, uint32_t bytes,
                       int call_count)
{
    (void)bytes;
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT8(ELOG_FW_SRC_XMODEM, source);
    if (ELOG_FW_RESULT_FAIL == result)
    {
        failures++;
    }
}

void setUp(void)
{
    tick = 0U;
    can_count = 0U;
    failures = 0U;
    timer_ready = false;
    console_logger_is_enabled_flag = true;
    bsp_get_tick_StubWithCallback(get_tick);
    bsp_kick_wdt_Ignore();
    bsp_putchr_StubWithCallback(write_byte);
    boot_get_download_address_IgnoreAndReturn(
        SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A));
    shell_get_session_level_IgnoreAndReturn(SHELL_LVL_SUPER_USER);
    shell_register_command_StubWithCallback(register_command);
    shell_putchr_Ignore();
    console_logger_set_enabled_StubWithCallback(set_logging);
    elog_log_fw_update_StubWithCallback(log_update);
    xmodem_init_StubWithCallback(init_transport);
    w25qxx_erase_sector_IgnoreAndReturn(W25QXX_RES_OK);
    w25qxx_write_sector_IgnoreAndReturn(W25QXX_RES_OK);
    xmodem_app_init();
}

void tearDown(void)
{
    char *args[] = {"xmodem", "stop"};
    if (xmodem_is_active())
    {
        (void)command.func(2, args);
    }
}

static void start_download(void)
{
    char *args[] = {"xmodem", "start"};
    TEST_ASSERT_EQUAL_INT(0, command.func(2, args));
    TEST_ASSERT_TRUE(xmodem_is_active());
    TEST_ASSERT_FALSE(console_logger_is_enabled_flag);
}

static void run_watchdog(void)
{
    timer_ready = true;
    (void)xmodem_watchdog_process.thread(&xmodem_watchdog_process.pt,
                                        PROCESS_EVENT_TIMER, NULL);
}

void test_overflow_then_complete_closes_mode_as_failure_without_update(void)
{
    static const uint8_t chunk[128] = {0};
    start_download();
    transport.event_callback(XMODEM_EVT_FILE_DOWNLOAD_STARTED, NULL, 0U);
    const uint32_t capacity =
        SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FIRMWARE_A);
    for (uint32_t received = 0U; received < capacity; received += 128U)
    {
        transport.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED,
                                 chunk, sizeof(chunk));
    }
    transport.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED,
                             chunk, sizeof(chunk));
    TEST_ASSERT_EQUAL_UINT32(4U, can_count);
    transport.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED,
                             chunk, sizeof(chunk));
    TEST_ASSERT_EQUAL_UINT32(4U, can_count);
    transport.event_callback(XMODEM_EVT_FILE_DOWNLOAD_COMPLETE, NULL, 0U);
    run_watchdog();
    TEST_ASSERT_FALSE(xmodem_is_active());
    TEST_ASSERT_TRUE(console_logger_is_enabled_flag);
    TEST_ASSERT_EQUAL_UINT32(1U, failures);
}

void test_empty_complete_is_failure_without_update(void)
{
    start_download();
    transport.event_callback(XMODEM_EVT_FILE_DOWNLOAD_COMPLETE, NULL, 0U);
    run_watchdog();
    TEST_ASSERT_FALSE(xmodem_is_active());
    TEST_ASSERT_TRUE(console_logger_is_enabled_flag);
    TEST_ASSERT_EQUAL_UINT32(1U, failures);
}

void test_invalid_image_header_does_not_request_update(void)
{
    static const uint8_t chunk[128] = {0};
    start_download();
    transport.event_callback(XMODEM_EVT_FILE_DOWNLOAD_STARTED, NULL, 0U);
    transport.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED,
                             chunk, sizeof(chunk));
    transport.event_callback(XMODEM_EVT_FILE_DOWNLOAD_COMPLETE, NULL, 0U);
    app_ipc_read_download_image_id_IgnoreAndReturn(-1);
    run_watchdog();
    TEST_ASSERT_FALSE(xmodem_is_active());
    TEST_ASSERT_EQUAL_UINT32(1U, failures);
}

void test_start_timeout_restores_console_at_boundary(void)
{
    start_download();
    tick = 59999U;
    run_watchdog();
    TEST_ASSERT_TRUE(xmodem_is_active());
    tick = 60000U;
    run_watchdog();
    TEST_ASSERT_FALSE(xmodem_is_active());
    TEST_ASSERT_TRUE(console_logger_is_enabled_flag);
}

/*** end of file ***/
