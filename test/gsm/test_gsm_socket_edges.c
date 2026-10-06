/*
 * test_gsm_socket_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * Public socket/session contracts with a deterministic clock.
 */
#include "unity.h"
#include "gsm_socket.h"
#include "mock_bsp.h"
#include <string.h>

static uint32_t tick;
static uint32_t epoch;

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return tick;
}
static uint32_t get_epoch(int call_count)
{
    (void)call_count;
    return epoch;
}
void setUp(void)
{
    tick = 100U;
    epoch = 1700000000U;
    bsp_get_tick_Stub(get_tick);
    bsp_get_epoch_time_Stub(get_epoch);
    gsm_socket_init();
}
void tearDown(void)
{
}

void test_socket_init_resets_all_sessions_and_counters(void)
{
    gsm_socket_set_state(0U, '2');
    gsm_socket_record_attempt(0U);
    gsm_socket_record_error(0U);
    gsm_socket_init();
    for (uint8_t i = 0U; i < GSM_SOCKET_COUNT; i++)
    {
        const gsm_socket_stats_t *stats = gsm_socket_get_stats(i);
        TEST_ASSERT_NOT_NULL(stats);
        TEST_ASSERT_FALSE(stats->is_connected);
        TEST_ASSERT_EQUAL_UINT32(0U, stats->total_sessions);
        TEST_ASSERT_EQUAL_UINT32(0U, stats->connect_attempts);
        TEST_ASSERT_EQUAL_UINT32(0U, stats->error_count);
        TEST_ASSERT_EQUAL_UINT32(0U, gsm_socket_get_last_activity(i));
    }
}
void test_socket_invalid_indices_preserve_valid_session(void)
{
    gsm_socket_set_state(0U, '2');
    const gsm_socket_stats_t before = *gsm_socket_get_stats(0U);
    const uint8_t invalid[] = {GSM_SOCKET_COUNT, UINT8_MAX};
    for (size_t i = 0U; i < sizeof(invalid); i++)
    {
        gsm_socket_set_state(invalid[i], '3');
        gsm_socket_touch_activity(invalid[i]);
        gsm_socket_record_attempt(invalid[i]);
        gsm_socket_record_error(invalid[i]);
        gsm_socket_record_disconnect(invalid[i], SOCK_DISC_ERROR);
        TEST_ASSERT_NULL(gsm_socket_get_stats(invalid[i]));
        TEST_ASSERT_EQUAL_HEX8(0xFFU, gsm_socket_get_state(invalid[i]));
        TEST_ASSERT_EQUAL_UINT32(0U,
            gsm_socket_get_last_activity(invalid[i]));
        TEST_ASSERT_FALSE(gsm_socket_is_connected(invalid[i]));
        TEST_ASSERT_FALSE(gsm_socket_is_check_expired(invalid[i]));
        TEST_ASSERT_FALSE(gsm_socket_first_data_expired(invalid[i], 1U));
        TEST_ASSERT_FALSE(gsm_socket_idle_expired(invalid[i], 1U));
    }
    TEST_ASSERT_EQUAL_MEMORY(&before, gsm_socket_get_stats(0U),
        sizeof(before));
}
void test_socket_connected_state_changes_do_not_restart_session(void)
{
    gsm_socket_set_state(0U, '2');
    tick = 500U;
    gsm_socket_set_state(0U, '3');
    const gsm_socket_stats_t *stats = gsm_socket_get_stats(0U);
    TEST_ASSERT_TRUE(stats->is_connected);
    TEST_ASSERT_EQUAL_UINT32(1U, stats->total_sessions);
    TEST_ASSERT_EQUAL_UINT32(100U, stats->connect_tick);
    TEST_ASSERT_EQUAL_UINT32(500U, gsm_socket_get_last_activity(0U));
    tick = 900U;
    gsm_socket_set_state(0U, '3');
    TEST_ASSERT_EQUAL_UINT32(500U, gsm_socket_get_last_activity(0U));
}
void test_socket_first_data_timeout_exact_boundary_and_disabled_watch(void)
{
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 1U));
    gsm_socket_set_state(0U, '2');
    tick = 1099U;
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 1000U));
    tick = 1100U;
    TEST_ASSERT_TRUE(gsm_socket_first_data_expired(0U, 1000U));
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 0U));
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 1U));
    gsm_socket_touch_activity(0U);
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 1U));
}
void test_socket_idle_timeout_starts_only_after_rx_and_rearms_on_activity(void)
{
    gsm_socket_set_state(0U, '2');
    tick = 2000U;
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 1000U));
    gsm_socket_touch_activity(0U);
    tick = 2999U;
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 1000U));
    tick = 3000U;
    TEST_ASSERT_TRUE(gsm_socket_idle_expired(0U, 1000U));
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 0U));
    gsm_socket_touch_activity(0U);
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 1000U));
    gsm_socket_set_state(0U, '4');
    TEST_ASSERT_FALSE(gsm_socket_idle_expired(0U, 1U));
}
void test_socket_timeouts_and_duration_survive_uint32_tick_wrap(void)
{
    tick = UINT32_MAX - 9U;
    gsm_socket_set_state(0U, '2');
    tick = 9U;
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 20U));
    tick = 10U;
    TEST_ASSERT_TRUE(gsm_socket_first_data_expired(0U, 20U));
    gsm_socket_set_state(0U, '0');
    TEST_ASSERT_EQUAL_UINT32(20U,
        gsm_socket_get_stats(0U)->last_session_ms);
    tick = UINT32_MAX - 4U;
    gsm_socket_set_state(0U, '2');
    gsm_socket_touch_activity(0U);
    tick = 5U;
    TEST_ASSERT_TRUE(gsm_socket_idle_expired(0U, 10U));
}
void test_socket_check_timeout_boundary_and_activity_reset(void)
{
    tick = 179999U;
    TEST_ASSERT_FALSE(gsm_socket_is_check_expired(0U));
    tick = 180000U;
    TEST_ASSERT_TRUE(gsm_socket_is_check_expired(0U));
    gsm_socket_touch_activity(0U);
    TEST_ASSERT_FALSE(gsm_socket_is_check_expired(0U));
    tick += 180000U;
    TEST_ASSERT_TRUE(gsm_socket_is_check_expired(0U));
}
void test_socket_disconnect_reason_and_duration_count_once(void)
{
    gsm_socket_set_state(0U, '2');
    tick = 2100U;
    epoch += 2U;
    gsm_socket_record_disconnect(0U, SOCK_DISC_IDLE_TIMEOUT);
    gsm_socket_set_state(0U, '0');
    gsm_socket_record_disconnect(0U, SOCK_DISC_ERROR);
    const gsm_socket_stats_t *stats = gsm_socket_get_stats(0U);
    TEST_ASSERT_EQUAL_UINT32(1U, stats->total_disconnects);
    TEST_ASSERT_EQUAL_UINT32(2000U, stats->last_session_ms);
    TEST_ASSERT_EQUAL_UINT32(1700000000U, stats->last_connect_epoch);
    TEST_ASSERT_EQUAL_UINT32(1700000002U, stats->last_disconnect_epoch);
    TEST_ASSERT_EQUAL(SOCK_DISC_IDLE_TIMEOUT, stats->last_disconnect_reason);
    TEST_ASSERT_FALSE(stats->is_connected);
}
void test_socket_multiple_sessions_accumulate_and_keep_longest_duration(void)
{
    gsm_socket_set_state(0U, '2');
    tick = 1100U;
    gsm_socket_set_state(0U, '4');
    TEST_ASSERT_EQUAL(SOCK_DISC_NO_CARRIER,
        gsm_socket_get_stats(0U)->last_disconnect_reason);
    tick = 1200U;
    gsm_socket_set_state(0U, '3');
    tick = 1500U;
    gsm_socket_set_state(0U, '0');
    const gsm_socket_stats_t *stats = gsm_socket_get_stats(0U);
    TEST_ASSERT_EQUAL_UINT32(2U, stats->total_sessions);
    TEST_ASSERT_EQUAL_UINT32(2U, stats->total_disconnects);
    TEST_ASSERT_EQUAL_UINT32(1300U, stats->total_connected_ms);
    TEST_ASSERT_EQUAL_UINT32(1000U, stats->max_session_ms);
    TEST_ASSERT_EQUAL_UINT32(300U, stats->last_session_ms);
}
void test_socket_reconnect_restarts_first_data_watch_and_sockets_are_isolated(void)
{
    gsm_socket_set_state(0U, '2');
    gsm_socket_set_state(1U, '3');
    gsm_socket_touch_activity(0U);
    tick += 10U;
    TEST_ASSERT_FALSE(gsm_socket_first_data_expired(0U, 10U));
    TEST_ASSERT_TRUE(gsm_socket_first_data_expired(1U, 10U));
    gsm_socket_set_state(0U, '0');
    gsm_socket_set_state(0U, '2');
    tick += 10U;
    TEST_ASSERT_TRUE(gsm_socket_first_data_expired(0U, 10U));
    gsm_socket_record_attempt(0U);
    gsm_socket_record_error(1U);
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_socket_get_stats(0U)->connect_attempts);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_socket_get_stats(1U)->connect_attempts);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_socket_get_stats(0U)->error_count);
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_socket_get_stats(1U)->error_count);
}
/*** end of file ***/
