/*
 * test_gsm_init_scenario.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify IEC104 socket configuration in the production GSM init flow.
 */
#include "unity.h"
#include "gsm_init.h"
#include "gsm_types.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_info.h"
#include "mock_at_engine2.h"
#include "mock_modem_config.h"
#include "mock_led_driver.h"
#include "mock_gsm_elog.h"
#include "mock_gsm_listener_process.h"
#include <string.h>

gsm_t gsm;
static uint8_t reported_socket_state;

static uint8_t read_socket_state(uint8_t socket, int call_count)
{
    (void)socket;
    (void)call_count;
    return reported_socket_state;
}

void gsm_internet_connection_on_cb(void)
{
}

void gsm_request_module_restart(void)
{
    TEST_FAIL_MESSAGE("Unexpected modem restart");
}

void setUp(void)
{
    memset(&gsm, 0, sizeof(gsm));
    gsm_get_tick_IgnoreAndReturn(0U);
    reported_socket_state = SOCKET_CLOSED;
    gsm_get_socket_state_StubWithCallback(read_socket_state);
}

void tearDown(void)
{
}

void test_init_keeps_static_settings_and_defers_socket_opening(void)
{
    const uint8_t expected[] = {
        GSM_CONFIG_SOCKET_WEB_SCFG,
        GSM_CONFIG_SOCKET_WEB_SCFGEXT,
        GSM_CONFIG_SOCKET_WEB_SCFGEXT2,
        GSM_CONFIG_SOCKET_IEC104_SCFGEXT,
        GSM_CONFIG_SOCKET_IEC104_SCFGEXT2
    };
    size_t settings_count = 0U;
    gsm_load_LE910R1_init_vector();
    while (GSM_INIT_DONE != gsm_get_init_state())
    {
        const gsm_init_state_t state = gsm_get_init_state();
        TEST_ASSERT_NOT_EQUAL(GSM_OPEN_WEB_LISTENER_SOCKET, state);
        TEST_ASSERT_NOT_EQUAL(GSM_OPEN_IEC104_LISTENER_SOCKET, state);
        TEST_ASSERT_NOT_EQUAL(GSM_CONFIG_SOCKET_IEC104_SCFG, state);
        if ((settings_count < sizeof(expected) / sizeof(expected[0])) &&
            (expected[settings_count] == state))
        {
            settings_count++;
        }
        gsm_init_next_step();
    }
    TEST_ASSERT_EQUAL_UINT32(5U, settings_count);
}

void test_module_init_completion_prepares_both_listeners_and_invalidates_cache(void)
{
    for (size_t run = 0U; run < 2U; run++)
    {
        gsm.listener[GSM_LISTENER_IEC104].configured_idle_timeout = 70U;
        gsm.listener[GSM_LISTENER_IEC104].pending_idle_timeout = 70U;
        gsm.listener[GSM_LISTENER_IEC104].phase = GSM_INIT_PHASE_WAIT_RESPONSE;
        gsm.listener[GSM_LISTENER_IEC104].temp_counter = 3U;
        gsm_set_init_state(GSM_INIT_DONE);
        gsm_get_main_state_ExpectAndReturn(GSM_MODULE_INIT_MODE);
        gsm_set_tx_state_Expect(GSM_TX_READY);
        led_driver_set_modem_mode_Expect(LED_MODEM_READY);
        gsm_set_main_state_Expect(GSM_NORMAL_MODE);
        gsm_init_old();
        TEST_ASSERT_EQUAL_UINT8(GSM_LS_OPEN_SOCKET,
            gsm.listener[GSM_LISTENER_WEB].state);
        TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET,
            gsm.listener[GSM_LISTENER_IEC104].state);
        TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_SEND,
            gsm.listener[GSM_LISTENER_IEC104].phase);
        TEST_ASSERT_EQUAL_UINT16(0U,
            gsm.listener[GSM_LISTENER_IEC104].configured_idle_timeout);
        TEST_ASSERT_EQUAL_UINT16(0U,
            gsm.listener[GSM_LISTENER_IEC104].pending_idle_timeout);
        TEST_ASSERT_EQUAL_UINT8(0U,
            gsm.listener[GSM_LISTENER_IEC104].temp_counter);
    }
}

void test_static_setting_error_still_uses_existing_init_reset(void)
{
    gsm_set_init_state(GSM_CONFIG_SOCKET_WEB_SCFGEXT);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_1_CONFIG_SCFGEXT, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_ERROR);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_RESET_MODULE, gsm_get_init_state());
}

