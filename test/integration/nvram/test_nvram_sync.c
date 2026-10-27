/*
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
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
#include "rf_config.h"
#include "crc32.h"
#include "iec104_util.h"
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

/* ---- 1. Normal kullanim ---------------------------------------------- */

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

/* ---- 3. Yedek hazirlama ----------------------------------------------- */

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

/* ---- 5. RAM korunmasi (onarim flash->flash) ---------------------------- */

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


static void test_iec_default_address_regions(void)
{
    boot_virgin();
    const breaker_t *breaker = nvram_get_breaker();
    for (uint32_t i = 0U; i < MAX_POWER_LINE_COUNT; i++)
    {
        const iec104_line_config_t *line = &breaker->line[i].iec104;
        uint32_t temp = iec104_ioa_3byte_to_uint32(line->temporary_fault);
        uint32_t perm = iec104_ioa_3byte_to_uint32(line->permanent_fault);
        check(temp == (100000U + (i * 1000U)),
              "IEC default: temporary base has its own region");
        check(perm == (200000U + (i * 1000U)),
              "IEC default: permanent base has its own region");
        check(0U == line->in_use, "IEC default: line remains disabled");
        check((temp + (i * 180U) + 179U) < 200000U,
              "IEC default: temporary window does not reach permanent region");
        if ((i + 1U) < MAX_POWER_LINE_COUNT)
        {
            check((temp + (i * 180U) + 179U) <
                  (100000U + ((i + 1U) * 1180U)),
                  "IEC default: adjacent temporary windows do not overlap");
        }
    }
}

static void test_periodic_default_is_off_and_saved_interval_survives_reboot(void)
{
    boot_virgin();
    check(0U == nvram_get_iec104_config()->periodical_send_interval,
          "IEC periodic: factory default is disabled");
    nvram_get_iec104_config_rw()->periodical_send_interval = 900U;
    check(0 == nvram_sync(false), "IEC periodic: custom interval saved");
    reboot();
    check(900U == nvram_get_iec104_config()->periodical_send_interval,
          "IEC periodic: reboot preserves the saved interval");
    nvram_set_defaults();
    check(0U == nvram_get_iec104_config()->periodical_send_interval,
          "IEC periodic: factory reset disables publication");
}

static void test_factory_iec_addresses_are_unchanged(void)
{
    boot_virgin();
    const breaker_t *breaker = nvram_get_breaker();
    for (uint32_t i = 0U; i < MAX_POWER_LINE_COUNT; i++)
    {
        const iec104_line_config_t *iec = &breaker->line[i].iec104;

        for (uint32_t phase = 0U; phase < 3U; phase++)
        {
            const uint32_t iec_fields[] =
            {
                iec104_ioa_3byte_to_uint32(iec->anlik_akim[phase]),
                iec104_ioa_3byte_to_uint32(iec->enerji_varyok[phase]),
                iec104_ioa_3byte_to_uint32(iec->yuk_akimi_varyok[phase]),
                iec104_ioa_3byte_to_uint32(iec->rf_haberlesme_varyok[phase])
            };
            for (size_t field = 0U; field < 4U; field++)
            {
                check(iec_fields[field] ==
                      (1000U + (i * 100U) + (((uint32_t)field + 3U) * 10U) + phase),
                      "factory IEC: every point address unchanged");
            }
        }
    }
}
static void test_detailed_save_distinguishes_failed_and_primary_only(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(9U);
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(NVRAM_SAVE_FAILED == nvram_save(false),
          "detailed: failed A is not a verified new record");
    check(NVRAM_SAVE_COMPLETE == nvram_save(false),
          "detailed: retry repairs and saves both copies");
    nvram_set_gsm_log_level(10U);
    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(NVRAM_SAVE_PRIMARY_ONLY == nvram_save(false),
          "detailed: failed B distinguishes verified A");
    check(10U == img(MOCK_SLOT_A)->gsm_log_level,
          "detailed: verified A contains the new setting");
    reboot();
    check(10U == nvram_get_gsm_log_level(),
          "detailed: reboot selects the new setting");
    check(NVRAM_SAVE_COMPLETE == nvram_save(false),
          "detailed: repaired copies are complete");
}

