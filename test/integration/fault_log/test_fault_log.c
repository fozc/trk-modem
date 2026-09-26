/*
 * Host tests for the fault_log feeder switch / dual-copy contract.
 *
 * Contract under test:
 *   - a feeder switch whose pending flush FAILS must be rejected: the
 *     unsaved RAM buffer survives, callers get a graceful error and a
 *     later retry persists the data
 *   - primary/backup selection uses the sequence marker: the copy with
 *     the valid CRC AND the higher sequence wins, so a failed primary
 *     write with a successful backup write is not shadowed by the older
 *     primary after a reload
 *   - the basic add/read/count/clear flow keeps working
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "fault_log.h"
#include "mock_platform.h"

#define SLOT_IMAGE_SIZE 4096U

static int passed = 0;
static int failed = 0;

static void check(bool cond, const char *name)
{
    if (cond) {
        printf("PASS: %s\n", name);
        passed++;
    } else {
        printf("FAIL: %s\n", name);
        failed++;
    }
}

static void boot_virgin(void)
{
    mock_reset();
    fault_log_init();
}

static void test_flush_failure_preserves_ram(void)
{
    fault_log_t log;

    boot_virgin();

    check(fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U),
          "F1: fault added to feeder 1");
    check(fault_log_get_temp_count(0U, 0U) == 1U, "F1: count 1 in RAM");

    mock_fail_primary(0U, 10);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "F1: switch to feeder 2 rejected while flush fails");
    check(fault_log_get_temp_count(0U, 0U) == 1U,
          "F1: feeder 1 RAM preserved after the rejected switch");

    mock_fail_primary(0U, 0);
    check(fault_log_get_temp_count(1U, 0U) == 0U,
          "F1: switch succeeds once the fault clears");
    check(fault_log_get_temp_count(0U, 0U) == 1U,
          "F1: feeder 1 record survived (retry flushed it)");
}

static void test_backup_newer_wins_after_reload(void)
{
    uint8_t old_primary[4096];

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);   /* flush: both copies, 1 fault */
    check(fault_log_get_temp_count(0U, 0U) == 1U, "F2: one fault stored");

    memcpy(old_primary, mock_slot_image_rw(false, 0U), sizeof(old_primary));

    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);   /* flush: both copies, 2 faults */

    /* "Primary older, backup newer": the module itself must never produce
     * this state (primary is always written first); simulate a torn
     * primary write by restoring the older image into the primary slot. */
    memcpy(mock_slot_image_rw(false, 0U), old_primary, sizeof(old_primary));

    fault_log_init();   /* cold reload */
    check(fault_log_get_temp_count(0U, 0U) == 2U,
          "F2: newer BACKUP wins after reload (the reported bug)");
}

static void test_primary_newer_still_wins(void)
{
    uint8_t old_backup[4096];

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);
    memcpy(old_backup, mock_slot_image_rw(true, 0U), sizeof(old_backup));

    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);

    /* Mirror state: stale (older) backup. */
    memcpy(mock_slot_image_rw(true, 0U), old_backup, sizeof(old_backup));

    fault_log_init();
    check(fault_log_get_temp_count(0U, 0U) == 2U,
          "F3: newer PRIMARY wins after reload");
}

static void test_clear_keeps_deleted_records_gone(void)
{
    /* Last feeder (index 6): the clear loop processes it last, so its
     * primary is written exactly once and never retried within the same
     * clear call - this is the shape of the reported repro. */
    const uint8_t f = (uint8_t)(MAX_POWER_LINE_COUNT - 1U);

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, f, 0U);
    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, f, 0U);
    (void)fault_log_get_temp_count(0U, 0U);    /* flush: both copies, 2 faults */
    check(fault_log_get_temp_count(f, 0U) == 2U, "F5: two faults stored");

    /* Backup keeps failing (non-destructively, so the STALE but valid old
     * copy survives): after clear, primary must hold the empty image with
     * the CONTINUED sequence - the stale backup must not resurrect the
     * deleted records on reload. */
    mock_fail_backup(f, 100);
    fault_log_clear();
    mock_fail_backup(f, 0);

    fault_log_init();   /* cold reload */
    check(fault_log_get_temp_count(f, 0U) == 0U,
          "F5: cleared records stay gone despite the stale backup copy");
}

static void test_primary_failure_leaves_backup_untouched(void)
{
    uint8_t backup_before[4096];
    fault_log_t log;

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    memcpy(backup_before, mock_slot_image_rw(true, 0U), sizeof(backup_before));

    uint32_t backup_writes = mock_writes_backup(0U);

    /* Destructive primary failure: the slot ends up erased (the real
     * driver's erase-phase failure). The backup must not even be
     * attempted until the primary write succeeds. */
    mock_fail_primary_destructive(0U, 1);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "F6: switch rejected while the primary write fails");
    check(mock_writes_backup(0U) == backup_writes,
          "F6: backup write not even attempted");
    check(memcmp(mock_slot_image_rw(true, 0U), backup_before,
                 sizeof(backup_before)) == 0,
          "F6: backup image untouched");
    check(fault_log_get_temp_count(0U, 0U) == 1U,
          "F6: RAM preserved despite the wiped primary");

    mock_fail_primary_destructive(0U, 0);
    (void)fault_log_get_temp_count(1U, 0U);   /* retry flush + switch */
    check(fault_log_get_temp_count(0U, 0U) == 1U,
          "F6: record survives the retry after the wiped primary");
}