void test_busy_sim_is_polled_again_without_resetting_modem(void)
{
    gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_BUSY);
    gsm_set_delay_Expect(300U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_SIMCARD_AND_PIN, gsm_get_init_state());
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_SEND, gsm.init_phase);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
    gsm_init_old();
}

void test_enhrst_success_waits_twenty_seconds_before_common_init(void)
{
    gsm_set_init_state(GSM_REBOOT);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_ENHRST, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_delay_Expect(20000U);
    gsm_set_main_state_Expect(GSM_COMMON_INIT_MODE);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_SOFT_INIT, gsm_get_init_state());
}

void test_enhrst_timeout_allows_boot_before_another_reset(void)
{
    gsm_set_init_state(GSM_REBOOT);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_ENHRST, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_TIMEOUT);
    gsm_set_delay_Expect(20000U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_RESET_MODULE, gsm_get_init_state());
}

void test_reset_ladder_waits_for_modem_nvm_before_enhrst(void)
{
    gsm.reboot_counter = 0U;
    gsm_set_init_state(GSM_RESET_MODULE);
    led_driver_set_modem_mode_Expect(LED_MODEM_ERROR);
    gsm_set_delay_Expect(5000U);
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_REBOOT, gsm_get_init_state());
}

void test_common_init_enables_qss_notifications(void)
{
    bool found = false;
    gsm_load_common_init_vector();
    while (gsm_get_init_state() != GSM_INIT_DONE)
    {
        found = found ||
            (gsm_get_init_state() == GSM_ENABLE_SIM_STATUS_URC);
        gsm_init_next_step();
    }
    TEST_ASSERT_TRUE(found);
}

void test_puk_keeps_sim_error_led_and_waits_without_sending_pin(void)
{
    gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_SIMPUK);
    led_driver_set_modem_mode_Expect(LED_MODEM_NO_SIM);
    gsm_set_delay_Expect(3000U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_SIMCARD_AND_PIN, gsm_get_init_state());
}

void test_pin_required_respects_development_build_setting(void)
{
    gsm_set_init_state(GSM_CHECK_SIMCARD_AND_PIN);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_SIMPIN);
    gsm_set_delay_Expect((GSM_AUTO_PIN_ENABLED != 0U) ? 50U : 3000U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8((GSM_AUTO_PIN_ENABLED != 0U) ? GSM_SET_PIN_NO :
        GSM_CHECK_SIMCARD_AND_PIN, gsm_get_init_state());
}

void test_pin_success_verifies_ready_without_advancing_init_vector(void)
{
    gsm_set_init_state(GSM_SET_PIN_NO);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SET_PIN_NO, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_delay_Expect(300U);
    gsm_set_free_Expect();
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_SIMCARD_AND_PIN, gsm_get_init_state());

    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_NO_SIM);
    led_driver_set_modem_mode_Expect(LED_MODEM_NO_SIM);
    gsm_set_delay_Expect(3000U);
    gsm_set_free_Expect();
    gsm_init_old();
}

void test_pin_error_does_not_resubmit_until_card_removal(void)
{
    const uint32_t errors[] = {GSM_CPIN_PINNO_ERROR, GSM_TIMEOUT};
    for (size_t index = 0U; index < 2U; index++)
    {
        gsm_set_init_state(GSM_SET_PIN_NO);
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_SET_PIN_NO, 1U);
        gsm_init_old();
        gsm_engine_get_query_res_ExpectAndReturn(errors[index]);
        gsm_set_delay_Expect(3000U);
        gsm_set_free_Expect();
        gsm_init_old();
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
        gsm_init_old();
        gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_SIMPIN);
        gsm_set_delay_Expect(3000U);
        gsm_set_free_Expect();
        gsm_init_old();
        TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_SIMCARD_AND_PIN,
                               gsm_get_init_state());

        gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_SIMCARD_STATUS, 1U);
        gsm_init_old();
        gsm_engine_get_query_res_ExpectAndReturn(GSM_CPIN_NO_SIM);
        led_driver_set_modem_mode_Expect(LED_MODEM_NO_SIM);
        gsm_set_delay_Expect(3000U);
        gsm_set_free_Expect();
        gsm_init_old();
    }
}

