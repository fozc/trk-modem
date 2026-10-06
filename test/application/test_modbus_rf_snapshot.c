/*
 * test_modbus_rf_snapshot.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise real RF cache and Modbus FC03 current/link consumers.
 */

#include "unity.h"
#include "rf.h"
#include "modbus_rf_stats.h"
#include "mock_rf_inventory.h"
#include "mock_bsp.h"
#include "mock_modbus_config.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_power_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_breaker.h"
#include "mock_nvram.h"
#include "modbus_rtu_slave.h"
#include <math.h>
#include <string.h>

TEST_SOURCE_FILE("rf.c")
#include "../support/modbus_rf_snapshot_fixture.h"

void setUp(void)
{
    reset_fixture();
}

void tearDown(void)
{
}

void test_real_scp_current_replaces_dummy_and_rf_link_is_online(void)
{
    check_real_scp_current_replaces_dummy_and_rf_link_is_online();
}

void test_missing_and_stale_current_are_nan_and_link_is_offline(void)
{
    check_missing_and_stale_current_are_nan_and_link_is_offline();
}

void test_invalid_current_does_not_turn_fresh_rf_link_offline(void)
{
    check_invalid_current_does_not_turn_fresh_rf_link_offline();
}

void test_both_float_words_use_the_same_age_check_at_timeout_boundary(void)
{
    check_float_word_age_at_timeout_boundary();
}

void test_mh_restart_does_not_reuse_dummy_or_old_rf_value(void)
{
    check_mh_restart_does_not_reuse_dummy_or_old_rf_value();
}

void test_energy_and_quality_are_separate_and_preserve_last_value(void)
{
    check_energy_and_quality_use_the_same_real_live_sample();
}

void test_fixed_quality_block_is_read_only_and_has_checked_bounds(void)
{
    check_quality_block_is_read_only_and_bounds_preserve_value();
}

void test_load_indicator_uses_mh_flag_in_fixed_map(void)
{
    check_load_indicator_uses_mh_flag();
}

/*** end of file ***/
