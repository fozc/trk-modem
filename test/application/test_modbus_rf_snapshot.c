/*
 * test_modbus_rf_snapshot.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise the real RF cache through the fixed feeder blocks: quality
 * semantics, live values, energy/load flags and reserved gaps.
 */

#include "unity.h"
#include "rf.h"
#include "mock_rf_inventory.h"
#include "mock_bsp.h"
#include "mock_modbus_config.h"
#include "mock_relay.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_power_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_fault_log.h"
#include "mock_elog.h"
#include "mock_shell.h"
#include "mock_power_board_control.h"
#include "mock_breaker.h"
#include "mock_nvram.h"
#include "modbus_rtu_slave.h"
#include <math.h>
#include <string.h>

TEST_SOURCE_FILE("rf.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("cp56time2a.c")
TEST_SOURCE_FILE("power_board_scp.c")
#include "../support/modbus_rf_snapshot_fixture.h"

void setUp(void)
{
    reset_fixture();
}

void tearDown(void)
{
}

void test_live_current_link_and_quality(void)
{
    check_live_current_link_and_quality();
}

void test_missing_and_stale_sample_zero_then_aged_value(void)
{
    check_missing_and_stale();
}

void test_invalid_current_keeps_link_online(void)
{
    check_invalid_current_keeps_link_online();
}

void test_freshness_flips_at_timeout_boundary(void)
{
    check_freshness_flips_at_timeout_boundary();
}

void test_mh_restart_drops_freshness(void)
{
    check_mh_restart_drops_freshness();
}

void test_energy_and_load_flags_follow_live_sample(void)
{
    check_energy_and_load_flags_follow_live_sample();
}

void test_undefined_feeder_reports_zero_quality(void)
{
    check_undefined_feeder_reports_zero_quality();
}

void test_reserved_gaps_read_zero_and_write_rejected(void)
{
    check_reserved_gaps_read_zero_and_write_rejected();
}

void test_stalled_uptime_clears_validity_but_keeps_link(void)
{
    check_stalled_uptime_clears_validity_but_keeps_link();
}

void test_valid_sample_time_sets_time_bit_and_unix(void)
{
    check_valid_sample_time_sets_time_bit_and_unix();
}

void test_trip_failure_publishes_without_live(void)
{
    check_trip_failure_publishes_without_live();
}

/*** end of file ***/
