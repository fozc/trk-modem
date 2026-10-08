/*
 * test_rf_faults.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise real sequence classification and feeder opening counts.
 */
#include "unity.h"
#include "rf_faults.h"
#include "cp56time2a.h"
#include "mock_rf_inventory.h"
#include "mock_bsp.h"
#include <string.h>

TEST_SOURCE_FILE("cp56time2a.c")

static rf_scp_event_t event;

static rf_fault_class_t classify(uint8_t code, uint8_t phase, uint16_t ms)
{
    event.event = code;
    event.phase = phase;
    event.uptime_sec++;
    const cp56time2a_t time = cp56time2a_make(ms, 0U, 12U,
                                            7U, 3U, 10U, 26U);
    (void)memcpy(event.timestamp, &time, sizeof(time));
    return rf_faults_classify(&event);
}

static rf_fault_stats_t counts(uint8_t feeder)
{
    rf_fault_stats_t result;
    TEST_ASSERT_TRUE(rf_faults_get_stats(1U, feeder, &result));
    return result;
}

void setUp(void)
{
    rf_faults_init();
    event = (rf_scp_event_t){.zone = 1U, .feeder = 1U, .phase = 1U,
        .boot_counter = 1U, .clock_quality = 1U};
    rf_inventory_get_binding_IgnoreAndReturn(false);
}

void tearDown(void)
{
}

void test_detection_without_breaker_opening_is_raw_only(void)
{
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(4U, 1U, 10000U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 11000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).permanent);
}

void test_breaker_opening_then_recovery_is_one_temporary_fault(void)
{
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(5U, 1U, 10000U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(6U, 1U, 11000U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(3U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
}

void test_permanent_failure_prevents_later_recovery_from_becoming_temporary(void)
{
    (void)classify(6U, 1U, 10000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, classify(101U, 1U, 11000U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_permanent_on_another_phase_prevents_a_second_temporary_count(void)
{
    (void)classify(6U, 2U, 10000U);
    (void)classify(1U, 1U, 11000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 2U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
}

void test_three_phase_opening_has_three_outcomes_and_one_feeder_count(void)
{
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, classify(1U, 1U, 10000U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, classify(7U, 2U, 10500U));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, classify(7U, 3U, 11000U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
    (void)classify(1U, 1U, 13000U);
    TEST_ASSERT_EQUAL_UINT32(2U, counts(1U).permanent);
}

void test_failure_followed_by_opening_does_not_increment_feeder_twice(void)
{
    (void)classify(100U, 1U, 10000U);
    (void)classify(7U, 2U, 10500U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_failure_after_known_opening_is_not_an_additional_fault_entry(void)
{
    (void)classify(1U, 1U, 10000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(101U, 2U, 10500U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_unsynchronized_clock_does_not_produce_an_exact_opening_count(void)
{
    event.clock_quality = 2U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, classify(1U, 1U, 10000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).permanent);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

void test_new_boot_and_phase_replacement_cannot_complete_an_old_sequence(void)
{
    (void)classify(6U, 1U, 10000U);
    event.boot_counter = 2U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
    (void)classify(6U, 1U, 31000U);
    rf_faults_reset_phase(1U, 1U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 40000U));
}

void test_unknown_diagnostic_does_not_erase_a_valid_fault_sequence(void)
{
    (void)classify(6U, 1U, 10000U);
    rf_scp_event_t unknown = {.event = 255U, .zone = 1U,
        .feeder = 1U, .phase = 1U};
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&unknown));
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(3U, 1U, 30000U));
}

void test_unassigned_117_has_a_separate_count_and_no_assigned_feeder(void)
{
    event.feeder = 0U;
    event.phase = 0U;
    event.event = 117U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, rf_faults_classify(&event));
    TEST_ASSERT_EQUAL_UINT32(1U, counts(0U).permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).permanent);
}

void test_invalid_input_and_intermediate_events_preserve_counts(void)
{
    (void)classify(1U, 1U, 10000U);
    const rf_fault_stats_t before = counts(1U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(NULL));
    event.feeder = 5U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
    event.feeder = 1U;
    event.zone = 8U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
    rf_fault_stats_t after = counts(1U);
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(after));
    after = (rf_fault_stats_t){.permanent = 42U};
    TEST_ASSERT_FALSE(rf_faults_get_stats(8U, 1U, &after));
    TEST_ASSERT_EQUAL_UINT32(42U, after.permanent);
}

void test_unreliable_permanent_time_cannot_block_a_new_valid_sequence(void)
{
    event.clock_quality = 2U;
    (void)classify(101U, 1U, 50000U);
    event.clock_quality = 1U;
    (void)classify(5U, 1U, 10000U);
    (void)classify(6U, 1U, 11000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(3U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

void test_valid_newer_permanent_time_survives_an_older_detection(void)
{
    (void)classify(101U, 1U, 50000U);
    (void)classify(5U, 1U, 10000U);
    (void)classify(6U, 1U, 11000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
}

/*** end of file ***/
