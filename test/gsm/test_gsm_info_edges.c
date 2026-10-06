/*
 * test_gsm_info_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * Runtime storage and telemetry boundary contracts, no mocked algorithm.
 */
#include "unity.h"
#include "gsm_info.h"
#include <string.h>

void setUp(void)
{
    gsm_info_init();
}
void tearDown(void)
{
}
void test_gsm_info_init_and_reinit_clear_stale_runtime_values(void)
{
    gsm_info_set_imei("123");
    gsm_info_set_ip(UINT32_MAX);
    gsm_info_add_rx_bytes(100U);
    gsm_info_set_signal_quality_4G(61U);
    gsm_info_init();
    uint32_t tx = UINT32_MAX;
    uint32_t rx = UINT32_MAX;
    gsm_info_get_rxtx_counters(&tx, &rx);
    TEST_ASSERT_EQUAL_UINT32(0U, tx);
    TEST_ASSERT_EQUAL_UINT32(0U, rx);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info_get_ip());
    TEST_ASSERT_EQUAL_STRING("", gsm_info_get_imei());
    TEST_ASSERT_EQUAL_UINT8(99U, gsm_info_get_signal_quality());
    TEST_ASSERT_EQUAL(GSM_SIM_STATE_UNKNOWN, gsm_info_get_sim_state());
    TEST_ASSERT_EQUAL(GSM_NET_REG_NOT_REGISTERED, gsm_info_get_creg());
    TEST_ASSERT_EQUAL(GSM_MODULE_UNDEFINED, gsm_info_get_module_model());
    TEST_ASSERT_EQUAL(NETWORK_GEN_UNKNOWN, get_network_generation());
}
void test_gsm_info_strings_copy_truncate_terminate_and_preserve_on_null(void)
{
    static const struct
    {
        void (*set)(const char *);
        const char *(*get)(void);
        size_t capacity;
    } cases[] = {
        {gsm_info_set_imei, gsm_info_get_imei, 15U},
        {gsm_info_set_iccid, gsm_info_get_iccid, 20U},
        {gsm_info_set_imsi, gsm_info_get_imsi, 15U},
        {gsm_info_set_fw_version, gsm_info_get_fw_version, 15U}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        char input[64];
        memset(input, '7', sizeof(input) - 1U);
        input[sizeof(input) - 1U] = '\0';
        cases[i].set(input);
        memset(input, '9', sizeof(input) - 1U);
        cases[i].set(NULL);
        const char *stored = cases[i].get();
        TEST_ASSERT_EQUAL_size_t(cases[i].capacity, strlen(stored));
        for (size_t j = 0U; j < cases[i].capacity; j++)
        {
            TEST_ASSERT_EQUAL_CHAR('7', stored[j]);
        }
        cases[i].set("A");
        TEST_ASSERT_EQUAL_STRING("A", cases[i].get());
        cases[i].set("");
        TEST_ASSERT_EQUAL_STRING("", cases[i].get());
    }
}
void test_gsm_info_cell_copy_null_and_output_independence(void)
{
    gsm_info_cell_t input = {UINT16_MAX, 0U, 0xABCDU, 1U};
    gsm_info_cell_t output = {0};
    const gsm_info_cell_t expected = input;
    gsm_info_set_cell(&input);
    memset(&input, 0, sizeof(input));
    gsm_info_set_cell(NULL);
    gsm_info_get_cell(NULL);
    gsm_info_get_cell(&output);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &output, sizeof(output));
    memset(&output, 0, sizeof(output));
    gsm_info_get_cell(&output);
    TEST_ASSERT_EQUAL_UINT16(0xABCDU, output.lac);
}
void test_gsm_info_counters_wrap_and_null_outputs_do_not_reset_other_counter(void)
{
    gsm_info_add_tx_bytes(UINT32_MAX);
    gsm_info_add_rx_bytes(UINT32_MAX - 1U);
    gsm_info_add_tx_bytes(2U);
    gsm_info_add_rx_bytes(3U);
    uint32_t tx;
    uint32_t rx;
    gsm_info_get_rxtx_counters(NULL, NULL);
    gsm_info_get_rxtx_counters(&tx, NULL);
    gsm_info_get_rxtx_counters(NULL, &rx);
    TEST_ASSERT_EQUAL_UINT32(1U, tx);
    TEST_ASSERT_EQUAL_UINT32(1U, rx);
}
void test_gsm_info_network_generations_cover_all_rat_values_and_unknowns(void)
{
    static const network_generation_t expected[] = {
        NETWORK_GEN_2G, NETWORK_GEN_2G, NETWORK_GEN_3G, NETWORK_GEN_2G,
        NETWORK_GEN_3G, NETWORK_GEN_3G, NETWORK_GEN_3G, NETWORK_GEN_4G,
        NETWORK_GEN_3G, NETWORK_GEN_4G
    };
    for (uint8_t rat = 0U; rat < sizeof(expected) / sizeof(expected[0]); rat++)
    {
        gsm_info_set_access_technology(rat);
        TEST_ASSERT_EQUAL(expected[rat], get_network_generation());
        TEST_ASSERT_NOT_EQUAL(0,
            strcmp("UNK", get_access_tech_str((gsm_access_technology_t)rat)));
    }
    gsm_info_set_access_technology(10U);
    TEST_ASSERT_EQUAL(NETWORK_GEN_UNKNOWN, get_network_generation());
    gsm_info_set_access_technology(UINT8_MAX);
    TEST_ASSERT_EQUAL(NETWORK_GEN_UNKNOWN, get_network_generation());
    TEST_ASSERT_EQUAL_STRING("UNK",
        get_access_tech_str(GSM_ACCESS_TECH_UNDEFINED));
}
void test_gsm_info_registration_sim_and_model_fields_are_independent(void)
{
    gsm_info_set_creg(GSM_NET_REG_DENIED);
    gsm_info_set_cgreg(GSM_NET_REG_SEARCHING);
    gsm_info_set_cereg(GSM_NET_REG_ROAMING);
    gsm_info_set_sim_state(GSM_SIM_STATE_PIN_REQUIRED);
    gsm_info_set_module_model(GSM_MODULE_LE910R1);
    gsm_info_set_gprs_state(1U);
    gsm_info_set_ip(UINT32_MAX);
    TEST_ASSERT_EQUAL(GSM_NET_REG_DENIED, gsm_info_get_creg());
    TEST_ASSERT_EQUAL(GSM_NET_REG_SEARCHING, gsm_info_get_cgreg());
    TEST_ASSERT_EQUAL(GSM_NET_REG_ROAMING, gsm_info_get_cereg());
    TEST_ASSERT_EQUAL(GSM_SIM_STATE_PIN_REQUIRED, gsm_info_get_sim_state());
    TEST_ASSERT_EQUAL(GSM_MODULE_LE910R1, gsm_info_get_module_model());
    TEST_ASSERT_EQUAL_UINT8(1U, gsm_info_get_gprs_state());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, gsm_info_get_ip());
}
void test_gsm_cesq_unknown_fields_reset_previous_report_and_accept_null_output(void)
{
    gsm_cesq_report_t report;
    memset(&report, 0xA5, sizeof(report));
    gsm_info_get_cesq_report(NULL);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("None", report.active_technology);
    TEST_ASSERT_FALSE(report.rxlev.available);
    TEST_ASSERT_FALSE(report.ber.available);
    TEST_ASSERT_FALSE(report.rscp.available);
    TEST_ASSERT_FALSE(report.ecno.available);
    TEST_ASSERT_FALSE(report.rsrq.available);
    TEST_ASSERT_FALSE(report.rsrp.available);
    TEST_ASSERT_EQUAL_STRING("N/A", report.rsrp.label);
    TEST_ASSERT_EQUAL_INT16(0, report.rsrp.physical);
    TEST_ASSERT_EQUAL_UINT8(255U, report.rsrp.raw);
}
void test_gsm_cesq_valid_minimum_and_maximum_physical_values(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_signal_quality_2G(0U);
    gsm_info_set_2G_ber(0U);
    gsm_info_set_signal_quality_3G(0U);
    gsm_info_set_3G_ecno(0U);
    gsm_info_set_4G_rsrq(0U);
    gsm_info_set_signal_quality_4G(0U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_INT16(-1110, report.rxlev.physical);
    TEST_ASSERT_EQUAL_INT16(-1210, report.rscp.physical);
    TEST_ASSERT_EQUAL_INT16(-1410, report.rsrp.physical);
    TEST_ASSERT_EQUAL_INT16(-245, report.ecno.physical);
    TEST_ASSERT_EQUAL_INT16(-200, report.rsrq.physical);
    TEST_ASSERT_EQUAL(GSM_CESQ_UNIT_DBM, report.rsrp.unit);
    TEST_ASSERT_EQUAL(GSM_CESQ_UNIT_DB, report.rsrq.unit);
    gsm_info_set_signal_quality_2G(63U);
    gsm_info_set_2G_ber(7U);
    gsm_info_set_signal_quality_3G(96U);
    gsm_info_set_3G_ecno(49U);
    gsm_info_set_4G_rsrq(34U);
    gsm_info_set_signal_quality_4G(97U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_INT16(-480, report.rxlev.physical);
    TEST_ASSERT_EQUAL_INT16(-250, report.rscp.physical);
    TEST_ASSERT_EQUAL_INT16(-440, report.rsrp.physical);
    TEST_ASSERT_EQUAL_INT16(0, report.ecno.physical);
    TEST_ASSERT_EQUAL_INT16(-30, report.rsrq.physical);
    TEST_ASSERT_EQUAL_STRING("Very Weak", report.ber.label);
}
void test_gsm_cesq_power_labels_change_at_exact_rsrp_thresholds(void)
{
    static const uint8_t raw[] = {30U, 31U, 40U, 41U, 50U, 51U, 60U, 61U};
    static const char *const labels[] = {
        "Very Weak", "Weak", "Weak", "Mid",
        "Mid", "Strong", "Strong", "Excellent"
    };
    for (size_t i = 0U; i < sizeof(raw); i++)
    {
        gsm_cesq_report_t report;
        gsm_info_set_signal_quality_4G(raw[i]);
        gsm_info_get_cesq_report(&report);
        TEST_ASSERT_EQUAL_STRING(labels[i], report.rsrp.label);
    }
}
void test_gsm_cesq_active_technology_falls_back_when_power_field_unknown(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_signal_quality_2G(10U);
    gsm_info_set_signal_quality_3G(20U);
    gsm_info_set_signal_quality_4G(30U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("4G / LTE", report.active_technology);
    gsm_info_set_signal_quality_4G(255U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("3G / WCDMA", report.active_technology);
    gsm_info_set_signal_quality_3G(255U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("2G / GSM", report.active_technology);
    gsm_info_set_signal_quality_2G(99U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("None", report.active_technology);
}

/* Values outside the documented raw ranges must not become valid telemetry. */
void test_gsm_cesq_reserved_rxlev_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_signal_quality_2G(64U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.rxlev.available);
}
void test_gsm_cesq_reserved_ber_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_2G_ber(8U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.ber.available);
}
void test_gsm_cesq_reserved_rscp_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_signal_quality_3G(97U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.rscp.available);
}
void test_gsm_cesq_reserved_ecno_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_3G_ecno(50U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.ecno.available);
}
void test_gsm_cesq_reserved_rsrq_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_4G_rsrq(35U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.rsrq.available);
}
void test_gsm_cesq_reserved_rsrp_is_unavailable(void)
{
    gsm_cesq_report_t report;
    gsm_info_set_signal_quality_4G(98U);
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.rsrp.available);
}

