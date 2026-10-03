/*
 * test_json_config_bounds.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Regression tests for JSON-to-persistent configuration boundaries.
 */
#include "unity.h"
#include "json_config.h"
#include "mock_iec104_config.h"
#include "mock_modbus_config.h"
#include "mock_modbus_process.h"
#include "mock_modem_config.h"
#include "mock_rf_config.h"
#include <string.h>
#include <stdio.h>

TEST_SOURCE_FILE("iec104_util.c")

static jiec_config_t iec;
static iec104_config_t saved_iec;
static modbus_configs_t saved_modbus;
static modbus_line_config_t saved_modbus_line;

static void capture_iec(const iec104_config_t *config, int call_count)
{
    (void)call_count;
    saved_iec = *config;
}

static void capture_modbus(const modbus_configs_t *config, int call_count)
{
    (void)call_count;
    saved_modbus = *config;
}

static bool capture_modbus_line(uint32_t line_index,
                               const modbus_line_config_t *config,
                               int call_count)
{
    (void)call_count;
    if (0U == line_index)
    {
        saved_modbus_line = *config;
    }
    return true;
}

void setUp(void)
{
    memset(&iec, 0, sizeof(iec));
    memset(&saved_iec, 0, sizeof(saved_iec));
    memset(&saved_modbus, 0, sizeof(saved_modbus));
    memset(&saved_modbus_line, 0, sizeof(saved_modbus_line));
    (void)strcpy(iec.scada_ip_address, "192.168.1.2");
    iec.scada_port = 2404U;
    iec.periodical_send_interval = 60U;
    iec.t0_timeout = 30U;
    iec.t1_timeout = 15U;
    iec.t2_timeout = 10U;
    iec.t3_timeout = 20U;
    iec.k_max = 12U;
    iec.w_max = 8U;
    iec.common_address = 1U;
}

void tearDown(void)
{
}

void test_scada_ip_high_first_octet_is_saved(void)
{
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_HEX32(0xC0A80102U, saved_iec.scada_ip_address);
}

void test_iec_common_address_supports_existing_16_bit_range(void)
{
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{\"CommonAddr\":300}", &iec));
    TEST_ASSERT_EQUAL_UINT16(300U, iec.common_address);
    TEST_ASSERT_EQUAL_INT(1,
                          parse_iec_config("{\"CommonAddr\":65535}", &iec));
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, iec.common_address);
}

void test_iec_originator_rejects_8_bit_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(1,
                          parse_iec_config("{\"OriginatorAddr\":255}", &iec));
    TEST_ASSERT_EQUAL_UINT8(UINT8_MAX, iec.originator_address);
    TEST_ASSERT_EQUAL_INT(0,
                          parse_iec_config("{\"OriginatorAddr\":256}", &iec));
}

void test_inactive_sbo_rejects_storage_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(0,
        parse_iec_config("{\"SBO\":false,\"SBOTimeout\":65536}", &iec));
}

void test_modbus_line_address_rejects_storage_overflow(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 1U;
    config.baud_rate = 9600U;

    TEST_ASSERT_EQUAL_INT(0,
        parse_modbus_config(
            "{\"Hat\":{\"ADDR_R_ArizaAkimi\":[65536]}}", &config));
}

void test_modbus_setter_rejects_overflow_before_writing(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 1U;
    config.baud_rate = 9600U;
    config.addr_modem_reset = 65536U;

    /* No mock write is expected on invalid input. */
    TEST_ASSERT_EQUAL_INT(-1, set_modbus_config(&config));
}

void test_partial_iec_update_preserves_wide_address_in_storage(void)
{
    iec.common_address = 300U;
    iec.originator_address = 255U;
    TEST_ASSERT_EQUAL_INT(1, parse_iec_config("{\"Port\":2405}", &iec));
    iec104_config_get_IgnoreAndReturn(NULL);
    iec104_config_set_Stub(capture_iec);
    iec104_set_line_config_IgnoreAndReturn(true);
    iec104_config_sync_ExpectAndReturn(0);
    TEST_ASSERT_EQUAL_INT(0, set_iec_config(&iec));
    TEST_ASSERT_EQUAL_UINT16(300U, saved_iec.common_address);
    TEST_ASSERT_EQUAL_UINT8(255U, saved_iec.originator_address);
    TEST_ASSERT_EQUAL_UINT16(2405U, saved_iec.scada_port);
}

void test_common_address_rejects_zero_and_16_bit_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(0, parse_iec_config("{\"CommonAddr\":0}", &iec));
    TEST_ASSERT_EQUAL_INT(0,
                          parse_iec_config("{\"CommonAddr\":65536}", &iec));
}

