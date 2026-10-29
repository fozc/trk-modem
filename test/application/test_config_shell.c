/*
 * test_config_shell.c
 *
 *  Created on: Oct 29, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise configuration through the real shell receive/dispatch path.
 */
#include "unity.h"
#undef NO_SHELL_LOG
#undef DISABLE_SHELL_LOG
#include "mock_bsp.h"
#include "mock_modem_config.h"
#include "mock_nvram.h"
#include "mock_rtc.h"
#include "xprintf.h"
#include "xscanf.h"
TEST_SOURCE_FILE("datetime.c")
#include "../../Application/libs/shell.c"
#define config_shell_init config_shell_register_real
#include "../../Application/config_shell.c"
#undef config_shell_init

static modem_config_t current_config;
static modem_config_t stored_config;
static char output[4096];
static size_t output_used;
static uint32_t save_calls;
static nvram_save_result_t save_result;
static bool storage_busy;

static void capture(int chr)
{
    TEST_ASSERT_TRUE(output_used + 1U < sizeof(output));
    output[output_used++] = (char)chr;
    output[output_used] = '\0';
}

static void web_capture(int chr)
{
    capture(chr);
}

static const modem_config_t *get_config(int call)
{
    (void)call;
    return &current_config;
}

static modem_config_t *get_writable_config(int call)
{
    (void)call;
    return &current_config;
}

static bool is_busy(int call)
{
    (void)call;
    return storage_busy;
}

static nvram_save_result_t save(bool force, int call)
{
    (void)call;
    TEST_ASSERT_FALSE(force);
    save_calls++;
    if (save_result != NVRAM_SAVE_FAILED)
    {
        stored_config = current_config;
    }
    return save_result;
}

static void command(const char *line)
{
    for (; *line != '\0'; line++)
    {
        shell_on_rx_received((unsigned char)*line);
    }
    shell_on_rx_received('\r');
    shell_process();
}

void setUp(void)
{
    memset(cmd_list, 0, sizeof(cmd_list));
    cmd_list_counter = 0U;
    session_level = SHELL_LVL_USER;
    shell_reset_rx();
    shell_clear_rx_special_ready();
    output_used = 0U;
    output[0] = '\0';
    memset(&current_config, 0, sizeof(current_config));
    current_config.serial_number = 99999999U;
    current_config.web_interface_port = 8080U;
    current_config.ntp_server_port = 123U;
    stored_config = current_config;
    save_calls = 0U;
    save_result = NVRAM_SAVE_COMPLETE;
    storage_busy = false;
    modem_config_get_Stub(get_config);
    nvram_get_modem_config_rw_Stub(get_writable_config);
    nvram_is_busy_Stub(is_busy);
    nvram_save_Stub(save);
    shell_init("test", "testpass", capture);
    config_shell_register_real();
    command("su testpass");
}

void tearDown(void)
{
}

void test_numeric_settings_are_persisted_and_unrelated_fields_preserved(void)
{
    command("config set serial 26000123");
    command("config set sim-pin 0001");
    command("config set ntp-port 65535");
    command("config set timezone -12");
    TEST_ASSERT_EQUAL_UINT32(4U, save_calls);
    TEST_ASSERT_EQUAL_UINT32(26000123U, stored_config.serial_number);
    TEST_ASSERT_EQUAL_UINT16(1U, stored_config.sim_card_pin);
    TEST_ASSERT_EQUAL_UINT16(65535U, stored_config.ntp_server_port);
    TEST_ASSERT_EQUAL_INT32(-12, stored_config.time_zone);
    TEST_ASSERT_EQUAL_UINT16(8080U, stored_config.web_interface_port);
    command("config set timezone +14");
    TEST_ASSERT_EQUAL_INT32(14, stored_config.time_zone);
}

void test_invalid_numbers_unknown_fields_and_extra_arguments_do_not_write(void)
{
    const char *invalid[] = {"config set serial -1", "config set serial 100000000",
        "config set serial 42949672960", "config set serial 4294967296",
        "config set serial 00000000001", "config set serial 1x", "config set serial 0x10", "config set serial",
        "config set serial 1 extra", "config set ntp-port 0", "config set ntp-port 65536",
        "config set timezone -13", "config set timezone 15", "config set timezone +",
        "config set timezone 3x", "config set timezone 2147483648", "config set timezone -2147483649",
        "config set sim-pin 123", "config set sim-pin 12345", "config set imei 123"};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); index++)
    {
        command(invalid[index]);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
    TEST_ASSERT_EQUAL_MEMORY(&stored_config, &current_config, sizeof(current_config));
}

