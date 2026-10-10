/*
 * test_app_ipc_scenario.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify real IPC encoding and image parsing at mocked Flash/reset boundaries.
 */

#include "unity.h"
#include "app_ipc.h"
#include "boot_ipc.h"
#include "efw.h"
#include "crc32.h"
#include "mock_boot.h"
#include "mock_w25qxx.h"
#include "mock_elog.h"
#include "mock_shell.h"
#include "stm32u3xx.h"
#include <string.h>

TEST_SOURCE_FILE("efw.c")
TEST_SOURCE_FILE("crc32.c")

static boot_ipc_t stored;
static efw_header_t image;
static fw_info_t installed;
static uint32_t reset_count;
static uint32_t barrier_count;
static int erase_result;
static int write_result;
static int verify_result;
static uint8_t flash_stage;
static shell_cmd_t command;

void __DSB(void)
{
    TEST_ASSERT_EQUAL_UINT8(3U, flash_stage);
    barrier_count++;
}

void NVIC_SystemReset(void)
{
    TEST_ASSERT_EQUAL_UINT32(1U, barrier_count);
    reset_count++;
}

static void read_flash(uint32_t address, void *out, uint32_t length,
                       int call_count)
{
    (void)call_count;
    if (BOOT_IPC_FLASH_ADDR == address)
    {
        TEST_ASSERT_EQUAL_UINT32(sizeof(stored), length);
        (void)memcpy(out, &stored, sizeof(stored));
    }
    else
    {
        TEST_ASSERT_EQUAL_UINT32(0x100000U, address);
        TEST_ASSERT_EQUAL_UINT32(sizeof(image), length);
        (void)memcpy(out, &image, sizeof(image));
    }
}

static int erase_flash(uint32_t address, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(BOOT_IPC_FLASH_ADDR, address);
    TEST_ASSERT_EQUAL_UINT8(0U, flash_stage);
    flash_stage = 1U;
    return erase_result;
}

static int write_flash(uint32_t address, const void *data, uint32_t length,
                       int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(BOOT_IPC_FLASH_ADDR, address);
    TEST_ASSERT_EQUAL_UINT32(sizeof(stored), length);
    TEST_ASSERT_EQUAL_UINT8(1U, flash_stage);
    TEST_ASSERT_EQUAL_INT(0, erase_result);
    flash_stage = 2U;
    (void)memcpy(&stored, data, sizeof(stored));
    return write_result;
}

static int verify_flash(uint32_t address, const void *data, uint32_t length,
                        int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL_UINT32(BOOT_IPC_FLASH_ADDR, address);
    TEST_ASSERT_EQUAL_UINT32(sizeof(stored), length);
    TEST_ASSERT_EQUAL_UINT8(2U, flash_stage);
    TEST_ASSERT_EQUAL_INT(0, write_result);
    TEST_ASSERT_EQUAL_MEMORY(&stored, data, sizeof(stored));
    flash_stage = 3U;
    return verify_result;
}

static uint32_t message_crc(void)
{
    return crc32_finalize(crc32_update(crc32_init(), &stored,
        sizeof(stored) - sizeof(stored.crc)));
}

static int register_command(const shell_cmd_t *entry, int call_count)
{
    (void)call_count;
    command = *entry;
    return 0;
}

void setUp(void)
{
    (void)memset(&stored, 0, sizeof(stored));
    (void)memset(&image, 0, sizeof(image));
    (void)memset(&installed, 0, sizeof(installed));
    reset_count = 0U;
    barrier_count = 0U;
    flash_stage = 0U;
    erase_result = 0;
    write_result = 0;
    verify_result = 0;
    (void)memcpy(image.base.magic, "*EFW", 4U);
    image.base.file_version = EFW_FILE_VERSION;
    image.base.auth_type = EFW_AUTH_TYPE_ECDSA_P256;
    image.base.app_size = htole32(0x123456U);
    image.base.app_crc = htole32(0xABCDEF01U);
    (void)memcpy(image.identity.magic, "FWID", 4U);
    image.identity.format_version = 1U;
    image.identity.auth_type = EFW_AUTH_TYPE_ECDSA_P256;
    image.identity.app_size = image.base.app_size;
    w25qxx_read_buff_StubWithCallback(read_flash);
    w25qxx_erase_sector_StubWithCallback(erase_flash);
    w25qxx_write_buff_StubWithCallback(write_flash);
    w25qxx_verify_StubWithCallback(verify_flash);
    boot_get_download_address_IgnoreAndReturn(0x100000U);
}

