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
static uint8_t carrier_lost;
static uint32_t tx_direction;

static uint8_t get_carrier_lost(gsm_listener_id_t id, int count)
{
    (void)id;
    (void)count;
    return carrier_lost;
}

static uint32_t get_tx_direction(int count)
{
    (void)count;
    return tx_direction;
}

uint16_t iec104_get_t3_timeout(void)
{
    return active_t3;
}

void setUp(void)
{
    active_t3 = 60U;
    tx_direction = GSM_TX_DIR_IDLE;
    gsm_get_tx_direction_Stub(get_tx_direction);
    memset(&gsm, 0, sizeof(gsm));
    carrier_lost = 0U;
    gsm_listener_get_no_carrier_Stub(get_carrier_lost);
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
    iec104_process_apply_pending_config_Expect();
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
    ctx->configured_idle_timeout = 70U;
    ctx->pending_idle_timeout = 130U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_ERROR);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CHECK_NET, ctx->state);
    TEST_ASSERT_EQUAL_UINT16(70U, ctx->configured_idle_timeout);
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
    gsm_engine_get_query_res_ExpectAndReturn(GSM_NO_CARRIER);
    gsm_socket_record_disconnect_Expect(IEC104_LISTENER_SOCKET,
        SOCK_DISC_NO_CARRIER);
    gsm_set_socket_state_Expect(IEC104_LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_listener_set_no_carrier_Expect(GSM_LISTENER_IEC104, 0U);
    gsm_listener_set_rx_available_Expect(GSM_LISTENER_IEC104, 0U);
    gsm_listener_request_close_socket_clear_Expect(GSM_LISTENER_IEC104);
    iec104_process_socket_closed_cb_Expect();
    gsm_reset_iec104_session_info_Expect();
    led_driver_set_iec104_mode_Expect(LED_LISTENER_OFF);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET, ctx->state);
}

void test_scfg_cache_records_sent_value_and_rechecks_before_open(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_CONFIG_SOCKET;
    iec104_process_apply_pending_config_Expect();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 1U);
    gsm_listener_iec104_process();
    active_t3 = 120U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT16(70U, ctx->configured_idle_timeout);
    iec104_process_apply_pending_config_Expect();
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_CONFIG_SOCKET, ctx->state);
    iec104_process_apply_pending_config_Expect();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 1U);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT16(130U, ctx->pending_idle_timeout);
}

void test_rejected_scfg_send_does_not_mark_timeout_configured(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_IEC104];
    ctx->state = GSM_LS_CONFIG_SOCKET;
    iec104_process_apply_pending_config_Expect();
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_SOCKET_3_CONFIG_SCFG, 0U);
    gsm_listener_iec104_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_SEND, ctx->phase);
    TEST_ASSERT_EQUAL_UINT16(0U, ctx->configured_idle_timeout);
}

void test_first_web_open_sets_listening_state_and_led(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_OPEN_SOCKET;
    gsm_socket_record_attempt_Expect(LISTENER_SOCKET);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_OPEN_LISTENER_SOCKET, 1U);
    gsm_listener_web_process();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_listener_set_no_carrier_Expect(GSM_LISTENER_WEB, 0U);
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_LISTENING);
    led_driver_set_web_mode_Expect(LED_LISTENER_LISTENING);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_LS_IDLE, ctx->state);
}

static void expect_web_close(void)
{
    gsm_listener_set_no_carrier_Expect(GSM_LISTENER_WEB, 0U);
    gsm_listener_set_rx_available_Expect(GSM_LISTENER_WEB, 0U);
    gsm_listener_request_close_socket_clear_Expect(GSM_LISTENER_WEB);
    webserver_on_connection_closed_Expect();
    gsm_reset_web_session_info_Expect();
    led_driver_set_web_mode_Expect(LED_LISTENER_OFF);
}

void test_carrier_loss_before_prompt_cancels_only_web_tx_and_queries_status(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_DATA_MODE;
    carrier_lost = 1U;
    tx_direction = GSM_TX_DIR_LISTENER_SOCKET;
    gsm_set_tx_error_Expect();
    gsm_set_tx_state_Expect(GSM_TX_READY);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_SOCKET, ctx->state);
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_SEND, ctx->phase);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_LISTENER_SOCKET_STATUS, 1U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
}

