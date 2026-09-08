/*
 * Host tests for the nvram_sync persistence contract.
 *
 * Contract under test:
 *   - a failed flash save must leave the RAM image "dirty": the next
 *     nvram_sync(false) retries the write instead of skipping it with
 *     "CRC unchanged" (the persisted-version marker is updated only
 *     after BOTH slot writes succeed)
 *   - a successful save (or a validated load at boot) marks the image
 *     clean, so an unchanged sync performs no flash writes
 *   - a partial failure (one slot OK) also stays dirty and the retry
 *     rewrites both slots
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "nvram.h"
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

/* Boot on empty (0xFF) slots: defaults are force-written, both slots
 * end up valid. */
static void boot_empty(void)
{
    mock_reset();
    check(nvram_init() == 0, "boot on empty slots: init OK");
}

static void test_failed_write_stays_dirty(void)
{
    boot_empty();

    nvram_set_gsm_log_level(5U);

    uint32_t wm = mock_writes_main();
    uint32_t wb = mock_writes_backup();
    nvram_t before_main, before_backup;

    memcpy(&before_main, mock_slot_image(false), sizeof(nvram_t));
    memcpy(&before_backup, mock_slot_image(true), sizeof(nvram_t));

    mock_fail_main(1);
    mock_fail_backup(1);
    check(nvram_sync(false) == -1, "N1: failed sync reports -1");
    check((memcmp(mock_slot_image(false), &before_main, sizeof(nvram_t)) == 0)
          && (memcmp(mock_slot_image(true), &before_backup, sizeof(nvram_t)) == 0),
          "N1: flash content untouched by the failed save");
    check((mock_writes_main() == (wm + 1U)) && (mock_writes_backup() == (wb + 1U)),
          "N1: both slots attempted once (counters include failures)");

    /* The reported bug: this retry used to skip ("CRC unchanged"). */
    check(nvram_sync(false) == 0, "N1: retry after failure succeeds");
    check((mock_writes_main() == (wm + 2U)) && (mock_writes_backup() == (wb + 2U)),
          "N1: retry actually wrote both slots");
    check(memcmp(mock_slot_image(false), &before_main, sizeof(nvram_t)) != 0,
          "N1: flash image changed by the retry");

    const nvram_t *img = (const nvram_t *)mock_slot_image(false);
    check(img->gsm_log_level == 5U, "N1: setting persisted in flash");
}

static void test_clean_sync_skips(void)
{
    boot_empty();

    nvram_set_rf_log_level(3U);
    check(nvram_sync(false) == 0, "N2: changed sync succeeds");

    uint32_t wm = mock_writes_main();
    uint32_t wb = mock_writes_backup();

    check(nvram_sync(false) == 0, "N2: clean sync returns 0");
    check((mock_writes_main() == wm) && (mock_writes_backup() == wb),
          "N2: clean sync performs no flash writes");
}

static void test_loaded_state_is_clean(void)
{
    boot_empty();

    nvram_set_iec104_log_level(4U);
    check(nvram_sync(false) == 0, "N3: change persisted");

    /* Power cycle: the saved slot becomes the loaded baseline. */
    check(nvram_init() == 0, "N3: re-init loads the saved slot");
    check(nvram_get_iec104_log_level() == 4U, "N3: loaded value intact");

    uint32_t wm = mock_writes_main();
    uint32_t wb = mock_writes_backup();

    check(nvram_sync(false) == 0, "N3: loaded state is clean");
    check((mock_writes_main() == wm) && (mock_writes_backup() == wb),
          "N3: no writes right after load");
}

static void test_partial_failure_retries_both(void)
{
    boot_empty();

    nvram_set_gsm_log_level(7U);

    uint32_t wm = mock_writes_main();
    uint32_t wb = mock_writes_backup();
    nvram_t before_backup;

    memcpy(&before_backup, mock_slot_image(true), sizeof(nvram_t));

    mock_fail_backup(1);
    check(nvram_sync(false) == -1, "N4: partial failure reports -1");
    check((mock_writes_main() == (wm + 1U)) && (mock_writes_backup() == (wb + 1U)),
          "N4: main written, backup attempt failed");
    check(memcmp(mock_slot_image(true), &before_backup, sizeof(nvram_t)) == 0,
          "N4: backup content untouched by the failed attempt");

    check(nvram_sync(false) == 0, "N4: retry succeeds");
    check((mock_writes_main() == (wm + 2U)) && (mock_writes_backup() == (wb + 2U)),
          "N4: retry rewrote BOTH slots");

    check(memcmp(mock_slot_image(false), mock_slot_image(true),
                 sizeof(nvram_t)) == 0,
          "N4: slots consistent after retry");
}

int main(void)
{
    test_failed_write_stays_dirty();
    test_clean_sync_skips();
    test_loaded_state_is_clean();
    test_partial_failure_retries_both();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
