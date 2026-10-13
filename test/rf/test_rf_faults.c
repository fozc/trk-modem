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
#include "rf_types.h"
#include "mock_bsp.h"
#include <string.h>

TEST_SOURCE_FILE("cp56time2a.c")

static rf_scp_event_t event;
static rf_feeder_t feeder_config;

const rf_feeder_t *rf_store_get(feeder_id_t index);
const rf_feeder_t *rf_store_get(feeder_id_t index)
{
    (void)index;
    return &feeder_config;
}

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
    feeder_config = (rf_feeder_t){.in_use = true};
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
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(142U, 1U, 30000U));
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
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(142U, 1U, 30000U));
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
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(142U, 1U, 30000U));
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

void test_r2_event_three_never_creates_a_temporary_fault(void)
{
    (void)classify(6U, 1U, 10000U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
}

void test_r2_142_is_a_result_without_prior_detection(void)
{
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, classify(142U, 1U, 30000U));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

void test_r2_106_and_141_sequences_end_without_a_temporary_fault(void)
{
    const uint8_t failures[] = {100U, 101U, 106U, 117U, 141U};
    for (size_t index = 0U; sizeof(failures) > index; index++)
    {
        rf_faults_init();
        event.uptime_sec = 0U;
        (void)classify(failures[index], 1U, 10000U);
        (void)classify(4U, 1U, 11000U);
        (void)classify(6U, 1U, 12000U);
        TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, classify(3U, 1U, 30000U));
        TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    }
}

void test_each_unknown_event_id_preserves_all_fault_counts(void)
{
    for (uint16_t code = 0U; 256U > code; code++)
    {
        if ((1U == code) || (7U == code) || (100U == code) ||
            (101U == code) || (142U == code))
        {
            continue;
        }
        TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE,
            classify((uint8_t)code, 1U, 10000U));
        TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).permanent);
        TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    }
}

void test_all_invalid_feeder_phase_and_zone_values_preserve_counters(void)
{
    (void)classify(142U, 1U, 10000U);
    const rf_fault_stats_t before = counts(1U);
    event.event = 142U;
    for (uint16_t value = 0U; 256U > value; value++)
    {
        event.feeder = (uint8_t)value;
        if ((0U == value) || (4U < value))
        {
            TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
        }
        event.feeder = 1U;
        event.phase = (uint8_t)value;
        if ((0U == value) || (3U < value))
        {
            TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
        }
        event.phase = 1U;
        event.zone = (uint8_t)value;
        if (8U <= value)
        {
            TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
        }
        event.zone = 1U;
    }
    const rf_fault_stats_t after = counts(1U);
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(after));
}

void test_invalid_stats_indices_and_null_preserve_output(void)
{
    rf_fault_stats_t output = {.permanent = 123U};
    TEST_ASSERT_FALSE(rf_faults_get_stats(0U, 0U, NULL));
    for (uint16_t value = 5U; 256U > value; value++)
    {
        TEST_ASSERT_FALSE(rf_faults_get_stats(1U, (uint8_t)value, &output));
        TEST_ASSERT_EQUAL_UINT32(123U, output.permanent);
    }
    for (uint16_t value = 8U; 256U > value; value++)
    {
        TEST_ASSERT_FALSE(rf_faults_get_stats((uint8_t)value, 1U, &output));
        TEST_ASSERT_EQUAL_UINT32(123U, output.permanent);
    }
}

void test_invalid_clock_keeps_phase_result_and_marks_total_uncertain(void)
{
    (void)memset(event.timestamp, 0, sizeof(event.timestamp));
    event.event = 142U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_TEMPORARY, rf_faults_classify(&event));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