static void test_rf_staged_save_with_real_flash_failures(void)
{
    boot_virgin();
    rf_store_init();
    const rf_feeder_t previous = *rf_store_get(FEEDER_1);

    check(rf_store_stage_begin(), "RF: staging begins");
    rf_store_get_mutable(FEEDER_1)->config.ia_threshold = 27.0f;
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(-1 == rf_store_sync(), "RF: A failure rejects candidate");
    check(0 == memcmp(&previous, rf_store_get(FEEDER_1), sizeof(previous)),
          "RF: A failure preserves published store");
    check(0 == memcmp(&previous, &nvram_get_breaker_rw()->line[0].rf,
                      sizeof(previous)), "RF: A failure restores mirror");
    check(0 == nvram_sync(false), "RF: later unrelated save succeeds");
    reboot();
    rf_store_init();
    check(0 == memcmp(&previous, rf_store_get(FEEDER_1), sizeof(previous)),
          "RF: later save never persists rejected candidate");
    check(rf_store_stage_begin(), "RF: second staging begins");
    rf_store_get_mutable(FEEDER_1)->config.ia_threshold = 28.0f;
    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(1 == rf_store_sync(), "RF: B failure reports primary only");
    check(28.0f == rf_store_get(FEEDER_1)->config.ia_threshold,
          "RF: B failure retains verified candidate");
    reboot();
    rf_store_init();
    check(28.0f == rf_store_get(FEEDER_1)->config.ia_threshold,
          "RF: reboot agrees with primary-only result");
    check(0 == rf_store_sync(), "RF: manual retry completes copies");
}

static void update_image_crc(uint32_t slot)
{
    nvram_t *image = img(slot);
    image->crc = crc32_finalize(crc32_update(crc32_init(), image,
        sizeof(*image) - sizeof(image->crc)));
}

static void test_invalid_header_or_crc_uses_valid_peer_without_data_loss(void)
{
    for (uint32_t slot = 0U; 2U > slot; slot++)
    {
        for (uint8_t fault = 0U; 5U > fault; fault++)
        {
            boot_virgin();
            nvram_set_gsm_log_level(1U);
            check(0 == nvram_sync(false), "header: baseline saved");
            const uint32_t sequence = img(slot)->sequence;
            switch (fault)
            {
                case 0U: img(slot)->magic ^= 1U; break;
                case 1U: img(slot)->schema_version++; break;
                case 2U: img(slot)->length = 0U; break;
                case 3U: img(slot)->length = UINT32_MAX; break;
                default: img(slot)->crc ^= 1U; break;
            }
            if (4U != fault)
            {
                update_image_crc(slot);
            }
            reboot();
            check(1U == nvram_get_gsm_log_level(),
                "header: valid peer retains the committed settings");
            check(sequence == img(MOCK_SLOT_A)->sequence,
                "header: repair keeps the committed sequence");
            check(images_equal(), "header: bad copy repaired from peer");
            check(!nvram_is_busy(), "header: repair releases the lock");
        }
    }
}

static void test_repair_interrupted_at_every_page_retains_committed_settings(void)
{
    const uint32_t pages = ((uint32_t)sizeof(nvram_t) + 255U) / 256U;
    for (uint32_t slot = 0U; 2U > slot; slot++)
    {
        for (uint32_t page = 0U; pages > page; page++)
        {
            boot_virgin();
            nvram_set_gsm_log_level(1U);
            check(0 == nvram_sync(false), "page: baseline saved");
            const uint32_t sequence = img(slot)->sequence;
            (void)memset(mock_slot_image_rw(slot), 0xFF, 4096U);
            /* The 4 KB image has at most 16 pages; the mock uses int. */
            mock_fail_page(slot, (int)page);
            reboot();
            check(1U == nvram_get_gsm_log_level(),
                "page: interrupted repair retains settings in RAM");
            check(!nvram_is_busy(), "page: interrupted repair unlocks");
            check(0 == nvram_sync(false), "page: repair retry succeeds");
            check(images_equal(), "page: retry reconstructs both images");
            check(sequence == img(MOCK_SLOT_A)->sequence,
                "page: repair does not create a new settings revision");
            reboot();
            check(1U == nvram_get_gsm_log_level(),
                "page: settings survive restart after retry");
        }
    }
}