void tearDown(void)
{
}

void test_update_binds_image_without_approval_and_checks_message_crc(void)
{
    TEST_ASSERT_EQUAL_INT(APP_IPC_OK,
        app_ipc_request_update(0x12345678U, 0x87654321U, false));
    TEST_ASSERT_EQUAL_UINT32(BOOT_IPC_MAGIC, stored.magic);
    TEST_ASSERT_EQUAL_UINT8(0U, stored.self_test_passed);
    TEST_ASSERT_EQUAL_UINT8(BOOT_IPC_REQ_UPDATE_FW, stored.requested_mode);
    TEST_ASSERT_EQUAL_UINT32(0x12345678U, stored.expected_fw_crc);
    TEST_ASSERT_EQUAL_UINT32(0x87654321U, stored.expected_fw_size);
    TEST_ASSERT_EQUAL_UINT32(message_crc(), stored.crc);
    TEST_ASSERT_EQUAL_UINT8(0U, stored._reserved[0]);
    TEST_ASSERT_EQUAL_UINT8(0U, stored._reserved[1]);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_stay_resets_after_verified_write_without_approval(void)
{
    TEST_ASSERT_EQUAL_INT(APP_IPC_OK, app_ipc_request_stay_in_bootloader(true));
    TEST_ASSERT_EQUAL_UINT8(BOOT_IPC_REQ_STAY_IN_BL, stored.requested_mode);
    TEST_ASSERT_EQUAL_UINT8(0U, stored.self_test_passed);
    TEST_ASSERT_EQUAL_UINT32(0U, stored.expected_fw_crc);
    TEST_ASSERT_EQUAL_UINT32(0U, stored.expected_fw_size);
    TEST_ASSERT_EQUAL_UINT32(message_crc(), stored.crc);
    TEST_ASSERT_EQUAL_UINT32(1U, reset_count);
}

void test_each_flash_failure_stops_next_stage_and_never_resets(void)
{
    const int expected[] =
        {APP_IPC_ERR_ERASE, APP_IPC_ERR_WRITE, APP_IPC_ERR_VERIFY};
    for (uint8_t failure = 0U; 3U > failure; failure++)
    {
        flash_stage = 0U;
        erase_result = (0U == failure) ? -1 : 0;
        write_result = (1U == failure) ? -1 : 0;
        verify_result = (2U == failure) ? -1 : 0;
        TEST_ASSERT_EQUAL_INT(expected[failure],
            app_ipc_request_update(1U, 2U, true));
        TEST_ASSERT_EQUAL_UINT8(failure + 1U, flash_stage);
        TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
        TEST_ASSERT_EQUAL_UINT32(0U, barrier_count);
    }
}

void test_approval_requires_valid_magic_crc_and_nonzero_self_test(void)
{
    TEST_ASSERT_FALSE(app_ipc_is_firmware_approved());
    stored.magic = BOOT_IPC_MAGIC;
    stored.self_test_passed = 1U;
    stored.crc = message_crc();
    TEST_ASSERT_TRUE(app_ipc_is_firmware_approved());
    stored.crc ^= 1U;
    TEST_ASSERT_FALSE(app_ipc_is_firmware_approved());
    stored.self_test_passed = 0U;
    stored.crc = message_crc();
    TEST_ASSERT_FALSE(app_ipc_is_firmware_approved());
}

void test_already_approved_does_not_erase_log_or_reset(void)
{
    stored.magic = BOOT_IPC_MAGIC;
    stored.self_test_passed = 1U;
    stored.crc = message_crc();
    TEST_ASSERT_EQUAL_INT(APP_IPC_OK_ALREADY_APPROVED,
                         app_ipc_approve_firmware(true));
    TEST_ASSERT_EQUAL_UINT8(0U, flash_stage);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_approval_binds_installed_identity_without_requesting_update(void)
{
    installed.size = 123456U;
    installed.fw_crc = 0x12345678U;
    boot_get_installed_fw_info_ExpectAndReturn(&installed);
    elog_log_fw_approved_Expect();
    TEST_ASSERT_EQUAL_INT(APP_IPC_OK, app_ipc_approve_firmware(false));
    TEST_ASSERT_EQUAL_UINT8(1U, stored.self_test_passed);
    TEST_ASSERT_EQUAL_UINT8(BOOT_IPC_REQ_NONE, stored.requested_mode);
    TEST_ASSERT_EQUAL_UINT32(installed.size, stored.expected_fw_size);
    TEST_ASSERT_EQUAL_UINT32(installed.fw_crc, stored.expected_fw_crc);
    TEST_ASSERT_EQUAL_UINT32(message_crc(), stored.crc);
}

void test_partial_installed_identity_uses_unbound_contract(void)
{
    for (uint8_t variant = 0U; 3U > variant; variant++)
    {
        flash_stage = 0U;
        stored.magic = 0U;
        installed.size = (1U == variant) ? 0U : 123U;
        installed.fw_crc = (2U == variant) ? 0U : 456U;
        boot_get_installed_fw_info_ExpectAndReturn(
            (0U == variant) ? NULL : &installed);
        elog_log_fw_approved_Expect();
        TEST_ASSERT_EQUAL_INT(APP_IPC_OK, app_ipc_approve_firmware(false));
        TEST_ASSERT_EQUAL_UINT32(0U, stored.expected_fw_crc);
        TEST_ASSERT_EQUAL_UINT32(0U, stored.expected_fw_size);
    }
}

void test_download_identity_rejects_invalid_and_empty_images(void)
{
    uint32_t crc = 0U;
    uint32_t size = 0U;
    TEST_ASSERT_EQUAL_INT(0, app_ipc_read_download_image_id(&crc, &size));
    TEST_ASSERT_EQUAL_UINT32(0xABCDEF01U, crc);
    TEST_ASSERT_EQUAL_UINT32(0x123456U, size);
    const efw_header_t valid = image;
    for (uint8_t variant = 0U; 3U > variant; variant++)
    {
        image = valid;
        if (0U == variant)
        {
            image.identity.magic[0] = 0U;
        }
        else if (1U == variant)
        {
            image.base.app_crc = 0U;
        }
        else
        {
            image.base.app_size = 0U;
            image.identity.app_size = 0U;
        }
        TEST_ASSERT_EQUAL_INT(-1, app_ipc_read_download_image_id(&crc, &size));
        TEST_ASSERT_EQUAL_UINT32(0xABCDEF01U, crc);
        TEST_ASSERT_EQUAL_UINT32(0x123456U, size);
    }
    TEST_ASSERT_EQUAL_INT(-1, app_ipc_read_download_image_id(NULL, &size));
    TEST_ASSERT_EQUAL_INT(-1, app_ipc_read_download_image_id(&crc, NULL));
}

void test_shell_registration_status_and_bad_input_never_write_or_reset(void)
{
    shell_register_command_StubWithCallback(register_command);
    app_ipc_init();
    TEST_ASSERT_EQUAL_STRING("boot", command.cmd);
    TEST_ASSERT_EQUAL_INT(SHELL_LVL_SUPER_USER, command.level);
    char *status_args[] = {"boot", "status"};
    char *bad_args[] = {"boot", "unknown"};
    TEST_ASSERT_EQUAL_INT(-1, command.func(1, bad_args));
    TEST_ASSERT_EQUAL_INT(-1, command.func(2, bad_args));
    TEST_ASSERT_EQUAL_INT(0, command.func(2, status_args));
    TEST_ASSERT_EQUAL_UINT8(0U, flash_stage);
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

void test_shell_update_and_stay_propagate_flash_failure_without_reset(void)
{
    shell_register_command_StubWithCallback(register_command);
    app_ipc_init();
    char *update_args[] = {"boot", "update"};
    char *stay_args[] = {"boot", "stay"};
    erase_result = -1;
    elog_log_fw_update_Expect(ELOG_FW_SRC_BOOT_CMD, ELOG_FW_RESULT_START, 0U);
    TEST_ASSERT_EQUAL_INT(APP_IPC_ERR_ERASE, command.func(2, update_args));
    flash_stage = 0U;
    TEST_ASSERT_EQUAL_INT(APP_IPC_ERR_ERASE, command.func(2, stay_args));
    TEST_ASSERT_EQUAL_UINT32(0U, reset_count);
}

/*** end of file ***/