void test_late_arrival_with_same_event_time_does_not_split_feeder_count(void)
{
    (void)classify(1U, 1U, 10000U);
    /* These independent AY uptimes represent delayed/out-of-order delivery.
     * Matching is on event time, never on RTU receive time. */
    event.uptime_sec = 200000U;
    (void)classify(7U, 3U, 10000U);
    event.uptime_sec = 500000U;
    (void)classify(7U, 2U, 10000U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_current_event_time_window_has_explicit_999_1000_1001_boundaries(void)
{
    const uint16_t times[] = {10999U, 11000U, 11001U};
    for (size_t index = 0U; 3U > index; index++)
    {
        rf_faults_init();
        event.uptime_sec = 0U;
        (void)classify(1U, 1U, 10000U);
        (void)classify(7U, 2U, times[index]);
        TEST_ASSERT_EQUAL_UINT32((2U == index) ? 2U : 1U,
                                 counts(1U).permanent);
    }
}

void test_older_record_in_same_ay_boot_is_uncertain_and_cannot_change_counts(void)
{
    (void)classify(6U, 1U, 9000U);
    (void)classify(142U, 1U, 10000U);
    event.uptime_sec = 0U;
    event.event = 1U;
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&event));
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).permanent);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

static bool test_binding(uint8_t source, rf_inventory_entry_t *out,
                         int call_count)
{
    (void)call_count;
    *out = (rf_inventory_entry_t){.zone = 1U, .feeder = 1U,
        .phase = (uint8_t)(source & 3U), .line_index = 0U};
    out->eui64[0] = source;
    return true;
}

static void bind_phases(void)
{
    rf_inventory_get_binding_StubWithCallback(test_binding);
}

static void observe_live(uint8_t phase, uint32_t uptime, uint32_t now_ms,
                         uint8_t pending)
{
    const rf_scp_live_t live = {.source = (uint8_t)(4U | phase),
        .boot_counter = 1U, .uptime_sec = uptime, .seq = uptime,
        .state = 1U, .log_pending = pending};
    rf_faults_live(&live, now_ms);
}

static void ready_phase(uint8_t phase, uint32_t now_ms)
{
    observe_live(phase, 1000U, now_ms, 0U);
    observe_live(phase, 1001U, now_ms + 1U, 0U);
}

void test_142_overlapping_intervals_merge_despite_distant_end_times(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(4U, 2U, 15000U);
    (void)classify(142U, 1U, 30000U);
    (void)classify(142U, 2U, 45000U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).pending);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    ready_phase(1U, 10U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(12U));
    ready_phase(2U, 20U);
    TEST_ASSERT_TRUE(rf_faults_needs_head(22U));
    rf_faults_ring_empty(rf_faults_head_started(), 22U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
}

void test_disjoint_142_intervals_are_two_faults_even_with_close_end_times(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(142U, 1U, 11000U);
    (void)classify(4U, 2U, 11100U);
    (void)classify(142U, 2U, 11500U);
    TEST_ASSERT_EQUAL_UINT32(2U, counts(1U).temporary);
}

void test_transitive_interval_bridge_merges_both_existing_groups(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(142U, 1U, 20000U);
    (void)classify(4U, 2U, 30000U);
    (void)classify(142U, 2U, 40000U);
    TEST_ASSERT_EQUAL_UINT32(2U, counts(1U).temporary);
    (void)classify(4U, 3U, 15000U);
    (void)classify(142U, 3U, 35000U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).pending);
}

