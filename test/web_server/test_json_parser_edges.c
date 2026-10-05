/*
 * test_json_parser_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Parser-mechanics edge cases for the JSON config ingestion path:
 * unknown-key forward compatibility, array capacity drain, integer and
 * string boundaries, read-only key skipping and malformed input
 * rejection. Field-range validation lives in test_json_config_bounds.c.
 */
#include "unity.h"
#include "json_config.h"
#include "modem_types.h"
#include "iec104_util.h"
#include "mock_iec104_config.h"
#include "mock_modbus_config.h"
#include "mock_modbus_process.h"
#include "mock_modem_config.h"
#include "mock_rf_config.h"
#include <string.h>
#include <stdio.h>

TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("xprintf.c")

static modem_config_t device;
static jiec_config_t iec;
static jmodbus_configs_t modbus;
static modem_config_t seed;

void setUp(void)
{
    memset(&device, 0, sizeof(device));
    memset(&iec, 0, sizeof(iec));
    memset(&modbus, 0, sizeof(modbus));
    /* Seed values that satisfy the validators when a parse succeeds
     * (IEC validates timeouts, windows and addresses on every parse). */
    iec.scada_port = 2404U;
    iec.periodical_send_interval = 60U;
    iec.t0_timeout = 30U;
    iec.t1_timeout = 15U;
    iec.t2_timeout = 10U;
    iec.t3_timeout = 20U;
    iec.k_max = 12U;
    iec.w_max = 8U;
    iec.common_address = 1U;
    iec.ioa_aku_uyarisi = 10000U;
    iec.ioa_modem_reset = 10001U;
    modbus.device_addr = 1U;
    modbus.baud_rate = 9600U;
    modem_config_get_IgnoreAndReturn(NULL);
    iec104_get_line_config_IgnoreAndReturn(NULL);
}

void tearDown(void)
{
}

void test_null_and_empty_documents_are_rejected_and_empty_object_is_valid(void)
{
    TEST_ASSERT_EQUAL_INT(0, parse_device_config(NULL, &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("", &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{}", NULL));
    TEST_ASSERT_EQUAL_INT(1, parse_device_config("{}", &device));
}

void test_unknown_keys_with_every_json_value_type_are_skipped(void)
{
    /* Forward compatibility: keys added by a newer web UI must not break
     * this parser, whatever JSON value type they carry. */
    static const char json[] =
        "{"
        "\"TimeZone\":3,"
        "\"NewString\":\"a\\\"b[]{}:,\","
        "\"NewInt\":-42,"
        "\"NewFloat\":-1.5,"
        "\"NewTrue\":true,"
        "\"NewFalse\":false,"
        "\"NewNull\":null,"
        "\"NewArray\":[1,-2.5,\"three\",true,null,[4,5],{\"k\":6}],"
        "\"NewObject\":{\"s\":\"}]{[\",\"a\":[7,8],\"o\":{\"x\":9}},"
        "\"WebArayuzuPortu\":8081"
        "}";

    TEST_ASSERT_EQUAL_INT(1, parse_device_config(json, &device));
    TEST_ASSERT_EQUAL_INT32(3, device.time_zone);
    TEST_ASSERT_EQUAL_UINT16(8081U, device.web_interface_port);
}

void test_surplus_array_elements_are_drained_and_next_key_still_parses(void)
{
    /* Nine entries for a seven-slot array: the surplus must be drained so
     * the cursor re-syncs and the key after the nested object still
     * applies (a mid-array cursor would desync every later key). */
    static const char json[] =
        "{\"Hatlar\":{\"TemporaryFaultBase\":"
        "[100000,100001,100002,100003,100004,100005,100006,100007,100008]},"
        "\"T0\":30}";

    TEST_ASSERT_EQUAL_INT(1, parse_iec_config(json, &iec));
    TEST_ASSERT_EQUAL_UINT32(100006U,
                             iec.line.temporary_fault_base[MAX_ARRAYS - 1U]);
    TEST_ASSERT_EQUAL_UINT8(30U, iec.t0_timeout);
}

void test_uint32_time_field_accepts_maximum_and_rejects_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(1, parse_device_config("{\"Time\":4294967295}",
                                                 &device));
    TEST_ASSERT_EQUAL_UINT32(4294967295U, device.time);
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"Time\":4294967296}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"Time\":-1}", &device));
}

void test_timezone_range_boundaries_are_enforced(void)
{
    TEST_ASSERT_EQUAL_INT(1, parse_device_config("{\"TimeZone\":-12}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT32(-12, device.time_zone);
    TEST_ASSERT_EQUAL_INT(1, parse_device_config("{\"TimeZone\":14}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT32(14, device.time_zone);
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\":-13}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\":15}",
                                                 &device));
    /* Positive INT32 overflow is caught by the signed parser itself. */
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\":2147483648}",
                                                 &device));
}