static void test_basic_flow_regression(void)
{
    fault_log_t log;

    boot_virgin();

    check(fault_log_add(222.0f, 2220U, 0U, 0U, 0U, 2U, 1U),
          "F4: temporary fault added to feeder 3 phase 2");
    check(fault_log_add(223.0f, 2230U, 1U, 1U, 1U, 2U, 1U),
          "F4: permanent fault added to feeder 3 phase 2");

    check(fault_log_get_temp_count(2U, 1U) == 1U, "F4: temp count");
    check(fault_log_get_perm_count(2U, 1U) == 1U, "F4: perm count");
    check(fault_log_get_temp_count(3U, 2U) == 0U, "F4: untouched feeder empty");

    check(fault_log_read_nth(2U, 1U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "F4: read_nth newest temporary");
    check((uint32_t)log.fault_current == 2220U, "F4: current x10 roundtrip");
    check(log.info.feeder == 2U && log.info.phase == 1U, "F4: feeder/phase tags");

    fault_log_dump();   /* must not crash */

    fault_log_clear();
    check(fault_log_get_temp_count(2U, 1U) == 0U, "F4: cleared");
    check(fault_log_get_perm_count(2U, 1U) == 0U, "F4: cleared (perm)");
}

static void test_retry_after_backup_wipe_preserves_history(void)
{
    fault_log_t log;

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);    /* flush: both copies, 2 faults */

    /* Third fault: primary gets the new image, backup is wiped by a
     * destructive failure (the sole valid copy is now primary). */
    (void)fault_log_add(113.0f, 1130U, 0U, 0U, 0U, 0U, 0U);
    mock_fail_backup_destructive(0U, 1);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "F7: switch rejected while the backup write fails");

    /* The retry must NOT start from the surviving primary: with BOTH
     * writes failing, the new code must stop after the backup attempt
     * and leave the primary (sole valid copy) untouched. */
    mock_fail_backup_destructive(0U, 1);
    mock_fail_primary_destructive(0U, 1);
    uint32_t primary_writes = mock_writes_primary(0U);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "F7: retry rejected while both writes fail");
    check(mock_writes_primary(0U) == primary_writes,
          "F7: surviving primary not even attempted");

    mock_fail_backup_destructive(0U, 0);
    mock_fail_primary_destructive(0U, 0);
    (void)fault_log_get_temp_count(1U, 0U);    /* retry succeeds */

    fault_log_init();                          /* cold reload */
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "F7: all three faults survive the backup-wipe + failing retries");
}

/* ---- nvram interruption-matrix ports -------------------------------- */

/* fault_log.c FAULT_LOG_MAGIC ("TRKF") - kept in sync by hand: the value
 * lives in the module, tests only prove a valid header was written. */
#define IMG_MAGIC_EXPECTED 0x54524B46U
#define IMG_MAGIC_OFF      0U
#define IMG_SEQ_OFF        12U

static uint32_t img_u32(bool backup, uint8_t feeder, uint32_t off)
{
    uint32_t v = 0U;
    memcpy(&v, mock_slot_image_rw(backup, feeder) + off, sizeof(v));
    return v;
}

static void test_virgin_boot_writes_defaults(void)
{
    boot_virgin();   /* init on all-0xFF flash writes the empty image */

    check(img_u32(false, 0U, IMG_MAGIC_OFF) == IMG_MAGIC_EXPECTED,
          "V1: virgin boot writes a valid header to the primary");
    check(img_u32(false, 0U, IMG_SEQ_OFF) == 1U,
          "V1: virgin boot sequence starts at 1");

    uint32_t primary_writes = mock_writes_primary(0U);
    fault_log_init();   /* cold reload of already-valid slots */

    check(mock_writes_primary(0U) == primary_writes,
          "V1: valid unchanged image is not rewritten on reload");
    check(fault_log_get_temp_count(0U, 0U) == 0U,
          "V1: empty and valid after reload");
}

