/*
 * test_gsm_periodical_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_process.h"
#include "mock_gsm_listener_process.h"
#include "mock_gsm_signal_led.h"
#include "mock_gsm_info.h"
#include "mock_bsp.h"
#include "gsm_types.h"
#include "gsm_periodical.h"
#include <string.h>

/* gsm_periodical.c owns only an extern reference; the scenario provides
 * the shared modem state object here. */
gsm_t gsm;

static uint32_t fake_tick = 1000U;

/* Park every periodical timer in the future so the scheduler picks only
 * the one the test deliberately expires. */
static void arm_timers_idle(void)
{
    gsm.signal_quality_timer   = fake_tick + 60000U;
    gsm.sms_check_timer        = fake_tick + 60000U;
    gsm.network_type_timer     = fake_tick + 60000U;
    gsm.socket_info_timer      = fake_tick + 60000U;
    gsm.gprs_check_timer       = fake_tick + 60000U;
    gsm.net_check_requested    = 0U;
    gsm.net_reconnect_retry    = 0U;
    gsm.periodical_event_state = 0U;
    gsm.periodical_phase       = GSM_INIT_PHASE_SEND;
}

void setUp(void)
{
    memset(&gsm, 0, sizeof(gsm));
    arm_timers_idle();
}

void tearDown(void)
{
}

void test_schedule_skipped_when_not_in_normal_mode(void)
{
    gsm_get_main_state_ExpectAndReturn(GSM_INIT_DONE);

    gsm_periodical_event_process();

    /* No query may leave and no state may change outside NORMAL mode;
     * any unexpected engine call would fail the strict mock order. */
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.periodical_event_state);
}

void test_ext_signal_quality_cycle_rearms_timer_and_led(void)
{
    /* Signal-quality timer due: scheduler picks the CESQ step. */
    gsm.signal_quality_timer = 0U;

    /* Tick 1: scheduler decision + query send phase. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_EXT_SIGNAL_QUALITY,
                                          true);
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_GET_EXT_SIGNAL_QUALITY,
                            gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_WAIT_RESPONSE, gsm.periodical_phase);

    /* Tick 2: response arrives -> LED refresh + 60 s rearm + release. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_signal_led_update_Expect();
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();

    TEST_ASSERT_EQUAL_UINT8(0U, gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT8(GSM_INIT_PHASE_SEND, gsm.periodical_phase);
    TEST_ASSERT_EQUAL_UINT32(fake_tick + 60000U, gsm.signal_quality_timer);
}

void test_present_sms_chains_into_delete_step(void)
{
    gsm.sms_check_timer = 0U;

    /* Tick 1: scheduler passes the signal timer, then selects SMS. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_CMGR, true);
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_SMS, gsm.periodical_event_state);

    /* Tick 2: an SMS is present -> schedule the delete step. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_SMS_REC_UNREAD);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_DELETE_SMS, gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT32(fake_tick + 180000U, gsm.sms_check_timer);

    /* Tick 3: delete query goes out. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_CMGD, true);
    gsm_periodical_event_process();

    /* Tick 4: delete OK -> back to idle. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.periodical_event_state);
}

void test_absent_sms_returns_to_idle_without_delete(void)
{
    gsm.sms_check_timer = 0U;

    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_CMGR, true);
    gsm_periodical_event_process();

    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_SMS_NO_SMS);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();

    TEST_ASSERT_EQUAL_UINT8(0U, gsm.periodical_event_state);
}

void test_listener_gprs_request_checked_first_and_cleared_when_up(void)
{
    gsm.net_check_requested = 1U;

    /* The listener request wins over every timer. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_GPRS_STATUS, true);
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_GPRS_CONNECTION,
                            gsm.periodical_event_state);

    /* GPRS is up: clear the request, rearm the 45 s check, go idle. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    bsp_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_gprs_state_ExpectAndReturn(true);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();

    TEST_ASSERT_EQUAL_UINT8(0U, gsm.net_check_requested);
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT32(fake_tick + (45U * 1000U),
                             gsm.gprs_check_timer);
}

void test_gprs_down_escalates_reconnect_then_process_reset(void)
{
    gsm.net_check_requested = 1U;

    /* Check says GPRS is down: flag the fault, enter reconnect. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_GPRS_STATUS, true);
    gsm_periodical_event_process();

    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    bsp_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_gprs_state_ExpectAndReturn(false);
    gsm_internet_connection_faild_cd_Expect();
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_RECONNECT_GPRS_CONNECTION,
                            gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT8(0U, gsm.net_reconnect_retry);

    /* Two failed activation attempts stay in the reconnect loop (1 s
     * pacing); the third failure escalates to a full process reset. */
    for (uint8_t attempt = 0U; attempt < 3U; attempt++)
    {
        gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
        gsm_engine_send_query_ExpectAndReturn(ATQUERY_ACTIVATE_GPRS, true);
        gsm_periodical_event_process();

        gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
        gsm_engine_get_query_res_ExpectAndReturn(GSM_TIMEOUT);
        if (attempt < 2U)
        {
            gsm_set_delay_Expect(1000U);
            gsm_set_free_Expect();
        }
        else
        {
            gsm_reset_process_old_Expect();
        }
        gsm_periodical_event_process();
    }

    TEST_ASSERT_EQUAL_UINT8(GSM_RECONNECT_GPRS_CONNECTION,
                            gsm.periodical_event_state);
    TEST_ASSERT_EQUAL_UINT8(3U, gsm.net_reconnect_retry);
}

void test_gprs_check_error_with_pending_request_resets_process(void)
{
    gsm.net_check_requested = 1U;

    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_GPRS_STATUS, true);
    gsm_periodical_event_process();

    /* Response error while the listener waits: the 45 s timer still
     * rearms (line: gsm.gprs_check_timer = bsp_get_tick() + ...), then
     * aggressive recovery takes over. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_TIMEOUT);
    bsp_get_tick_ExpectAndReturn(fake_tick);
    gsm_reset_process_old_Expect();
    gsm_periodical_event_process();

    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_GPRS_CONNECTION,
                            gsm.periodical_event_state);
}

void test_socket_info_rearms_listener_data_checks(void)
{
    gsm.socket_info_timer = 0U;

    /* Scheduler walks past signal, SMS and network timers. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_engine_send_query_ExpectAndReturn(ATQUERY_GET_ALL_SOCKET_INFO, true);
    gsm_periodical_event_process();
    TEST_ASSERT_EQUAL_UINT8(GSM_CHECK_ALL_SOCKET_INFO,
                            gsm.periodical_event_state);

    /* Response: 30 s rearm on the global timer and both listeners so the
     * per-socket SI queries do not double-poll. */
    gsm_get_main_state_ExpectAndReturn(GSM_NORMAL_MODE);
    gsm_engine_get_query_res_ExpectAndReturn(GSM_RESPONSE_OK);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_get_tick_ExpectAndReturn(fake_tick);
    gsm_set_delay_Expect(50U);
    gsm_set_free_Expect();
    gsm_periodical_event_process();

    TEST_ASSERT_EQUAL_UINT32(fake_tick + 30000U, gsm.socket_info_timer);
    TEST_ASSERT_EQUAL_UINT32(fake_tick + 30000U,
                             gsm.listener[GSM_LISTENER_WEB].data_check_timer);
    TEST_ASSERT_EQUAL_UINT32(fake_tick + 30000U,
                             gsm.listener[GSM_LISTENER_IEC104].data_check_timer);
}
