/*
 * test_gsm_wtd_liveness_scenario.c
 *
 * GSM software watchdog liveness recovery scenarios.
 */

#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gsm_wtd_test_types.h"
#include "gsm_wtd.h"

gsm_t gsm;

static uint32_t tick_now;
static uint8_t busy_state;
static unsigned int restart_sets;
static unsigned int bsp_reset_calls;
static unsigned int elog_liveness_calls;
static uint8_t elog_last_recovery_count;

uint32_t gsm_get_tick(void)
{
    return tick_now;
}

bool gsm_is_busy(void)
{
    return (0U != busy_state);
}

void gsm_set_busy(void)
{
    busy_state = 1U;
}

void gsm_set_free(void)
{
    busy_state = 0U;
}

void gsm_request_module_restart(void)
{
    restart_sets++;
}

void bsp_system_reset(void)
{
    bsp_reset_calls++;
}

void gsm_elog_modem_event(elog_code_t code)
{
    (void)code;
}

void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg,
    uint8_t arg_len)
{
    (void)arg_len;

    if (ELOG_GSM_WTD_LIVENESS == code)
    {
        elog_liveness_calls++;
        elog_last_recovery_count = ((const uint8_t *)arg)[2];
    }
}

void at_engine_clear_buff(void)
{
}

void at_engine_reset(void)
{
}

const char *at_engine_get_state_str(void)
{
    return "IDLE";
}

void gsm_log_set_level(gsm_log_level_t level)
{
    (void)level;
}

gsm_log_level_t gsm_log_get_level(void)
{
    return 0;
}

void setUp(void)
{
    (void)memset(&gsm, 0, sizeof(gsm));
    tick_now = 1000U;
    busy_state = 0U;
    restart_sets = 0U;
    bsp_reset_calls = 0U;
    elog_liveness_calls = 0U;
    elog_last_recovery_count = 0U;
    gsm_wtd_liveness_ping();
}

void tearDown(void)
{
}

void test_recent_ping_does_not_trigger_recovery(void)
{
    tick_now += 9U * 60U * 1000U;

    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT(0U, restart_sets);
    TEST_ASSERT_EQUAL_UINT(0U, bsp_reset_calls);
}

void test_silence_requests_two_restarts_then_hard_reset(void)
{
    gsm_wtd_liveness_ping();
    tick_now += 11U * 60U * 1000U;
    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT(1U, restart_sets);
    TEST_ASSERT_EQUAL_UINT(1U, elog_liveness_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, elog_last_recovery_count);
    TEST_ASSERT_EQUAL_UINT(0U, bsp_reset_calls);

    tick_now += (10U * 60U * 1000U) + 100U;
    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT(2U, restart_sets);
    TEST_ASSERT_EQUAL_UINT8(2U, elog_last_recovery_count);
    TEST_ASSERT_EQUAL_UINT(0U, bsp_reset_calls);

    tick_now += (10U * 60U * 1000U) + 100U;
    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT(1U, bsp_reset_calls);
    TEST_ASSERT_EQUAL_UINT(2U, restart_sets);
    TEST_ASSERT_EQUAL_UINT8(3U, elog_last_recovery_count);
}

void test_ping_resets_recovery_counter(void)
{
    tick_now += 11U * 60U * 1000U;
    gsm_wtd_check();
    TEST_ASSERT_EQUAL_UINT(1U, restart_sets);

    gsm_wtd_liveness_ping();
    tick_now += 11U * 60U * 1000U;
    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT8(1U, elog_last_recovery_count);
}

void test_busy_engine_owns_recovery(void)
{
    tick_now += 20U * 60U * 1000U;
    busy_state = 1U;

    gsm_wtd_check();

    TEST_ASSERT_EQUAL_UINT(0U, restart_sets);
    TEST_ASSERT_EQUAL_UINT(0U, bsp_reset_calls);
}

/*** end of file ***/