void test_single_participating_phase_does_not_wait_for_healthy_phase_142(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(142U, 1U, 30000U);
    ready_phase(1U, 10U);
    rf_faults_ring_empty(rf_faults_head_started(), 12U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_permanent_opening_inside_a_142_interval_removes_temporary_count(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(142U, 1U, 40000U);
    (void)classify(7U, 2U, 20000U);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_late_142_inside_known_permanent_interval_is_not_an_extra_fault(void)
{
    bind_phases();
    (void)classify(1U, 1U, 20000U);
    (void)classify(4U, 2U, 10000U);
    (void)classify(142U, 2U, 40000U);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
}

void test_first_idle_pending_zero_live_cannot_complete_both_barriers(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    observe_live(1U, 100U, 10U, 0U);
    rf_faults_ring_empty(rf_faults_head_started(), 11U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    observe_live(1U, 101U, 20U, 0U);
    rf_faults_ring_empty(rf_faults_head_started(), 21U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_head_started_before_latest_pending_zero_cannot_finalize(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    observe_live(1U, 100U, 10U, 1U);
    const uint64_t old_head = rf_faults_head_started();
    observe_live(1U, 101U, 20U, 0U);
    rf_faults_ring_empty(old_head, 21U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    rf_faults_ring_empty(rf_faults_head_started(), 22U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_repeated_live_cannot_supply_the_next_pending_zero_observation(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    observe_live(1U, 100U, 10U, 0U);
    observe_live(1U, 100U, 20U, 0U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(21U));
}

void test_t_mem_dead_wait_prevents_early_idle_closure(void)
{
    bind_phases();
    feeder_config.config.t_mem_dead_sec = 30U;
    (void)classify(1U, 1U, 10000U); /* uptime 1 */
    observe_live(1U, 30U, 10U, 0U);
    observe_live(1U, 31U, 20U, 0U); /* First eligible idle sample. */
    TEST_ASSERT_FALSE(rf_faults_needs_head(21U));
    observe_live(1U, 32U, 30U, 0U);
    TEST_ASSERT_TRUE(rf_faults_needs_head(31U));
}

void test_stale_live_and_tick_wrap_are_checked_at_empty_head(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    ready_phase(1U, UINT32_MAX - 10U);
    TEST_ASSERT_TRUE(rf_faults_needs_head(5U));
    rf_faults_ring_empty(rf_faults_head_started(), 30000U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
}

void test_boot_change_keeps_sequence_count_uncertain_after_idle(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    const rf_scp_live_t changed = {.source = 5U, .boot_counter = 2U,
        .uptime_sec = 100U, .seq = 100U, .state = 1U};
    rf_faults_live(&changed, 10U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
    TEST_ASSERT_FALSE(counts(1U).is_final);
}

void test_active_or_faulted_live_revokes_a_ready_barrier(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    ready_phase(1U, 10U);
    const rf_scp_live_t active = {.source = 5U, .boot_counter = 1U,
        .uptime_sec = 1002U, .seq = 1002U, .state = 2U, .fault_count = 1U};
    rf_faults_live(&active, 20U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(21U));
}

void test_fsm_error_cannot_prove_idle_state(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    const rf_scp_live_t invalid = {.source = 5U, .boot_counter = 1U,
        .uptime_sec = 100U, .seq = 100U, .state = 1U, .fsm_error = true};
    rf_faults_live(&invalid, 10U);
    observe_live(1U, 101U, 20U, 0U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(21U));
}

void test_unsynchronized_intervals_never_become_exact_from_empty_ring(void)
{
    bind_phases();
    for (uint8_t quality = 0U; 2U >= quality; quality += 2U)
    {
        rf_faults_init();
        event.uptime_sec = 0U;
        event.clock_quality = quality;
        (void)classify(4U, 1U, 10000U);
        (void)classify(142U, 1U, 30000U);
        ready_phase(1U, 10U);
        rf_faults_ring_empty(rf_faults_head_started(), 12U);
        TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
        TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
        TEST_ASSERT_FALSE(counts(1U).is_final);
    }
}

void test_hub_restart_revokes_live_and_head_evidence_without_erasing_counts(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    ready_phase(1U, 10U);
    const uint64_t old_head = rf_faults_head_started();
    rf_faults_hub_restarted();
    rf_faults_ring_empty(old_head, 12U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
    observe_live(1U, 1002U, 20U, 0U);
    observe_live(1U, 1003U, 21U, 0U);
    rf_faults_ring_empty(rf_faults_head_started(), 22U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_late_new_phase_record_revokes_a_ready_group_before_head_reply(void)
{
    bind_phases();
    (void)classify(4U, 1U, 10000U);
    (void)classify(142U, 1U, 30000U);
    ready_phase(1U, 10U);
    const uint64_t old_head = rf_faults_head_started();
    (void)classify(4U, 2U, 15000U);
    (void)classify(142U, 2U, 45000U);
    rf_faults_ring_empty(old_head, 12U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).temporary);
}

void test_live_detected_participant_with_missing_records_remains_uncertain(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    const rf_scp_live_t active = {.source = 6U, .boot_counter = 1U,
        .uptime_sec = 10U, .seq = 10U, .state = 2U, .fault_count = 1U};
    rf_faults_live(&active, 10U);
    ready_phase(1U, 20U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(22U));
    ready_phase(2U, 30U);
    rf_faults_ring_empty(rf_faults_head_started(), 32U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
    TEST_ASSERT_FALSE(counts(1U).is_final);
}

void test_unresolved_capacity_never_overwrites_existing_fault_evidence(void)
{
    bind_phases();
    for (uint16_t index = 0U; 129U > index; index++)
    {
        event.phase = 1U;
        event.event = 1U;
        event.uptime_sec = index + 1U;
        const cp56time2a_t stamped = cp56time2a_make(0U,
            (uint8_t)(index % 60U), (uint8_t)(12U + (index / 60U)),
            7U, 3U, 10U, 26U);
        (void)memcpy(event.timestamp, &stamped, sizeof(stamped));
        TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, rf_faults_classify(&event));
    }
    TEST_ASSERT_EQUAL_UINT32(128U, counts(1U).permanent);
    TEST_ASSERT_EQUAL_UINT32(128U, counts(1U).pending);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).uncertain);
}

void test_exact_old_duplicate_does_not_recount_or_invalidate_newer_history(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    const rf_scp_event_t first = event;
    rf_faults_record_stored(&first);
    (void)classify(1U, 1U, 30000U);
    const rf_fault_stats_t before = counts(1U);
    TEST_ASSERT_EQUAL_INT(RF_FAULT_NONE, rf_faults_classify(&first));
    const rf_fault_stats_t after = counts(1U);
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(after));
}

void test_latest_1_or_7_is_the_t_mem_dead_origin_not_an_earlier_6(void)
{
    bind_phases();
    feeder_config.config.t_mem_dead_sec = 30U;
    (void)classify(6U, 1U, 10000U); /* uptime 1 */
    event.uptime_sec = 10U;
    (void)classify(1U, 1U, 20000U); /* uptime 11 */
    observe_live(1U, 40U, 10U, 0U);
    observe_live(1U, 41U, 20U, 0U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(21U));
    observe_live(1U, 42U, 30U, 0U);
    TEST_ASSERT_TRUE(rf_faults_needs_head(31U));
}

void test_live_participant_is_resolved_when_its_delayed_records_arrive(void)
{
    bind_phases();
    (void)classify(1U, 1U, 20000U);
    const rf_scp_live_t active = {.source = 6U, .boot_counter = 1U,
        .uptime_sec = 50U, .seq = 50U, .state = 2U, .fault_count = 1U};
    rf_faults_live(&active, 10U);
    (void)classify(4U, 2U, 10000U);
    (void)classify(142U, 2U, 40000U);
    ready_phase(1U, 20U);
    ready_phase(2U, 30U);
    rf_faults_ring_empty(rf_faults_head_started(), 32U);
    TEST_ASSERT_EQUAL_UINT32(1U, counts(1U).permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).temporary);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).uncertain);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_completed_capacity_is_reused_without_losing_confirmed_counts(void)
{
    bind_phases();
    for (uint16_t index = 0U; 129U > index; index++)
    {
        event.phase = 1U;
        event.event = 1U;
        event.uptime_sec = index + 1U;
        const cp56time2a_t stamped = cp56time2a_make(0U,
            (uint8_t)(index % 60U), (uint8_t)(12U + (index / 60U)),
            7U, 3U, 10U, 26U);
        (void)memcpy(event.timestamp, &stamped, sizeof(stamped));
        TEST_ASSERT_EQUAL_INT(RF_FAULT_PERMANENT, rf_faults_classify(&event));
        ready_phase(1U, 10U);
        rf_faults_ring_empty(rf_faults_head_started(), 12U);
    }
    TEST_ASSERT_EQUAL_UINT32(129U, counts(1U).permanent);
    TEST_ASSERT_EQUAL_UINT32(0U, counts(1U).uncertain);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

void test_idle_other_phase_with_delayed_records_blocks_early_finality(void)
{
    bind_phases();
    (void)classify(1U, 1U, 10000U);
    observe_live(2U, 100U, 10U, 1U);
    ready_phase(1U, 20U);
    TEST_ASSERT_FALSE(rf_faults_needs_head(22U));
    const uint64_t old_head = rf_faults_head_started();
    observe_live(2U, 101U, 30U, 0U);
    rf_faults_ring_empty(old_head, 31U);
    TEST_ASSERT_FALSE(counts(1U).is_final);
    rf_faults_ring_empty(rf_faults_head_started(), 32U);
    TEST_ASSERT_TRUE(counts(1U).is_final);
}

/*** end of file ***/
