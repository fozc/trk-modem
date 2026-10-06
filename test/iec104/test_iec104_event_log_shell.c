/*
 * test_iec104_event_log_shell.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify stored event bits through the public shell dump entry point.
 */
#include "unity.h"
#include "iec104_event_log.h"
#include "spi_flash_log.h"
#include "spi_flash_organization.h"
#include "mock_w25qxx.h"
#include "mock_nvram.h"
#include "mock_shell.h"
#include "mock_bsp.h"
#include "mock_breaker.h"
#include "mock_utils.h"
#include <string.h>

TEST_SOURCE_FILE("spi_flash_log.c")
TEST_SOURCE_FILE("crc32.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("xprintf.c")

static uint8_t flash[IEC104_EVTLOG_SECTOR_COUNT * LOG_SECTOR_SIZE];
static iec104_evtlog_state_t replay_state;
static char output[4096U];
static size_t output_len;

static size_t flash_offset(uint32_t address, uint32_t length)
{
    TEST_ASSERT_TRUE(IEC104_EVTLOG_ADDR <= address);
    const size_t offset = (size_t)(address - IEC104_EVTLOG_ADDR);
    TEST_ASSERT_TRUE(sizeof(flash) >= offset);
    TEST_ASSERT_TRUE(sizeof(flash) - offset >= length);
    return offset;
}

static void read_flash(uint32_t address, void *buffer, uint32_t length,
                        int call_count)
{
    (void)call_count;
    (void)memcpy(buffer, &flash[flash_offset(address, length)], length);
}

static int program_flash(uint32_t address, const void *buffer,
                          uint32_t length, int call_count)
{
    (void)call_count;
    const size_t offset = flash_offset(address, length);
    const uint8_t *bytes = buffer;
    TEST_ASSERT_TRUE(256U >= (address % 256U) + length);
    for (size_t index = 0U; index < length; index++)
    {
        flash[offset + index] &= bytes[index];
    }
    return W25QXX_RES_OK;
}

static int erase_flash(uint32_t address, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(0U, address % LOG_SECTOR_SIZE);
    (void)memset(&flash[flash_offset(address, LOG_SECTOR_SIZE)],
                 0xFF, LOG_SECTOR_SIZE);
    return W25QXX_RES_OK;
}

static void capture_char(int ch, int call_count)
{
    (void)call_count;
    TEST_ASSERT_TRUE(sizeof(output) > output_len + 1U);
    output[output_len] = (char)ch;
    output_len++;
    output[output_len] = '\0';
}

void setUp(void)
{
    (void)memset(flash, 0xFF, sizeof(flash));
    (void)memset(&replay_state, 0, sizeof(replay_state));
    output_len = 0U;
    output[0] = '\0';
    w25qxx_read_buff_StubWithCallback(read_flash);
    w25qxx_page_write_StubWithCallback(program_flash);
    w25qxx_erase_sector_StubWithCallback(erase_flash);
    nvram_get_iec104_evtlog_state_IgnoreAndReturn(&replay_state);
    shell_register_command_IgnoreAndReturn(0);
    shell_putchr_StubWithCallback(capture_char);
    bsp_kick_wdt_Ignore();
    TEST_ASSERT_TRUE(iec104_event_log_init());
}

void tearDown(void)
{
}

void test_public_dump_preserves_stored_load_present_and_absent_bits(void)
{
    fault_log_t record = {0};
    record.info.nominal_current_status = 1U;
    record.info.power_status = 0U;
    record.info.type = FAULT_LOG_TYPE_PERMANENT;
    fault_log_set_current_amps(&record, 12.5F);
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    record.info.nominal_current_status = 0U;
    record.info.power_status = 1U;
    TEST_ASSERT_TRUE(iec104_event_log_add(&record, NULL));
    iec104_event_log_dump();
    TEST_ASSERT_NOT_NULL(strstr(output, "P=0 Load=1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "P=1 Load=0"));
    TEST_ASSERT_NULL(strstr(output, " N="));
}

/*** end of file ***/