void test_gsm_cesq_full_byte_range_marks_only_documented_values_available(void)
{
    gsm_cesq_report_t report;

    for (uint16_t value = 0U; value <= 255U; value++)
    {
        const uint8_t raw = (uint8_t)value;

        gsm_info_set_signal_quality_2G(raw);
        gsm_info_set_2G_ber(raw);
        gsm_info_set_signal_quality_3G(raw);
        gsm_info_set_3G_ecno(raw);
        gsm_info_set_4G_rsrq(raw);
        gsm_info_set_signal_quality_4G(raw);
        gsm_info_get_cesq_report(&report);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_RXLEV_MAX,
                                report.rxlev.available);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_BER_MAX,
                                report.ber.available);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_RSCP_MAX,
                                report.rscp.available);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_ECNO_MAX,
                                report.ecno.available);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_RSRQ_MAX,
                                report.rsrq.available);
        TEST_ASSERT_EQUAL_UINT8(raw <= GSM_CESQ_RSRP_MAX,
                                report.rsrp.available);
    }
}

void test_gsm_cesq_reserved_lte_does_not_override_valid_3g(void)
{
    gsm_cesq_report_t report;

    gsm_info_set_signal_quality_3G(40U);   /* Gecerli 3G olcumu. */
    gsm_info_set_signal_quality_4G(98U);   /* Ayrimis LTE gucu. */
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_FALSE(report.rsrp.available);
    TEST_ASSERT_TRUE(report.rscp.available);
    TEST_ASSERT_EQUAL_STRING("3G / WCDMA", report.active_technology);

    gsm_info_set_signal_quality_4G(50U);   /* Gecerli LTE onceligi alir. */
    gsm_info_get_cesq_report(&report);
    TEST_ASSERT_EQUAL_STRING("4G / LTE", report.active_technology);
}
/*** end of file ***/
