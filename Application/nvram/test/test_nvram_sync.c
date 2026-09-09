/*
 * Host tests for the two-slot NVRAM core (doc/nvram.md).
 *
 * Contract under test:
 *   - A is never erased before B holds a verified copy of the current image
 *   - a successful save ends with A and B carrying the same verified image
 *   - every interruption leaves at least one valid image; the newest valid
 *     image wins on reload; mixed/torn images are rejected
 *   - repairs are flash-to-flash: unsaved RAM settings survive them
 *   - sequence advances only on new records (from the last valid flash
 *     version); repair-only and factory reset keep it; wrap is safe
 *   - the save lock is released on every exit path
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "nvram.h"
#include "crc32.h"
#include "mock_platform.h"

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

/* ---- helpers --------------------------------------------------------- */

static void boot_virgin(void)
{
    mock_reset();
    check(nvram_init() == 0, "boot: virgin init OK");
}

/* Power-cycle simulation: module state restarts, flash images stay. */
static void reboot(void)
{
    check(nvram_init() == 0, "reboot: init OK");
}

static nvram_t *img(uint32_t slot)
{
    return (nvram_t *)mock_slot_image_rw(slot);
}

static bool images_equal(void)
{
    return memcmp(mock_slot_image_rw(MOCK_SLOT_A),
                  mock_slot_image_rw(MOCK_SLOT_B),
                  sizeof(nvram_t)) == 0;
}

/* Patch an image's sequence field and mend the stored CRC so the image
 * stays valid (used for the wrap test). */
static void craft_sequence(uint32_t slot, uint32_t seq)
{
    nvram_t *image = img(slot);
    crc32_t crc;

    image->sequence = seq;
    image->length = (uint32_t)sizeof(nvram_t);
    crc = crc32_init();
    crc = crc32_update(crc, image, sizeof(nvram_t) - sizeof(image->crc));
    image->crc = crc32_finalize(crc);
}

/* ---- 1. Normal kullanım ---------------------------------------------- */

static void test_normal_flow(void)
{
    boot_virgin();

    check(images_equal(), "T1: virgin defaults written to both slots");
    check(img(MOCK_SLOT_A)->sequence == 1U, "T1: provisioning starts at seq 1");
    check(nvram_get_gsm_log_level() == 2U, "T1: defaults readable");

    nvram_set_gsm_log_level(5U);
    check(nvram_sync(false) == 0, "T2: save succeeds");
    check(images_equal(), "T2: both slots carry the new image");
    check(img(MOCK_SLOT_A)->sequence == 2U, "T2: sequence advanced");
    check(img(MOCK_SLOT_A)->gsm_log_level == 5U, "T2: value stored");

    uint32_t wa = mock_write_buff_calls(MOCK_SLOT_A);
    uint32_t wb = mock_write_buff_calls(MOCK_SLOT_B);
    uint32_t ea = mock_erase_calls(MOCK_SLOT_A);

    check(nvram_sync(false) == 0, "T3: unchanged sync returns 0");
    check((mock_write_buff_calls(MOCK_SLOT_A) == wa)
          && (mock_write_buff_calls(MOCK_SLOT_B) == wb)
          && (mock_erase_calls(MOCK_SLOT_A) == ea),
          "T3: unchanged data performs no writes");
}

/* ---- 2. Kurtarma ------------------------------------------------------ */

static void test_recovery_only_a(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(5U);
    (void)nvram_sync(false);

    uint32_t seq_before = img(MOCK_SLOT_A)->sequence;
    memset(mock_slot_image_rw(MOCK_SLOT_B), 0xFF, 4096U);

    uint32_t wa = mock_write_buff_calls(MOCK_SLOT_A);
    uint32_t ea = mock_erase_calls(MOCK_SLOT_A);

    reboot();
    check(nvram_get_gsm_log_level() == 5U, "T4: values loaded from A");
    check(images_equal(), "T4: boot repaired B from A");
    check((mock_write_buff_calls(MOCK_SLOT_A) == wa)
          && (mock_erase_calls(MOCK_SLOT_A) == ea),
          "T4: repair did not touch A");
    check(mock_page_writes(MOCK_SLOT_B) > 0U, "T4: B rebuilt page by page");
    check(img(MOCK_SLOT_A)->sequence == seq_before,
          "T4: repair kept the sequence");
}

static void test_recovery_only_b(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(6U);
    (void)nvram_sync(false);

    uint32_t seq_before = img(MOCK_SLOT_B)->sequence;
    memset(mock_slot_image_rw(MOCK_SLOT_A), 0xFF, 4096U);

    uint32_t wb = mock_write_buff_calls(MOCK_SLOT_B);

    reboot();
    check(nvram_get_gsm_log_level() == 6U, "T5: values loaded from B");
    check(images_equal(), "T5: boot repaired A from B");
    check(mock_write_buff_calls(MOCK_SLOT_B) == wb,
          "T5: repair did not touch B");
    check(img(MOCK_SLOT_A)->sequence == seq_before,
          "T5: repair kept the sequence");
}

