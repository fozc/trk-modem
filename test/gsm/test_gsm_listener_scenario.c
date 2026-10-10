/*
 * test_gsm_listener_scenario.c
 *
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Verify IEC104 modem configuration precedes listener opening.
 */
#include "unity.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_socket.h"
#include "mock_bsp.h"
#include "mock_modem_config.h"
#include "mock_iec104_process.h"
#include "mock_web_server.h"
#include "mock_led_driver.h"
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#include "../../Application/gsm/gsm_listener_process.c"
#pragma GCC diagnostic pop

gsm_t gsm;
static uint16_t active_t3;

uint16_t iec104_get_t3_timeout(void)
{
    return active_t3;
}

void setUp(void)
{
    active_t3 = 60U;
    memset(&gsm, 0, sizeof(gsm));
    gsm_listener_get_no_carrier_IgnoreAndReturn(0U);
    gsm_get_tick_IgnoreAndReturn(0U);
    gsm_get_main_state_IgnoreAndReturn(GSM_NORMAL_MODE);
    gsm_set_delay_Ignore();
    gsm_set_free_Ignore();
}

void tearDown(void)
{
}

void test_iec104_reopen_applies_pending_config_then_scfg_before_open(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_CHECK_NET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET, ctx->state);
    iec104_process_apply_pending_config_Expect();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 1U);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
    gsm_engine_get_query_res_ExpectAndReturn(0U);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET, ctx->state);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_OPEN_SOCKET, ctx->state);
    gsm_socket_record_attempt_Expect(IEC104_LISTENER_SOCKET);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_OPEN_IEC104_LISTENER_SOCKET,
        1U);
    gsm_listener_iec104_process();
}

void test_failed_modem_configuration_does_not_open_listener(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_CONFIG_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_ERROR);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CHECK_NET, ctx->state);
}

void test_web_reopen_keeps_existing_path_without_iec104_configuration(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CHECK_NET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_OPEN_SOCKET, ctx->state);
}

void test_unchanged_idle_timeout_is_not_rewritten_on_reconnect(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_CONFIG_SOCKET;
    ctx->phase = GSM_INIT_PHASE_SEND;
    ctx->configured_idle_timeout = 70U;
    iec104_process_apply_pending_config_Expect();
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_OPEN_SOCKET, ctx->state);
    active_t3 = 120U;
    ctx->state = GSM_LS_CONFIG_SOCKET;
    iec104_process_apply_pending_config_Expect();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 1U);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
}

void test_data_mode_carrier_loss_notifies_close_before_configuring_reopen(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_DATA_MODE;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_listener_is_close_socket_requested_ExpectAndReturn(
        GSM_LISTENER_IEC104, 0U);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_NO_CARRIER);
    gsm_socket_record_disconnect_Expect(IEC104_LISTENER_SOCKET,
        SOCK_DISC_NO_CARRIER);
    gsm_set_socket_state_Expect(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
    iec104_process_socket_closed_cb_Expect();
    gsm_reset_iec104_session_info_Expect();
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET, ctx->state);
}

/*** end of file ***/