void test_text_settings_reject_truncation_and_at_injection_and_allow_clear(void)
{
    command("config set apn abcdefghijklmno");
    command("config set apn-user user");
    command("config set ntp-server time.example.com");
    TEST_ASSERT_EQUAL_UINT32(3U, save_calls);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", stored_config.apn.apn);
    command("config set apn abcdefghijklmnop");
    command("config set apn bad\"value");
    command("config set apn-user bad\\value");
    TEST_ASSERT_EQUAL_UINT32(3U, save_calls);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", current_config.apn.apn);
    command("config set apn-user");
    command("config set ntp-server");
    TEST_ASSERT_EQUAL_STRING("", stored_config.apn.user_name);
    TEST_ASSERT_EQUAL_STRING("", stored_config.ntp_server);
}

void test_failed_save_rolls_back_and_busy_storage_does_not_modify_ram(void)
{
    save_result = NVRAM_SAVE_FAILED;
    command("config set serial 123");
    TEST_ASSERT_EQUAL_UINT32(99999999U, current_config.serial_number);
    TEST_ASSERT_NOT_NULL(strstr(output, "Save failed"));
    TEST_ASSERT_NULL(strstr(output, "Saved."));
    storage_busy = true;
    command("config set apn internet");
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    TEST_ASSERT_EQUAL_STRING("", current_config.apn.apn);
}

void test_verified_primary_copy_is_success_with_backup_warning(void)
{
    save_result = NVRAM_SAVE_PRIMARY_ONLY;
    command("config set serial 123");
    TEST_ASSERT_EQUAL_UINT32(123U, stored_config.serial_number);
    TEST_ASSERT_EQUAL_UINT32(123U, current_config.serial_number);
    TEST_ASSERT_NOT_NULL(strstr(output, "Backup copy incomplete"));
}

void test_non_admin_cannot_write_settings_or_start_secret_input(void)
{
    command("exit");
    command("config set serial 123");
    command("config set apn-password");
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
    TEST_ASSERT_NULL(secret_handler);
    TEST_ASSERT_NOT_NULL(strstr(output, "Permission denied"));
}

void test_dates_use_real_calendar_validation_and_epoch_conversion(void)
{
    command("config set production-date 2028-02-29");
    command("config set commissioning-time 2026-11-02 08:30:00");
    TEST_ASSERT_EQUAL_UINT32(2U, save_calls);
    datetime_t date = {0};
    dt_conv_from_epoch(stored_config.production_date, &date);
    TEST_ASSERT_EQUAL_UINT16(2028U, date.date.year);
    TEST_ASSERT_EQUAL_UINT8(29U, date.date.day);
    dt_conv_from_epoch(stored_config.commissioning_time, &date);
    TEST_ASSERT_EQUAL_UINT8(8U, date.time.hour);
    TEST_ASSERT_EQUAL_UINT8(30U, date.time.minute);
    command("config set production-date 2027-02-29");
    command("config set production-date 2026-13-01");
    command("config set production-date 2100-01-01");
    command("config set production-date 2026-1-01");
    command("config set production-date 2026-+1-01");
    command("config set production-date 2026-01-0x");
    command("config set production-date 2026-01-001");
    command("config set commissioning-time 2026-11-02 24:00:00");
    command("config set commissioning-time 2026-11-02");
    TEST_ASSERT_EQUAL_UINT32(2U, save_calls);
}

void test_set_time_calls_unified_rtc_service_without_nvram_write(void)
{
    const rtc_t clock = {.year = 26U, .month = 10U, .day = 29U,
                         .hour = 16U, .minute = 45U, .second = 0U, .millisec = 0U};
    rtc_sync_ExpectAndReturn(&clock, true);
    command("config set time 2026-10-29 16:45:00");
    TEST_ASSERT_NOT_NULL(strstr(output, "RTC updated (UTC)"));
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
}

void test_rtc_failure_is_not_reported_as_success_and_bad_time_never_reaches_rtc(void)
{
    const rtc_t clock = {.year = 26U, .month = 10U, .day = 29U,
                         .hour = 16U, .minute = 45U, .second = 0U, .millisec = 0U};
    rtc_sync_ExpectAndReturn(&clock, false);
    command("config set time 2026-10-29 16:45:00");
    TEST_ASSERT_NOT_NULL(strstr(output, "RTC update failed"));
    TEST_ASSERT_NULL(strstr(output, "RTC updated"));
    command("config set time 2026-02-30 10:00:00");
    command("config set time 2026-10-29 10:00:60");
    command("config set time 2026-10-29 1:00:00");
    command("config set time 2026-10-29 10:00:0x");
    command("config set time 2026-10-29 10:00:00 extra");
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
}

