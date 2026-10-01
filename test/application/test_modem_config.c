/*
 * test_modem_config.c
 *
 *  Created on: Sep 28, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verifies NVRAM-backed modem configuration access and string bounds.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mock_nvram.h"
#include "mock_periodic_reset.h"
#include "modem_config.h"

static modem_config_t stored_config;
static bool last_sync_force;
static uint32_t sync_count;

static modem_config_t *get_config_rw_callback(int call_count)
{
    (void)call_count;
    return &stored_config;
}

static const modem_config_t *get_config_callback(int call_count)
{
    (void)call_count;
    return &stored_config;
}

static int sync_callback(bool force, int call_count)
{
    (void)call_count;
    last_sync_force = force;
    sync_count++;
    return 37;
}

void setUp(void)
{
    memset(&stored_config, 0, sizeof(stored_config));
    last_sync_force = true;
    sync_count = 0U;
    nvram_get_modem_config_rw_StubWithCallback(get_config_rw_callback);
    nvram_get_modem_config_StubWithCallback(get_config_callback);
    nvram_sync_StubWithCallback(sync_callback);
}

void tearDown(void)
{
}

void test_modem_config_sync_and_get_delegate_to_nvram(void)
{
    TEST_ASSERT_EQUAL_PTR(&stored_config, modem_config_get());
    TEST_ASSERT_EQUAL_INT(37, modem_config_sync());
    TEST_ASSERT_EQUAL_UINT32(1U, sync_count);
    TEST_ASSERT_FALSE(last_sync_force);
}

void test_modem_config_scalar_accessors_round_trip(void)
{
    modem_config_set_serial_number(12345678UL);
    modem_config_set_web_port(8080U);
    modem_config_set_sim_pin(4321U);
    modem_config_set_ntp_port(123U);
    modem_config_set_time(2000000000UL);
    modem_config_set_timezone(-3);
    modem_config_set_production_date(1700000000UL);
    modem_config_set_lifetime(987654UL);
    modem_config_set_commissioning_time(1800000000UL);
    modem_config_set_web_session_counter(10U);
    modem_config_set_iec_session_counter(20U);

    TEST_ASSERT_EQUAL_UINT32(12345678UL,
                             modem_config_get_serial_number());
    TEST_ASSERT_EQUAL_UINT16(8080U, modem_config_get_web_port());
    TEST_ASSERT_EQUAL_UINT16(4321U, modem_config_get_sim_pin());
    TEST_ASSERT_EQUAL_UINT16(123U, modem_config_get_ntp_port());
    TEST_ASSERT_EQUAL_UINT32(2000000000UL, modem_config_get_time());
    TEST_ASSERT_EQUAL_INT32(-3, modem_config_get_timezone());
    TEST_ASSERT_EQUAL_UINT32(1700000000UL,
                             modem_config_get_production_date());
    TEST_ASSERT_EQUAL_UINT32(987654UL, modem_config_get_lifetime());
    TEST_ASSERT_EQUAL_UINT32(1800000000UL,
                             modem_config_get_commissioning_time());
    TEST_ASSERT_EQUAL_UINT32(10U,
                             modem_config_get_web_session_counter());
    TEST_ASSERT_EQUAL_UINT32(20U,
                             modem_config_get_iec_session_counter());
}

void test_modem_config_timeout_getters_return_stored_values(void)
{
    stored_config.web_first_data_timeout_sec = 11U;
    stored_config.web_idle_timeout_sec = 22U;
    stored_config.iec104_first_data_timeout_sec = 33U;
    stored_config.iec104_idle_timeout_sec = 44U;

    TEST_ASSERT_EQUAL_UINT16(11U,
        modem_config_get_web_first_data_timeout_sec());
    TEST_ASSERT_EQUAL_UINT16(22U,
        modem_config_get_web_idle_timeout_sec());
    TEST_ASSERT_EQUAL_UINT16(33U,
        modem_config_get_iec104_first_data_timeout_sec());
    TEST_ASSERT_EQUAL_UINT16(44U,
        modem_config_get_iec104_idle_timeout_sec());
}

void test_modem_config_strings_are_truncated_and_null_terminated(void)
{
    char long_value[100];

    memset(long_value, 'x', sizeof(long_value));
    long_value[sizeof(long_value) - 1U] = '\0';

    modem_config_set_sim_apn(long_value);
    modem_config_set_sim_apn_username(long_value);
    modem_config_set_sim_apn_password(long_value);
    modem_config_set_ntp_server(long_value);
    modem_config_set_imei(long_value);

    TEST_ASSERT_EQUAL_UINT(MAX_APN_LEN - 1U,
                           strlen(modem_config_get_sim_apn()));
    TEST_ASSERT_EQUAL_UINT(MAX_APN_USER_NAME_LEN - 1U,
                           strlen(modem_config_get_sim_apn_username()));
    TEST_ASSERT_EQUAL_UINT(MAX_APN_PASSWORD_LEN - 1U,
                           strlen(modem_config_get_sim_apn_password()));
    TEST_ASSERT_EQUAL_UINT(MAX_NTP_SERVER_LEN - 1U,
                           strlen(modem_config_get_ntp_server()));
    TEST_ASSERT_EQUAL_UINT(sizeof(stored_config.imei) - 1U,
                           strlen(modem_config_get_imei()));
}

void test_modem_config_null_string_inputs_preserve_values(void)
{
    modem_config_set_sim_apn("apn");
    modem_config_set_sim_apn_username("user");
    modem_config_set_sim_apn_password("pass");
    modem_config_set_ntp_server("ntp");
    modem_config_set_imei("123456789012345");

    modem_config_set_sim_apn(NULL);
    modem_config_set_sim_apn_username(NULL);
    modem_config_set_sim_apn_password(NULL);
    modem_config_set_ntp_server(NULL);
    modem_config_set_imei(NULL);

    TEST_ASSERT_EQUAL_STRING("apn", modem_config_get_sim_apn());
    TEST_ASSERT_EQUAL_STRING("user",
                             modem_config_get_sim_apn_username());
    TEST_ASSERT_EQUAL_STRING("pass",
                             modem_config_get_sim_apn_password());
    TEST_ASSERT_EQUAL_STRING("ntp", modem_config_get_ntp_server());
    TEST_ASSERT_EQUAL_STRING("123456789012345", modem_config_get_imei());
}

void test_modem_config_ntp_active_requires_terminated_nonempty_server(void)
{
    TEST_ASSERT_FALSE(modem_config_is_ntp_active());
    modem_config_set_ntp_server("a");
    TEST_ASSERT_TRUE(modem_config_is_ntp_active());

    memset(stored_config.ntp_server, 'x', sizeof(stored_config.ntp_server));
    TEST_ASSERT_FALSE(modem_config_is_ntp_active());
}

void test_modem_config_reset_period_is_clamped(void)
{
    modem_config_set_reset_period(PERIODIC_RESET_PERIOD_MAX_S);
    TEST_ASSERT_EQUAL_UINT32(PERIODIC_RESET_PERIOD_MAX_S,
                             modem_config_get_reset_period());

    modem_config_set_reset_period(PERIODIC_RESET_PERIOD_MAX_S + 1U);
    TEST_ASSERT_EQUAL_UINT32(PERIODIC_RESET_PERIOD_MAX_S,
                             modem_config_get_reset_period());
}

void test_modem_config_coordinates_and_rf_version_are_null_safe(void)
{
    coordinates_t coordinates = {
        .mcc = "1234",
        .mnc = "5678",
        .lac = "9ABC",
        .ci = "DEF0"
    };
    const uint8_t version[4] = {1U, 2U, 3U, 4U};

    modem_config_set_coordinates(&coordinates);
    modem_config_set_rf_firmware_version(version);
    TEST_ASSERT_EQUAL_MEMORY(&coordinates, modem_config_get_coordinates(),
                             sizeof(coordinates));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(version,
        modem_config_get_rf_firmware_version(), sizeof(version));

    modem_config_set_coordinates(NULL);
    modem_config_set_rf_firmware_version(NULL);
    TEST_ASSERT_EQUAL_MEMORY(&coordinates, modem_config_get_coordinates(),
                             sizeof(coordinates));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(version,
        modem_config_get_rf_firmware_version(), sizeof(version));
}

void test_modem_config_phone_number_is_bounded_and_terminated(void)
{
    phone_number_t input = {{'1', '2', '3', '4', '5', '6', '7', '8',
                             '9', '0', '\0'}};
    phone_number_t output;

    memset(stored_config.phone_num, 'x', sizeof(stored_config.phone_num));
    memset(&output, 0xA5, sizeof(output));
    modem_config_set_simcard_phone_number(&input);

    TEST_ASSERT_EQUAL_STRING("1234567890", stored_config.phone_num);
    modem_config_get_simcard_phone_number(&output);
    TEST_ASSERT_EQUAL_STRING("1234567890", output.number);

    modem_config_set_simcard_phone_number(NULL);
    modem_config_get_simcard_phone_number(NULL);
    TEST_ASSERT_EQUAL_STRING("1234567890", stored_config.phone_num);
}

void test_modem_config_set_copies_writable_fields_and_clamps_period(void)
{
    modem_config_t input = {0};

    input.serial_number = 999U;
    input.web_interface_port = 8088U;
    input.sim_card_pin = 9876U;
    strcpy(input.apn.apn, "internet");
    strcpy(input.apn.user_name, "alice");
    strcpy(input.apn.user_pass, "secret");
    strcpy(input.ntp_server, "pool.ntp.org");
    input.ntp_server_port = 123U;
    input.time_zone = 3;
    input.periodic_modem_reset_period =
        PERIODIC_RESET_PERIOD_MAX_S + 1U;

    modem_config_set(&input);

    TEST_ASSERT_EQUAL_UINT32(0U, stored_config.serial_number);
    TEST_ASSERT_EQUAL_UINT16(8088U, stored_config.web_interface_port);
    TEST_ASSERT_EQUAL_UINT16(9876U, stored_config.sim_card_pin);
    TEST_ASSERT_EQUAL_STRING("internet", stored_config.apn.apn);
    TEST_ASSERT_EQUAL_STRING("alice", stored_config.apn.user_name);
    TEST_ASSERT_EQUAL_STRING("secret", stored_config.apn.user_pass);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", stored_config.ntp_server);
    TEST_ASSERT_EQUAL_UINT16(123U, stored_config.ntp_server_port);
    TEST_ASSERT_EQUAL_INT32(3, stored_config.time_zone);
    TEST_ASSERT_EQUAL_UINT32(PERIODIC_RESET_PERIOD_MAX_S,
                             stored_config.periodic_modem_reset_period);

    modem_config_set(NULL);
    TEST_ASSERT_EQUAL_UINT16(8088U, stored_config.web_interface_port);
}

void test_modem_config_session_callbacks_increment_with_unsigned_wrap(void)
{
    stored_config.web_session_counter = UINT32_MAX;
    stored_config.iec_session_counter = UINT32_MAX;

    modem_config_webserver_client_connected_cb();
    modem_config_iec_client_connected_cb();

    TEST_ASSERT_EQUAL_UINT32(0U, stored_config.web_session_counter);
    TEST_ASSERT_EQUAL_UINT32(0U, stored_config.iec_session_counter);
}

void test_modem_config_imei_accepts_bounded_unterminated_input(void)
{
    /* Deliberately omit NUL: the source has exactly the permitted length. */
    char input[sizeof(stored_config.imei) - 1U];

    memset(input, '7', sizeof(input));
    modem_config_set_web_session_counter(123U);
    modem_config_set_iec_session_counter(456U);
    modem_config_set_imei(input);

    TEST_ASSERT_EQUAL_MEMORY(input, stored_config.imei, sizeof(input));
    TEST_ASSERT_EQUAL_UINT8(0U,
        stored_config.imei[sizeof(stored_config.imei) - 1U]);
    TEST_ASSERT_EQUAL_UINT32(123U,
                             modem_config_get_web_session_counter());
    TEST_ASSERT_EQUAL_UINT32(456U,
                             modem_config_get_iec_session_counter());
}

void test_modem_config_imei_shorter_and_empty_inputs_replace_old_value(void)
{
    modem_config_set_imei("123456789012345");
    modem_config_set_imei("42");
    TEST_ASSERT_EQUAL_STRING("42", modem_config_get_imei());
    modem_config_set_imei("");
    TEST_ASSERT_EQUAL_STRING("", modem_config_get_imei());
}

/*** end of file ***/
