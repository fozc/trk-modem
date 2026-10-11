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

/*** end of file ***/