void test_long_strings_are_truncated_and_parsing_continues(void)
{
    static const char apn41[] =
        "abcdefghijklmnopqrstuvwxyz0147852"; /* 35 chars, MAX_APN_LEN 16 */
    char json[128];

    (void)snprintf(json, sizeof(json),
                   "{\"SimKartAPN\":\"%s\",\"TimeZone\":7}", apn41);
    TEST_ASSERT_EQUAL_INT(1, parse_device_config(json, &device));
    TEST_ASSERT_EQUAL_UINT(MAX_APN_LEN - 1U, strlen(device.apn.apn));
    TEST_ASSERT_EQUAL_INT32(7, device.time_zone);
    /* A later string field still lands whole: NTP limit is wider. */
    TEST_ASSERT_EQUAL_INT(1,
        parse_device_config("{\"NtpServer\":\"pool.ntp.org\"}", &device));
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", device.ntp_server);
}

void test_readonly_keys_are_skipped_without_failing_the_parse(void)
{
    /* The web UI posts the whole document back; read-only members must
     * be ignored regardless of their value type. */
    static const char json[] =
        "{"
        "\"SeriNumarasi\":\"SN-1\","
        "\"UretimTarihi\":20260101,"
        "\"LifeTime\":12345,"
        "\"RunTime\":678,"
        "\"ModemYazilimVeriyonu\":\"v1\","
        "\"RFYazilimVeriyonu\":2,"
        "\"KurulumTarihi\":{\"y\":2026},"
        "\"CihazKoordinati\":[1.5,2.5],"
        "\"TimeZone\":5"
        "}";

    TEST_ASSERT_EQUAL_INT(1, parse_device_config(json, &device));
    TEST_ASSERT_EQUAL_INT32(5, device.time_zone);
}

void test_duplicate_key_last_write_wins(void)
{
    TEST_ASSERT_EQUAL_INT(1,
        parse_device_config("{\"TimeZone\":3,\"TimeZone\":5}", &device));
    TEST_ASSERT_EQUAL_INT32(5, device.time_zone);
}

void test_whitespace_is_tolerated_everywhere(void)
{
    TEST_ASSERT_EQUAL_INT(1,
        parse_device_config("  {  \"TimeZone\"  :  6  }  ", &device));
    TEST_ASSERT_EQUAL_INT32(6, device.time_zone);
}

void test_content_after_closing_brace_is_currently_ignored(void)
{
    /* Pin today's lenient behavior: the parsers stop at the closing
     * brace of the top-level object and never look at trailing bytes. */
    TEST_ASSERT_EQUAL_INT(1,
        parse_device_config("{\"TimeZone\":5}XYZ", &device));
    TEST_ASSERT_EQUAL_INT32(5, device.time_zone);
}

void test_malformed_documents_are_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        parse_device_config("{\"SimKartAPN\":\"unterminated", &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\" 5}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\":5", &device));
    /* Known key with a value of the wrong type. */
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"TimeZone\":\"5\"}",
                                                 &device));
    /* Unterminated array inside a known nested object. */
    TEST_ASSERT_EQUAL_INT(0,
        parse_modbus_config("{\"Hat\":{\"inUse\":[true}}", &modbus));
    /* Unterminated object as an unknown key's value. */
    TEST_ASSERT_EQUAL_INT(0,
        parse_device_config("{\"New\":{\"a\":1,\"TimeZone\":5}", &device));
}

void test_integer_fields_reject_fraction_and_exponent_notation(void)
{
    /* The hand-rolled numeric parser accepts plain decimal digits only;
     * document that contract (web UI posts integers as plain digits). */
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"Time\":1e3}", &device));
    TEST_ASSERT_EQUAL_INT(0, parse_device_config("{\"Time\":1.5}", &device));
}

static const modem_config_t *seeded_base(int call_count)
{
    (void)call_count;
    return &seed;
}

void test_partial_update_starts_from_current_persistent_config(void)
{
    memset(&seed, 0, sizeof(seed));
    seed.web_interface_port = 8080U;
    (void)strcpy(seed.apn.apn, "base");
    modem_config_get_StopIgnore();
    modem_config_get_Stub(seeded_base);

    TEST_ASSERT_EQUAL_INT(1, parse_device_config("{\"TimeZone\":5}",
                                                 &device));
    TEST_ASSERT_EQUAL_INT32(5, device.time_zone);
    TEST_ASSERT_EQUAL_UINT16(8080U, device.web_interface_port);
    TEST_ASSERT_EQUAL_STRING("base", device.apn.apn);
}

void test_modbus_bool_array_literals_and_unknown_nested_key(void)
{
    static const char json[] =
        "{\"Hat\":{\"YeniAlan\":{\"nested\":[1,2]},"
        "\"inUse\":[true,false]}}";

    TEST_ASSERT_EQUAL_INT(1, parse_modbus_config(json, &modbus));
    TEST_ASSERT_TRUE(modbus.line.in_use[0]);
    TEST_ASSERT_FALSE(modbus.line.in_use[1]);
}

/*** end of file ***/