static void test_two_invalid_images_provision_verified_defaults(void)
{
    boot_virgin();
    nvram_set_gsm_log_level(1U);
    check(0 == nvram_sync(false), "defaults: baseline saved");
    img(MOCK_SLOT_A)->magic = 0U;
    img(MOCK_SLOT_B)->magic = 0U;
    reboot();
    check(2U == nvram_get_gsm_log_level(),
        "defaults: corrupt images do not publish previous RAM settings");
    check(NVRAM_MAGIC == img(MOCK_SLOT_A)->magic,
        "defaults: provisioned image has the current magic");
    check(NVRAM_SCHEMA_VERSION == img(MOCK_SLOT_A)->schema_version,
        "defaults: provisioned image has the current schema");
    check(images_equal(), "defaults: verified copies agree");
}

static void test_rf_inventory_journal_survives_save_and_torn_clear(void)
{
    boot_virgin();
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    journal->pending = 1U;
    journal->count = 1U;
    journal->entries[0].eui64[0] = 0x11U;
    journal->entries[0].zone = 1U;
    journal->entries[0].phase = 1U;
    nvram_get_breaker_rw()->line[0].rf.r_eui64[0] = 0U;
    check(NVRAM_SAVE_COMPLETE == nvram_save(false),
          "RF journal: pending identity and desired target saved together");
    reboot();
    journal = nvram_get_rf_inventory_journal();
    check(1U == journal->pending && 1U == journal->count &&
          0x11U == journal->entries[0].eui64[0],
          "RF journal: pending delete survives power cycle");
    *journal = (rf_inventory_journal_t){0};
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(NVRAM_SAVE_FAILED == nvram_save(false),
          "RF journal: interrupted A clear is rejected");
    reboot();
    journal = nvram_get_rf_inventory_journal();
    check(1U == journal->pending,
          "RF journal: old backup keeps pending operation after torn clear");
    *journal = (rf_inventory_journal_t){0};
    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(NVRAM_SAVE_PRIMARY_ONLY == nvram_save(false),
          "RF journal: verified A clear is reported separately from B failure");
    reboot();
    check(0U == nvram_get_rf_inventory_journal()->pending && images_equal(),
          "RF journal: newest verified clear wins and repairs backup");
}

static void test_service_move_marker_and_target_survive_real_store_save_and_torn_clear(void)
{
    boot_virgin();
    rf_store_init();
    check(rf_store_stage_begin(), "service move: stage target");
    rf_feeder_t *target = rf_store_get_mutable(FEEDER_1);
    memset(target->r_eui64, 0, sizeof(target->r_eui64));
    memset(target->t_eui64, 0, sizeof(target->t_eui64));
    target->t_eui64[0] = 0x11U;
    nvram_get_rf_inventory_journal()->reserved = 0xA540U;
    check(rf_store_sync() == 0, "service move: target and marker saved together");
    check(images_equal() && (img(MOCK_SLOT_A)->schema_version == 1U) &&
          (img(MOCK_SLOT_A)->length == sizeof(nvram_t)), "service move: layout and schema unchanged");
    reboot();
    rf_store_init();
    check((nvram_get_rf_inventory_journal()->reserved == 0xA540U) &&
          (rf_store_get(FEEDER_1)->r_eui64[0] == 0U) && (rf_store_get(FEEDER_1)->t_eui64[0] == 0x11U),
          "service move: reboot restores pending marker with the new target");
    nvram_get_rf_inventory_journal()->reserved = 0U;
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_AFTER_ERASE);
    check(nvram_save(false) == NVRAM_SAVE_FAILED, "service move: torn clear reports failure");
    reboot();
    rf_store_init();
    check((nvram_get_rf_inventory_journal()->reserved == 0xA540U) &&
          (rf_store_get(FEEDER_1)->t_eui64[0] == 0x11U), "service move: old verified copy preserves the guard");
    nvram_get_rf_inventory_journal()->reserved = 0U;
    check(nvram_save(false) == NVRAM_SAVE_COMPLETE, "service move: verified clear succeeds");
    reboot();
    check(nvram_get_rf_inventory_journal()->reserved == 0U, "service move: cleared marker survives reboot");
}