static void test_retry_reproduces_same_sequence(void)
{
    fault_log_t log;

    boot_virgin();

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);    /* flush: seq 2, both copies */

    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, 0U, 0U);   /* 2 faults in RAM */
    mock_fail_backup_destructive(0U, 1);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "V2: first attempt wrote A (seq 3) and lost B");

    /* Retry: the repair copies A -> B, then the retry-completion path
     * adopts A's image WITHOUT rewriting either slot - the sequence
     * must not advance a second time (nvram: retry is idempotent). */
    mock_fail_backup_destructive(0U, 0);
    uint32_t primary_writes = mock_writes_primary(0U);
    uint32_t backup_writes  = mock_writes_backup(0U);
    (void)fault_log_get_temp_count(1U, 0U);

    check(mock_writes_primary(0U) == primary_writes,
          "V2: retry-completion rewrites neither slot via the driver write");
    check(mock_writes_backup(0U) == backup_writes,
          "V2: repair uses the raw erase/program path, not the driver write");
    check(img_u32(false, 0U, IMG_SEQ_OFF) == 3U,
          "V2: sequence stays 3 (retry reproduced the same value)");
    check(img_u32(true, 0U, IMG_SEQ_OFF) == 3U,
          "V2: repaired backup carries the same sequence");

    fault_log_init();
    check(fault_log_get_temp_count(0U, 0U) == 2U,
          "V2: both faults survive");
}

/* Drive one feeder into the "A holds the new image, B is wiped" state
 * that a mid-save power cut leaves behind. */
static void force_a_new_b_wiped(void)
{
    fault_log_t log;

    (void)fault_log_add(111.0f, 1110U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_add(112.0f, 1120U, 0U, 0U, 0U, 0U, 0U);
    (void)fault_log_get_temp_count(1U, 0U);    /* flush: seq 2, both copies */

    (void)fault_log_add(113.0f, 1130U, 0U, 0U, 0U, 0U, 0U);   /* 3 faults in RAM */
    mock_fail_backup_destructive(0U, 1);
    (void)!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log);
    mock_fail_backup_destructive(0U, 0);
}

static void test_torn_repair_interrupted_twice(void)
{
    fault_log_t log;
    uint8_t erased[SLOT_IMAGE_SIZE];
    uint8_t equal_probe[SLOT_IMAGE_SIZE];

    memset(erased, 0xFF, sizeof(erased));
    boot_virgin();
    force_a_new_b_wiped();

    check(memcmp(mock_slot_image_rw(true, 0U), erased, sizeof(erased)) == 0,
          "V3: precondition - backup wiped");

    /* First interruption: the A -> B repair copy is torn mid-program
     * (first page lands, second page is cut in half). */
    mock_torn_program(0U, true, 1);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "V3: torn repair copy fails the retry");
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "V3: RAM image survives the torn repair");

    /* Second interruption (nvram matrix: interrupt the repair again). */
    mock_torn_program(0U, true, 0);
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "V3: second torn repair also fails");
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "V3: RAM image still intact");

    /* Power restored: the repair completes and both slots carry the
     * same verified image. */
    (void)fault_log_get_temp_count(1U, 0U);
    memcpy(equal_probe, mock_slot_image_rw(false, 0U), sizeof(equal_probe));
    check(memcmp(equal_probe, mock_slot_image_rw(true, 0U), sizeof(equal_probe)) == 0,
          "V3: repaired backup is byte-identical to the primary");

    fault_log_init();
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "V3: all faults survive the double-interrupted repair");
}

static void test_erase_rejection_leaves_slot_untouched(void)
{
    fault_log_t log;
    uint8_t erased[SLOT_IMAGE_SIZE];

    memset(erased, 0xFF, sizeof(erased));
    boot_virgin();
    force_a_new_b_wiped();

    /* Erase command rejected twice: the backup stays erased and the
     * primary (sole valid copy) is never touched by the repair. */
    mock_fail_erase(0U, true, 2);
    uint32_t primary_writes = mock_writes_primary(0U);

    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "V4: repair fails on the first erase rejection");
    check(!fault_log_read_nth(1U, 0U, FAULT_LOG_TYPE_TEMPORARY, 0U, &log),
          "V4: repair fails on the second erase rejection");
    check(memcmp(mock_slot_image_rw(true, 0U), erased, sizeof(erased)) == 0,
          "V4: rejected erase leaves the backup untouched");
    check(mock_writes_primary(0U) == primary_writes,
          "V4: primary never re-written during the failed repairs");
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "V4: RAM image intact");

    (void)fault_log_get_temp_count(1U, 0U);    /* third try: repair completes */
    fault_log_init();
    check(fault_log_get_temp_count(0U, 0U) == 3U,
          "V4: history survives once the erase path recovers");
}

int main(void)
{
    test_flush_failure_preserves_ram();
    test_backup_newer_wins_after_reload();
    test_primary_newer_still_wins();
    test_clear_keeps_deleted_records_gone();
    test_primary_failure_leaves_backup_untouched();
    test_retry_after_backup_wipe_preserves_history();
    test_basic_flow_regression();

    test_virgin_boot_writes_defaults();
    test_retry_reproduces_same_sequence();
    test_torn_repair_interrupted_twice();
    test_erase_rejection_leaves_slot_untouched();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