void test_password_is_not_echoed_dispatched_or_left_in_receive_buffer(void)
{
    command("config set apn-password");
    command("reset"); /* A password matching a real command must not execute it. */
    TEST_ASSERT_EQUAL_STRING("reset", stored_config.apn.user_pass);
    TEST_ASSERT_NULL(strstr(output, "reset"));
    for (size_t index = 0U; index < sizeof(rx_buff); index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0U, rx_buff[index]);
    }
    command("config set apn-password");
    command("");
    TEST_ASSERT_EQUAL_STRING("", stored_config.apn.user_pass);
    for (size_t index = 0U; index < MAX_APN_PASSWORD_LEN; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0U, stored_config.apn.user_pass[index]);
    }
}

void test_invalid_password_preserves_value_and_prompt_rejects_inline_secret(void)
{
    strcpy(current_config.apn.user_pass, "original");
    command("config set apn-password inline");
    TEST_ASSERT_NULL(secret_handler);
    command("config set apn-password");
    command("abcdefghijklmnop");
    TEST_ASSERT_EQUAL_STRING("original", current_config.apn.user_pass);
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
    command("config set apn-password");
    command("bad\"secret");
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
}

void test_redirected_shell_cannot_start_or_consume_local_secret_prompt(void)
{
    command("config set apn-password");
    shell_set_putchar(web_capture);
    command("config set serial 123");
    TEST_ASSERT_EQUAL_UINT32(123U, stored_config.serial_number);
    command("config set apn-password");
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    shell_set_putchar(capture);
    command("local-secret");
    TEST_ASSERT_EQUAL_STRING("local-secret", stored_config.apn.user_pass);
}

void test_show_masks_credentials_and_uses_current_rtc(void)
{
    strcpy(current_config.apn.user_pass, "secret-value");
    current_config.sim_card_pin = 1234U;
    command("exit");
    rtc_get_unix_epoch_ExpectAndReturn(1793289600U);
    rtc_hw_is_valid_ExpectAndReturn(true);
    command("config show");
    TEST_ASSERT_NOT_NULL(strstr(output, "apn-password: ****"));
    TEST_ASSERT_NOT_NULL(strstr(output, "sim-pin: ****"));
    TEST_ASSERT_NULL(strstr(output, "secret-value"));
    TEST_ASSERT_NULL(strstr(output, "1234"));
    TEST_ASSERT_NOT_NULL(strstr(output, "RTC valid: yes"));
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
}

void test_maximum_ntp_hostname_fits_shell_and_overlong_command_is_rejected(void)
{
    char line[160] = "config set ntp-server ";
    const size_t prefix = strlen(line);
    memset(line + prefix, 'a', 63U);
    line[prefix + 63U] = '\0';
    command(line);
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    TEST_ASSERT_EQUAL_UINT32(63U, strlen(stored_config.ntp_server));
    line[prefix + 63U] = 'a';
    line[prefix + 64U] = '\0';
    command(line);
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    strcpy(line, "config set serial 123");
    memset(line + strlen(line), ' ', 135U);
    line[155] = '\0';
    command(line);
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    TEST_ASSERT_NOT_NULL(strstr(output, "Command too long; rejected"));
}

void test_password_save_failure_preserves_old_value_and_logout_revokes_pending_write(void)
{
    strcpy(current_config.apn.user_pass, "original");
    save_result = NVRAM_SAVE_FAILED;
    command("config set apn-password");
    command("new-secret");
    TEST_ASSERT_EQUAL_STRING("original", current_config.apn.user_pass);
    command("config set apn-password");
    shell_set_putchar(web_capture);
    command("exit");
    shell_set_putchar(capture);
    command("another-secret");
    TEST_ASSERT_EQUAL_UINT32(1U, save_calls);
    TEST_ASSERT_EQUAL_STRING("original", current_config.apn.user_pass);
    TEST_ASSERT_NULL(secret_handler);
}

void test_embedded_null_in_secret_is_rejected_instead_of_silently_truncated(void)
{
    command("config set apn-password");
    shell_on_rx_received('a');
    shell_on_rx_received(0);
    shell_on_rx_received('b');
    shell_on_rx_received('\r');
    shell_process();
    TEST_ASSERT_EQUAL_UINT32(0U, save_calls);
    TEST_ASSERT_EQUAL_STRING("", current_config.apn.user_pass);
}

void test_crlf_after_opening_prompt_does_not_become_part_of_the_password(void)
{
    command("config set apn-password");
    shell_on_rx_received('\n');
    command("secret");
    shell_on_rx_received('\n');
    TEST_ASSERT_EQUAL_STRING("secret", stored_config.apn.user_pass);
    TEST_ASSERT_EQUAL_UINT32(0U, rx_idx);
}

/*** end of file ***/