static void test_modbus_config_primary_only_boot_selects_new(void)
{
    /* MB-07: a save that writes A (new baud) but fails on B leaves the
     * new image in A with a higher sequence. On reboot the A image wins
     * and the NEW baud is active - a RAM-only rollback does not undo
     * the Flash copy. */
    boot_virgin();
    {
        modbus_configs_t *cfg =
            (modbus_configs_t *)nvram_get_modbus_config();
        cfg->baud_rate = 9600U;
        cfg->device_addr = 17U;
    }
    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(NVRAM_SAVE_PRIMARY_ONLY == nvram_save(false),
          "Modbus cfg: A written, B failed -> PRIMARY_ONLY");

    reboot();
    const modbus_configs_t *active = nvram_get_modbus_config();
    check(active->baud_rate == 9600U,
          "Modbus cfg: reboot selects new baud from A");
    check(active->device_addr == 17U,
          "Modbus cfg: reboot selects new device_addr from A");
}

static void test_schema_one_rejects_old_v5_image_and_loads_defaults(void)
{
    /* WP-J: BOTH slots carry a CRC-valid schema-5 image with non-default
     * settings. After reboot the schema mismatch must reject them and
     * load factory defaults (stop_bits=1, baud=115200, addr=23). */
    boot_virgin();
    for (uint32_t slot = 0U; 2U > slot; slot++)
    {
        nvram_t *image = img(slot);
        image->schema_version = 5U;
        image->modbus_config.stop_bits = 2U;
        image->modbus_config.baud_rate = 9600U;
        image->modbus_config.device_addr = 77U;
        image->length = (uint32_t)sizeof(nvram_t);
        image->crc = crc32_finalize(crc32_update(crc32_init(),
            image, sizeof(nvram_t) - sizeof(image->crc)));
    }

    reboot();

    /* Schema 5 != 1: factory defaults must be loaded, NOT the old
     * 9600/8N2/77 values from the schema-5 images. */
    const modbus_configs_t *cfg = nvram_get_modbus_config();
    check(cfg->stop_bits == 1U,
          "v5 image: stop_bits defaults to 1, not old 2");
    check(cfg->baud_rate == 115200U,
          "v5 image: baud defaults to 115200, not old 9600");
    check(cfg->device_addr == 23U,
          "v5 image: addr defaults to 23, not old 77");
}

static void test_stop_bits_persisted_and_restored_on_reboot(void)
{
    /* WP-J: save with stop_bits=2, reboot, verify it's restored. */
    boot_virgin();
    {
        modbus_configs_t *cfg = (modbus_configs_t *)nvram_get_modbus_config();
        cfg->stop_bits = 2U;
        cfg->baud_rate = 9600U;
    }
    check(NVRAM_SAVE_COMPLETE == nvram_save(false),
          "Save with stop_bits=2 and baud=9600");

    reboot();
    const modbus_configs_t *cfg = nvram_get_modbus_config();
    check(cfg->stop_bits == 2U,
          "stop_bits=2 survives reboot");
    check(cfg->baud_rate == 9600U,
          "baud=9600 survives reboot");

    /* Change back to 1 and verify round trip. */
    ((modbus_configs_t *)nvram_get_modbus_config())->stop_bits = 1U;
    check(NVRAM_SAVE_COMPLETE == nvram_save(false), "Save with stop_bits=1");
    reboot();
    check(nvram_get_modbus_config()->stop_bits == 1U,
          "stop_bits=1 survives reboot");
}

static void test_schema_three_uses_defaults_without_migration(void)
{
    boot_virgin();
    for (uint32_t slot = 0U; 2U > slot; slot++)
    {
        nvram_t *image = img(slot);
        image->schema_version = 3U;
        image->length = 2476U;
        image->modem_config.serial_number = 12345U;
        crc32_t crc = crc32_init();
        crc = crc32_update(crc, image, 2472U);
        const uint32_t stored_crc = crc32_finalize(crc);
        memcpy((uint8_t *)image + 2472U, &stored_crc, sizeof(stored_crc));
    }
    reboot();
    check(DEVICE_DEFAULT_SERIAL_NUMBER == nvram_get_device_serial_number(),
          "Schema 3: development images reset settings as explicitly chosen");
    check(NVRAM_SCHEMA_VERSION == img(MOCK_SLOT_A)->schema_version && images_equal(),
          "Schema 3: both slots become current schema without migration");
    check(0U == nvram_get_rf_inventory_journal()->pending,
          "Schema 3: no invented pending device operation");
}

