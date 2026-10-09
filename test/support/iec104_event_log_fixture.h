/*
 * iec104_event_log_fixture.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * NOR and terminal hardware model for the real event log entry points.
 * Include after the event log and CMock headers in each test target.
 */
#ifndef TEST_IEC104_EVENT_LOG_FIXTURE_H
#define TEST_IEC104_EVENT_LOG_FIXTURE_H

static uint8_t flash[IEC104_EVTLOG_SECTOR_COUNT * LOG_SECTOR_SIZE];
static iec104_evtlog_state_t replay_state;
static char output[4096U];
static size_t output_len;
static bool program_fails;
static bool erase_fails;
static bool sync_fails;
static uint32_t program_calls;
static uint32_t erase_calls;
static uint32_t sync_calls;
static uint32_t failed_program_call;
static uint32_t silent_program_call;
static size_t torn_program_bytes;
static iec104_evtlog_state_t persisted_replay_state;

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
    program_calls++;
    if (program_fails || (failed_program_call == program_calls))
    {
        const uint8_t *bytes = buffer;
        const size_t written = (torn_program_bytes < length) ?
                                torn_program_bytes : length;
        for (size_t index = 0U; index < written; index++)
        {
            flash[offset + index] &= bytes[index];
        }
        return W25QXX_RES_ERROR;
    }
    if (silent_program_call == program_calls)
    {
        return W25QXX_RES_OK;
    }
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
    erase_calls++;
    if (erase_fails)
    {
        return W25QXX_RES_ERROR;
    }
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

static int sync_nvram(bool crc_no_check, int call_count)
{
    (void)crc_no_check;
    (void)call_count;
    sync_calls++;
    if (sync_fails)
    {
        return -1;
    }
    persisted_replay_state = replay_state;
    return 0;
}

static inline void reboot_fixture(void)
{
    replay_state = persisted_replay_state;
    TEST_ASSERT_TRUE(iec104_event_log_init());
}

static void reset_fixture(void)
{
    (void)memset(flash, 0xFF, sizeof(flash));
    (void)memset(&replay_state, 0, sizeof(replay_state));
    program_fails = false;
    erase_fails = false;
    sync_fails = false;
    program_calls = 0U;
    erase_calls = 0U;
    sync_calls = 0U;
    failed_program_call = 0U;
    silent_program_call = 0U;
    torn_program_bytes = 0U;
    (void)memset(&persisted_replay_state, 0, sizeof(persisted_replay_state));
    output_len = 0U;
    output[0] = '\0';
    w25qxx_read_buff_StubWithCallback(read_flash);
    w25qxx_page_write_StubWithCallback(program_flash);
    w25qxx_erase_sector_StubWithCallback(erase_flash);
    nvram_get_iec104_evtlog_state_IgnoreAndReturn(&replay_state);
    nvram_sync_StubWithCallback(sync_nvram);
    shell_register_command_IgnoreAndReturn(0);
    shell_putchr_StubWithCallback(capture_char);
    bsp_kick_wdt_Ignore();
    TEST_ASSERT_TRUE(iec104_event_log_init());
}


#endif

/*** end of file ***/