void test_web_carrier_loss_does_not_clear_iec104_tx_or_other_at_query(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_DATA_MODE;
    carrier_lost = 1U;
    tx_direction = GSM_TX_DIR_IEC104_SOCKET;
    gsm_set_free_StopIgnore();
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_SOCKET, ctx->state);
}

void test_prompt_wait_is_not_abandoned_for_close_request_or_carrier_loss(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_DATA_MODE;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    gsm_engine_get_query_res_ExpectAndReturn(0U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_DATA_MODE, ctx->state);
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_busy_Expect();
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_DATA_MODE);
    gsm_engine_socket_send_ExpectAndReturn(ATQUERY_SEND_DATA_TO_LISTENER_SOCKET, 1U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_SEND_DATA, ctx->state);
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
}

void test_completed_payload_does_not_reconnect_socket_after_carrier_loss(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_SEND_DATA;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_CLOSED);
    gsm_set_tx_state_Expect(GSM_TX_READY);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_SOCKET, ctx->state);
}

void test_confirmed_closed_socket_cleans_once_and_skips_sh(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CHECK_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_raw_socket_state_IgnoreAndReturn('0');
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_CLOSED);
    expect_web_close();
    gsm_listener_get_new_conn_req_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_listener_get_rx_available_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_NET, ctx->state);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
    TEST_ASSERT_EQUAL_UINT8(1U, gsm.net_check_requested);
}

void test_carrier_loss_with_stale_connected_status_still_closes_modem_socket(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CHECK_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_raw_socket_state_IgnoreAndReturn('2');
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CLOSE_SOCKET, ctx->state);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_CLOSE_LISTENER_SOCKET, 1U);
    gsm_listener_web_process();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    expect_web_close();
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_NET, ctx->state);
}

void test_carrier_loss_preserves_already_listening_socket_without_sh_or_sl(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CHECK_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_raw_socket_state_IgnoreAndReturn('4');
    expect_web_close();
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_LISTENING);
    led_driver_set_web_mode_Expect(LED_LISTENER_LISTENING);
    gsm_listener_get_rx_available_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_IDLE, ctx->state);
}

void test_close_timeout_waits_for_status_without_repeating_close_callbacks(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CLOSE_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_TIMEOUT);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_SOCKET, ctx->state);
}

void test_new_connection_after_carrier_loss_keeps_pending_accept(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CHECK_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    tx_direction = GSM_TX_DIR_LISTENER_SOCKET;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_raw_socket_state_IgnoreAndReturn('5');
    gsm_set_tx_error_Expect();
    gsm_set_tx_state_Expect(GSM_TX_READY);
    expect_web_close();
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_OPENED);
    gsm_listener_get_rx_available_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_ACCEPT_CONNECTION, ctx->state);
    carrier_lost = 0U;
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_ACCEPT_LISTENER_CONN, 1U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_INIT_PHASE_WAIT_RESPONSE, ctx->phase);
}

void test_sh_error_after_carrier_loss_waits_for_confirmed_close_before_reopen(void)
{
    gsm_listener_ctx_t *ctx = &gsm.listener[GSM_LISTENER_WEB];
    ctx->state = GSM_LS_CLOSE_SOCKET;
    ctx->phase = GSM_INIT_PHASE_WAIT_RESPONSE;
    carrier_lost = 1U;
    gsm_engine_get_query_res_ExpectAndReturn(GSM_ERROR);
    gsm_set_delay_StopIgnore();
    gsm_set_delay_Expect(500U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_SOCKET, ctx->state);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_LISTENER_SOCKET_STATUS, 1U);
    gsm_listener_web_process();
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_raw_socket_state_IgnoreAndReturn('0');
    gsm_set_socket_state_Expect(LISTENER_SOCKET, SOCKET_CLOSED);
    expect_web_close();
    gsm_listener_get_new_conn_req_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_listener_get_rx_available_ExpectAndReturn(GSM_LISTENER_WEB, 0U);
    gsm_set_delay_Expect(50U);
    gsm_listener_web_process();
    TEST_ASSERT_EQUAL(GSM_LS_CHECK_NET, ctx->state);
}

/*** end of file ***/