static void test_relay_states_and_retention(void)
{
    boot_virgin();
    check(nvram_retain_output_states(), "relay: retention enabled by default");
    check(0U == nvram_get_relay_states(), "relay: defaults OFF");
    for (uint8_t states = 0U; 4U > states; states++)
    {
        check(nvram_save_relay_states(states), "relay: save all combinations");
        reboot();
        check(states == nvram_get_relay_states(), "relay: reboot retains bits");
    }
    const uint32_t writes = mock_write_buff_calls(MOCK_SLOT_A);
    check(nvram_save_relay_states(3U), "relay: repeated command accepted");
    check(writes == mock_write_buff_calls(MOCK_SLOT_A),
          "relay: repeated command does not erase Flash");
    check(!nvram_save_relay_states(4U), "relay: invalid bits rejected");
    mock_fail_write(MOCK_SLOT_A, MOCK_FAIL_BEFORE_ERASE);
    check(!nvram_save_relay_states(0U), "relay: failed A rejects command");
    check(3U == nvram_get_relay_states(), "relay: failed save restores RAM");
    reboot();
    check(3U == nvram_get_relay_states(), "relay: failed save keeps old Flash");
    mock_fail_write(MOCK_SLOT_B, MOCK_FAIL_AFTER_ERASE);
    check(nvram_save_relay_states(1U), "relay: verified A accepts command");
    reboot();
    check(1U == nvram_get_relay_states(), "relay: verified A survives reboot");
    check(nvram_set_output_retention(false), "relay: disable retention");
    check(0 == nvram_sync(false), "relay: persist disabled preference");
    const uint32_t disabled_writes = mock_write_buff_calls(MOCK_SLOT_A);
    check(nvram_save_relay_states(2U), "relay: disabled tracks current RAM");
    check(disabled_writes == mock_write_buff_calls(MOCK_SLOT_A),
          "relay: disabled commands do not write Flash");
    check(nvram_set_output_retention(true), "relay: reenable with current state");
    check(0 == nvram_sync(false), "relay: save reenabled preference");
    reboot();
    check(nvram_retain_output_states() && (2U == nvram_get_relay_states()),
          "relay: enabling saves current state, not stale state");
}

static void test_previous_layout_uses_defaults(void)
{
    boot_virgin();
    uint8_t old_image[1988];
    memset(old_image, 0, sizeof(old_image));
    memcpy(old_image, mock_slot_image_rw(MOCK_SLOT_A), sizeof(nvram_t));
    const uint32_t old_schema = 1U;
    memcpy(&old_image[4], &old_schema, sizeof(old_schema));
    const uint32_t old_length = sizeof(old_image);
    memcpy(&old_image[8], &old_length, sizeof(old_length));
    const uint32_t crc = crc32_finalize(crc32_update(
        crc32_init(), old_image, sizeof(old_image) - sizeof(uint32_t)));
    memcpy(&old_image[sizeof(old_image) - sizeof(crc)], &crc, sizeof(crc));
    memcpy(mock_slot_image_rw(MOCK_SLOT_A), old_image, sizeof(old_image));
    memcpy(mock_slot_image_rw(MOCK_SLOT_B), old_image, sizeof(old_image));
    reboot();
    check(1U == NVRAM_SCHEMA_VERSION,
          "runtime stats: schema stays 1 by user decision");
    check(sizeof(nvram_t) == img(MOCK_SLOT_A)->length,
          "relay: old length reprovisions current layout");
    check(nvram_retain_output_states() && (0U == nvram_get_relay_states()),
          "relay: old CRC bytes cannot become output commands");
}

int main(void)
{
    test_relay_states_and_retention();
    test_previous_layout_uses_defaults();
    test_rf_inventory_journal_survives_save_and_torn_clear();
    test_service_move_marker_and_target_survive_real_store_save_and_torn_clear();
    test_schema_three_uses_defaults_without_migration();
    test_invalid_header_or_crc_uses_valid_peer_without_data_loss();
    test_repair_interrupted_at_every_page_retains_committed_settings();
    test_two_invalid_images_provision_verified_defaults();
    test_rf_staged_save_with_real_flash_failures();
    test_detailed_save_distinguishes_failed_and_primary_only();
    test_modbus_config_primary_only_boot_selects_new();
    test_schema_one_rejects_old_v5_image_and_loads_defaults();
    test_stop_bits_persisted_and_restored_on_reboot();
    test_factory_iec_addresses_are_unchanged();
    test_iec_default_address_regions();
    test_periodic_default_is_off_and_saved_interval_survives_reboot();
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