static void test_recovery_none(void)
{
    mock_reset();
    reboot();
    check(nvram_get_gsm_log_level() == 2U, "T6: defaults on empty flash");
    check(images_equal(), "T6: defaults written to both slots");
    check(img(MOCK_SLOT_A)->sequence == 1U, "T6: fresh sequence");
}

/* ---- 3. Yedek hazırlama ----------------------------------------------- */

static void test_backup_prepared_before_a(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(5U);
    (void)nvram_sync(false);

    nvram_t old_image;
    memcpy(&old_image, mock_slot_image_rw(MOCK_SLOT_A), sizeof(nvram_t));

    /* B is stale/wiped and the A write will fail: the core must repair B
     * from A FIRST - so the good copy is never left without a twin. */
    memset(mock_slot_image_rw(MOCK_SLOT_B), 0xFF, 4096U);
    nvram_set_gsm_log_level(7U);
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_BEFORE_ERASE);

    check(nvram_sync(false) == -1, "T7: save rejected when A write fails");
    check(memcmp(mock_slot_image_rw(MOCK_SLOT_B), &old_image, sizeof(nvram_t)) == 0,
          "T7: B repaired to the old verified image before A was touched");
    check(memcmp(mock_slot_image_rw(MOCK_SLOT_A), &old_image, sizeof(nvram_t)) == 0,
          "T7: A untouched by the rejected save");
    check(nvram_get_gsm_log_level() == 7U, "T7: RAM value preserved");

    check(nvram_sync(false) == 0, "T7: retry succeeds");
    check(images_equal() && (img(MOCK_SLOT_A)->gsm_log_level == 7U),
          "T7: retry persisted the new value to both slots");
}

/* ---- 4. Hata ve retry -------------------------------------------------- */

static void test_retry_after_a_failure(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(5U);

    uint32_t eb = mock_erase_calls(MOCK_SLOT_B);

    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(nvram_sync(false) == -1, "T8: A failure reports -1");
    check(!nvram_is_busy(), "T8: lock released on failure");
    check(mock_erase_calls(MOCK_SLOT_B) == eb,
          "T8: B never erased while A failed");
    check(nvram_get_gsm_log_level() == 5U, "T8: RAM value preserved");

    check(nvram_sync(false) == 0, "T8: retry succeeds");
    check(images_equal(), "T8: both slots consistent after retry");
    check(!nvram_is_busy(), "T8: lock released on success");
}

static void test_retry_after_b_failure(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(9U);

    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(nvram_sync(false) == -1, "T9: B failure reports -1");
    check(img(MOCK_SLOT_A)->gsm_log_level == 9U,
          "T9: A holds the new verified image");
    check(!nvram_is_busy(), "T9: lock released on failure");

    uint32_t wa = mock_write_buff_calls(MOCK_SLOT_A);
    uint32_t wb = mock_write_buff_calls(MOCK_SLOT_B);

    check(nvram_sync(false) == 0, "T9: retry completes");
    check((mock_write_buff_calls(MOCK_SLOT_A) == wa)
          && (mock_write_buff_calls(MOCK_SLOT_B) == wb),
          "T9: retry rewrote NEITHER slot (repair + completion)");
    check(mock_page_writes(MOCK_SLOT_B) > 0U,
          "T9: missing twin rebuilt page by page from A");
    check(images_equal(), "T9: slots consistent");
}

/* ---- 5. RAM korunması (onarım flash->flash) ---------------------------- */

static void test_ram_preserved_during_repair(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(4U);
    (void)nvram_sync(false);

    nvram_set_gsm_log_level(8U);                    /* unsaved change */
    memset(mock_slot_image_rw(MOCK_SLOT_B), 0xFF, 4096U);

    uint32_t wa = mock_write_buff_calls(MOCK_SLOT_A);

    mock_fail_erase(MOCK_SLOT_B);
    check(nvram_sync(false) == -1, "T11: repair failure reports -1");
    check(nvram_get_gsm_log_level() == 8U,
          "T11: unsaved RAM settings survived the failed repair");
    check(mock_write_buff_calls(MOCK_SLOT_A) == wa,
          "T11: A not written while its twin was broken");

    check(nvram_sync(false) == 0, "T11: retry repairs then saves");
    check(images_equal() && (img(MOCK_SLOT_A)->gsm_log_level == 8U),
          "T11: unsaved change persisted after repair");
}

/* ---- 6. Sequence ------------------------------------------------------- */