void test_sbo_setter_rejects_overflow_before_writing(void)
{
    iec.sbo_timeout = 65536U;
    TEST_ASSERT_EQUAL_INT(-1, set_iec_config(&iec));
}

void test_sbo_keeps_existing_active_timeout_limits(void)
{
    TEST_ASSERT_EQUAL_INT(1,
        parse_iec_config("{\"SBO\":false,\"SBOTimeout\":65535}", &iec));
    TEST_ASSERT_EQUAL_INT(1,
        parse_iec_config("{\"SBO\":true,\"SBOTimeout\":300}", &iec));
    TEST_ASSERT_EQUAL_INT(0,
        parse_iec_config("{\"SBO\":true,\"SBOTimeout\":301}", &iec));
}

void test_all_modbus_line_address_fields_check_16_bit_boundary(void)
{
    const char *keys[] =
    {
        "ADDR_R_ArizaAkimi",
        "ADDR_S_ArizaAkimi",
        "ADDR_T_ArizaAkimi",
        "ADDR_R_ArizaSuresi",
        "ADDR_S_ArizaSuresi",
        "ADDR_T_ArizaSuresi",
        "ADDR_R_ArizaTuru",
        "ADDR_S_ArizaTuru",
        "ADDR_T_ArizaTuru",
        "ADDR_R_AnlikAkim",
        "ADDR_S_AnlikAkim",
        "ADDR_T_AnlikAkim",
        "ADDR_R_EnerjiVarYok",
        "ADDR_S_EnerjiVarYok",
        "ADDR_T_EnerjiVarYok",
        "ADDR_R_NominalAkimVarYok",
        "ADDR_S_NominalAkimVarYok",
        "ADDR_T_NominalAkimVarYok",
        "ADDR_R_RfhabVarYok",
        "ADDR_S_RfhabVarYok",
        "ADDR_T_RfhabVarYok"
    };
    char json[96];

    for (size_t field = 0U; field < (sizeof(keys) / sizeof(keys[0])); field++)
    {
        jmodbus_configs_t config = {0};
        config.device_addr = 1U;
        config.baud_rate = 9600U;
        (void)snprintf(json, sizeof(json),
                       "{\"Hat\":{\"%s\":[65535]}}", keys[field]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, parse_modbus_config(json, &config),
                                      keys[field]);
        (void)snprintf(json, sizeof(json),
                       "{\"Hat\":{\"%s\":[65536]}}", keys[field]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse_modbus_config(json, &config),
                                      keys[field]);
    }
}

void test_modbus_setter_rejects_last_slot_before_writing(void)
{
    jmodbus_configs_t config = {0};
    config.addr_aku_uyarisi = 65536U;
    TEST_ASSERT_EQUAL_INT(-1, set_modbus_config(&config));
    config.addr_aku_uyarisi = 0U;
    config.line.addr_t_rfhab_varyok[MAX_ARRAYS - 1U] = 65536U;
    TEST_ASSERT_EQUAL_INT(-1, set_modbus_config(&config));
}

void test_modbus_valid_boundary_values_reach_storage_unchanged(void)
{
    jmodbus_configs_t config = {0};
    config.device_addr = 1U;
    config.baud_rate = 9600U;
    config.addr_aku_uyarisi = 65535U;
    config.addr_modem_reset = 0U;
    config.line.in_use[0] = true;
    config.line.addr_r_ariza_akimi[0] = 65535U;
    config.line.addr_s_ariza_akimi[0] = 65534U;
    config.line.addr_t_ariza_akimi[0] = 65533U;
    config.line.addr_t_rfhab_varyok[0] = 65535U;
    modbus_config_get_IgnoreAndReturn(NULL);
    modbus_config_set_Stub(capture_modbus);
    modbus_set_line_config_Stub(capture_modbus_line);
    modbus_config_sync_ExpectAndReturn(0);
    modbus_process_notify_config_changed_Expect();

    TEST_ASSERT_EQUAL_INT(0, set_modbus_config(&config));
    TEST_ASSERT_EQUAL_UINT16(65535U, saved_modbus.addr_aku_uyarisi);
    TEST_ASSERT_EQUAL_UINT16(0U, saved_modbus.addr_modem_reset);
    TEST_ASSERT_EQUAL_UINT16(65535U, saved_modbus_line.ariza_akimi[PHASE_L1]);
    TEST_ASSERT_EQUAL_UINT16(65534U, saved_modbus_line.ariza_akimi[PHASE_L2]);
    TEST_ASSERT_EQUAL_UINT16(65533U, saved_modbus_line.ariza_akimi[PHASE_L3]);
    TEST_ASSERT_EQUAL_UINT16(65535U,
                            saved_modbus_line.rf_haberlesme_varyok[PHASE_L3]);
}

/*** end of file ***/