void test_reenabled_vector_steps_configure_and_open_without_listener_reopen(void)
{
    static const uint8_t vector[] = {
        GSM_CONFIG_SOCKET_IEC104_SCFG,
        GSM_OPEN_WEB_LISTENER_SOCKET,
        GSM_OPEN_IEC104_LISTENER_SOCKET,
        GSM_INIT_DONE
    };
    gsm.init_vector = vector;
    gsm.init_vec_len = (uint8_t)sizeof(vector);
    gsm_reset_init_vector();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_init_old();

    gsm_engine_send_query_ExpectAndReturn(ATQUERY_OPEN_LISTENER_SOCKET, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_LISTENING);
    led_driver_set_web_mode_Expect(LED_LISTENER_LISTENING);
    gsm_set_delay_Expect(50U);
    gsm_set_tx_state_Expect(GSM_TX_READY);
    gsm_set_free_Expect();
    gsm_init_old();

    gsm_engine_send_query_ExpectAndReturn(
        ATQUERY_OPEN_IEC104_LISTENER_SOCKET, 1U);
    gsm_init_old();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_socket_state_Expect(IEC104_LISTENER_SOCKET, SOCKET_LISTENING);
    led_driver_set_iec104_mode_Expect(LED_LISTENER_LISTENING);
    gsm_set_delay_Expect(50U);
    gsm_set_tx_state_Expect(GSM_TX_READY);
    gsm_set_free_Expect();
    gsm_init_old();

    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_DONE, gsm_get_init_state());
    reported_socket_state = SOCKET_LISTENING;
    gsm_get_main_state_ExpectAndReturn(GSM_MODULE_INIT_MODE);
    gsm_set_tx_state_Expect(GSM_TX_READY);
    led_driver_set_modem_mode_Expect(LED_MODEM_READY);
    gsm_set_main_state_Expect(GSM_NORMAL_MODE);
    gsm_init_old();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_IDLE,
        gsm.listener[GSM_LISTENER_WEB].state);
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_IDLE,
        gsm.listener[GSM_LISTENER_IEC104].state);
}

void test_registration_errors_leave_wait_phase_and_query_ceer(void)
{
    const gsm_init_state_t states[] = {
        GSM_CHECK_GPRS_NEWTWORK_STATE, GSM_CHECK_LTE_NEWTWORK_STATE};
    const uint32_t queries[] = {
        ATQUERY_GET_GPRS_NETWORK_STATE, ATQUERY_GET_LTE_NETWORK_STATE};
    for (size_t index = 0U; index < 2U; index++)
    {
        gsm_set_init_state(states[index]);
        gsm_engine_send_query_ExpectAndReturn(queries[index], 1U);
        gsm_init_old();
        TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, gsm.init_phase);
        gsm_engine_get_query_res_ExpectAndReturn(GSM_ERROR);
        gsm_set_free_Expect();
        gsm_init_old();
        TEST_ASSERT_EQUAL(GSM_GET_CEER, gsm_get_init_state());
        TEST_ASSERT_EQUAL(GSM_INIT_PHASE_SEND, gsm.init_phase);
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_CEER, 1U);
        gsm_init_old();
        TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, gsm.init_phase);
    }
}

void test_registration_search_keeps_polling_both_networks(void)
{
    gsm_set_init_state(GSM_CHECK_GPRS_NEWTWORK_STATE);
    for (size_t cycle = 0U; cycle < 3U; cycle++)
    {
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_GPRS_NETWORK_STATE, 1U);
        gsm_init_old();
        gsm_engine_get_query_res_ExpectAndReturn(GSM_GPRS_NETWORK_SEARCHING);
        gsm_set_free_Expect();
        gsm_init_old();
        TEST_ASSERT_EQUAL(GSM_CHECK_LTE_NEWTWORK_STATE, gsm_get_init_state());
        TEST_ASSERT_EQUAL(GSM_INIT_PHASE_SEND, gsm.init_phase);
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_LTE_NETWORK_STATE, 1U);
        gsm_init_old();
        gsm_engine_get_query_res_ExpectAndReturn(GSM_LTE_NETWORK_SEARCHING);
        gsm_set_free_Expect();
        gsm_init_old();
        TEST_ASSERT_EQUAL(GSM_CHECK_GPRS_NEWTWORK_STATE, gsm_get_init_state());
        TEST_ASSERT_EQUAL(GSM_INIT_PHASE_SEND, gsm.init_phase);
    }
}

/*** end of file ***/