static void test_sequence_behaviour(void)
{
    boot_virgin();

    nvram_set_gsm_log_level(3U);
    (void)nvram_sync(false);
    nvram_set_rf_log_level(3U);
    (void)nvram_sync(false);

    check(img(MOCK_SLOT_A)->sequence == 3U, "T12: sequence advances per save");

    /* Repair-only: corrupt B, save unchanged data - sequence must stay. */
    uint32_t seq_before = img(MOCK_SLOT_A)->sequence;
    memset(mock_slot_image_rw(MOCK_SLOT_B), 0xFF, 4096U);
    check(nvram_sync(false) == 0, "T13: repair-only sync succeeds");
    check(images_equal(), "T13: twin rebuilt");
    check(img(MOCK_SLOT_A)->sequence == seq_before,
          "T13: repair-only does not advance the sequence");
}

static void test_factory_reset_keeps_sequence(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(7U);
    check(nvram_sync(false) == 0, "T14: custom settings stored");
    uint32_t seq_before = img(MOCK_SLOT_A)->sequence;

    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    nvram_set_defaults();
    check(nvram_sync(true) == -1, "T14: partial factory-reset write fails");

    reboot();
    check(nvram_get_gsm_log_level() == 2U,
          "T14: defaults loaded, not the stale settings");
    check(img(MOCK_SLOT_A)->sequence == (seq_before + 1U),
          "T14: defaults continue the sequence");
}

static void test_sequence_wrap(void)
{
    boot_virgin();

    craft_sequence(MOCK_SLOT_A, 0xFFFFFFFEU);
    craft_sequence(MOCK_SLOT_B, 0xFFFFFFFEU);
    reboot();

    nvram_set_gsm_log_level(3U);
    check(nvram_sync(false) == 0, "T15: save at seq max-1");
    check(img(MOCK_SLOT_A)->sequence == 0xFFFFFFFFU, "T15: seq reaches max");

    nvram_set_rf_log_level(3U);
    check(nvram_sync(false) == 0, "T15: save wrapping past max");
    check(img(MOCK_SLOT_A)->sequence == 0U, "T15: sequence wrapped to 0");

    reboot();
    check(nvram_get_gsm_log_level() == 3U,
          "T15: wrapped image reloads correctly");
}

/* ---- 7. Kesinti matrisi ------------------------------------------------ */

static void test_torn_copy_rejected_and_repaired(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(5U);
    (void)nvram_sync(false);

    /* Torn B: boot repair interrupted mid-copy (page 4 of 10). */
    memset(mock_slot_image_rw(MOCK_SLOT_B), 0xFF, 4096U);
    mock_fail_page(MOCK_SLOT_B, 4);

    reboot();
    check(nvram_get_gsm_log_level() == 5U,
          "T10: settings readable while the twin is torn");
    check(!images_equal(), "T10: torn twin not trusted");

    /* Second interruption during the next repair attempt. */
    mock_fail_page(MOCK_SLOT_B, 7);
    reboot();
    check(nvram_get_gsm_log_level() == 5U,
          "T10: settings still readable after the second interruption");

    reboot();
    check(images_equal(), "T10: repair finally completes");
    check(nvram_get_gsm_log_level() == 5U, "T10: values intact");
}

static void test_mixed_image_rejected(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(5U);
    (void)nvram_sync(false);

    /* Single-byte corruption in the middle of B: CRC must reject it. */
    uint8_t *b = mock_slot_image_rw(MOCK_SLOT_B);
    b[sizeof(nvram_t) / 2U] ^= 0x01U;

    reboot();
    check(nvram_get_gsm_log_level() == 5U,
          "T16: mixed image rejected, good copy loaded");
    check(images_equal(), "T16: corrupt twin rebuilt");
}

static void test_retry_reuses_sequence(void)
{
    boot_virgin();

    uint32_t last_good = img(MOCK_SLOT_A)->sequence;   /* 1 */

    nvram_set_gsm_log_level(4U);
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(nvram_sync(false) == -1, "T17: A failure reports -1");

    check(nvram_sync(false) == 0, "T17: retry succeeds");
    check(img(MOCK_SLOT_A)->sequence == (last_good + 1U),
          "T17: retry reused the same next sequence (no double bump)");
    check(images_equal(), "T17: slots consistent");
}

int main(void)
{
    test_normal_flow();
    test_recovery_only_a();
    test_recovery_only_b();
    test_recovery_none();
    test_backup_prepared_before_a();
    test_retry_after_a_failure();
    test_retry_after_b_failure();
    test_ram_preserved_during_repair();
    test_sequence_behaviour();
    test_factory_reset_keeps_sequence();
    test_sequence_wrap();
    test_torn_copy_rejected_and_repaired();
    test_mixed_image_rejected();
    test_retry_reuses_sequence();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
