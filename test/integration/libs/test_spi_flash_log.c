/*
 * Host tests for the spi_flash_log append-only ring library.
 *
 * Flash model: a RAM array with real NOR semantics - program can only
 * clear bits (existing & new), erase sets a 4096-byte sector back to
 * 0xFF. A canary region behind the configured log area catches any
 * write that leaves the area.
 *
 * Fault injection knobs:
 *   - fail_prog_addr / fail_prog_silent: one-shot program failure at an
 *     exact address; silent = the flash array stays untouched
 *   - fail_erase_once: one-shot erase failure
 *
 * Baseline tests (T*) must always pass. Reproduction tests (R*) map to
 * the library findings and are expected to FAIL until the fixes land:
 *   R1: a program/erase failure at the last slot of the last sector
 *       must never let a later write leave the log area
 *   R2: a program failure that leaves the slot all-0xFF must not hide
 *       later records from the boot scan, and next_seq must not regress
 *   R3: an erase failure on the sector transition must be retryable
 *       without writing outside the area
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "spi_flash_log.h"
#include "bsp.h"

/* spi_flash_log.c uzun taramalarda wdt tekmeleme cagirir; host testinde
 * bakilacak bir watchdog yok - bos yutucu stub yeterli. */
void bsp_kick_wdt(void)
{
}

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

/* ------------------------------------------------------------------ */
/* NOR flash simulator                                                */
/* ------------------------------------------------------------------ */

/* Same geometry as the elog context: 24-byte payload, 28-byte entries. */
#define PAYLOAD   24U
#define ENTRY     (LOG_ENTRY_OVERHEAD + PAYLOAD)      /* 28 */
#define EPS       (LOG_SECTOR_SIZE / ENTRY)           /* entries per sector */

#define SIM_SECTORS   4U
#define SIM_AREA_SIZE (SIM_SECTORS * LOG_SECTOR_SIZE)
#define SIM_TOTAL     (SIM_AREA_SIZE + 256U)          /* + canary */

static uint8_t sim_flash[SIM_TOTAL];

static uint32_t sim_area_end;        /* [0, sim_area_end) is the log area */
static bool     sim_out_of_area;     /* a write left the configured area */
static bool     sim_page_crossed;    /* a program spanned a 256 B page */
static uint32_t fail_prog_addr;      /* 0 = off; fails at exact addr */
static uint32_t fail_prog_count;     /* consecutive failures at that addr */
static uint32_t fail_prog_silent_count; /* first N of those leave flash untouched */
static bool     fail_read_once;      /* next read fails once */
static bool     fail_erase_once;     /* next erase fails once */

static void sim_reset(void)
{
    memset(sim_flash, 0xFF, sizeof(sim_flash));
    sim_out_of_area       = false;
    sim_page_crossed      = false;
    fail_prog_addr        = 0U;
    fail_prog_count       = 0U;
    fail_prog_silent_count = 0U;
    fail_read_once        = false;
    fail_erase_once       = false;
}

static bool canary_intact(void)
{
    for (uint32_t i = sim_area_end; i < SIM_TOTAL; i++) {
        if (sim_flash[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

static int sim_read(uint32_t addr, void *buf, size_t len)
{
    if (fail_read_once) {
        fail_read_once = false;      /* one-shot */
        return -1;
    }
    if (((uint32_t)len > SIM_TOTAL) || (addr > SIM_TOTAL - (uint32_t)len)) {
        return -1;
    }
    memcpy(buf, &sim_flash[addr], len);
    return 0;
}

static int sim_program(uint32_t addr, const void *buf, size_t len)
{
    const uint8_t *src = buf;

    if (((uint32_t)len > SIM_TOTAL) || (addr > SIM_TOTAL - (uint32_t)len)) {
        sim_out_of_area = true;      /* beyond the modeled chip */
        return -1;
    }
    if (addr + (uint32_t)len > sim_area_end) {
        sim_out_of_area = true;      /* left the configured log area */
    }
    if (((addr % LOG_FLASH_PAGE_SIZE) + (uint32_t)len) > LOG_FLASH_PAGE_SIZE) {
        sim_page_crossed = true;     /* real chip would wrap the page */
    }

    bool injected = (fail_prog_addr != 0U) && (addr == fail_prog_addr)
                    && (fail_prog_count > 0U);
    if (injected) {
        fail_prog_count--;           /* armed for N consecutive calls */
    }
    if (injected && (fail_prog_silent_count > 0U)) {
        fail_prog_silent_count--;
        return -1;                   /* driver failed, flash untouched */
    }

    for (uint32_t i = 0U; i < (uint32_t)len; i++) {
        sim_flash[addr + i] &= src[i];   /* NOR: only 1 -> 0 */
    }
    return injected ? -1 : 0;
}

static int sim_erase_sector(uint32_t sector_addr)
{
    if (fail_erase_once) {
        fail_erase_once = false;     /* one-shot */
        return -1;
    }
    if ((sector_addr % LOG_SECTOR_SIZE) != 0U) {
        return -1;
    }
    if (sector_addr >= sim_area_end) {
        sim_out_of_area = true;
        return -1;
    }
    memset(&sim_flash[sector_addr], 0xFF, LOG_SECTOR_SIZE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Library context helpers                                            */
/* ------------------------------------------------------------------ */

static log_ctx_t ctx;

static void ctx_setup(uint32_t sector_count)
{
    log_config_t cfg;

    memset(&ctx, 0, sizeof(ctx));
    memset(&cfg, 0, sizeof(cfg));

    sim_area_end = sector_count * LOG_SECTOR_SIZE;
    cfg.base_addr    = 0U;
    cfg.sector_count = sector_count;
    cfg.payload_size = PAYLOAD;
    cfg.ops.read         = sim_read;
    cfg.ops.program      = sim_program;
    cfg.ops.erase_sector = sim_erase_sector;

    check(log_init(&ctx, &cfg) == LOG_OK, "ctx_setup: log_init OK");
}

/* Boot scan on the same flash contents (power cycle simulation). */
static void reboot_ctx(void)
{
    log_config_t cfg;

    memset(&ctx, 0, sizeof(ctx));
    memset(&cfg, 0, sizeof(cfg));

    cfg.base_addr    = 0U;
    cfg.sector_count = sim_area_end / LOG_SECTOR_SIZE;
    cfg.payload_size = PAYLOAD;
    cfg.ops.read         = sim_read;
    cfg.ops.program      = sim_program;
    cfg.ops.erase_sector = sim_erase_sector;

    check(log_init(&ctx, &cfg) == LOG_OK, "reboot: log_init OK");
}

/* ------------------------------------------------------------------ */
/* Payload helpers and record collector                               */
/* ------------------------------------------------------------------ */

static void make_payload(uint8_t *dst, uint32_t seed)
{
    for (uint32_t i = 0U; i < PAYLOAD; i++) {
        dst[i] = (uint8_t)((seed * 13U) + i + 1U);
    }
}

static bool payload_matches(const uint8_t *got, uint32_t seed)
{
    uint8_t want[PAYLOAD];

    make_payload(want, seed);
    return memcmp(got, want, PAYLOAD) == 0;
}

#define MAX_COLLECT 512U

typedef struct {
    uint32_t seq;
    uint8_t  payload[PAYLOAD];
} rec_t;

static rec_t    recs[MAX_COLLECT];
static uint32_t rec_n;

static void collect_visit(const void *payload, uint32_t payload_size,
                          uint32_t seq, void *user_ctx)
{
    (void)payload_size;
    (void)user_ctx;

    if (rec_n < MAX_COLLECT) {
        recs[rec_n].seq = seq;
        memcpy(recs[rec_n].payload, payload, PAYLOAD);
    }
    rec_n++;
}

static log_status_t write_one(uint32_t seed)
{
    uint8_t p[PAYLOAD];

    make_payload(p, seed);
    return log_write(&ctx, p);
}

static uint32_t read_all_collect(void)
{
    rec_n = 0U;
    if (log_read_all(&ctx, collect_visit, NULL) != LOG_OK) {
        return 0U;
    }
    return rec_n;
}

static uint32_t head_addr(void)
{
    return log_get_write_sector_index(&ctx) * LOG_SECTOR_SIZE
           + log_get_write_offset(&ctx);
}

/* Arm consecutive program failures at addr: the first silent_count calls
 * fail without touching the array, the remaining ones land the data and
 * THEN report an error (timeout-after-write model). */
static void arm_prog_fail_mixed(uint32_t addr, uint32_t silent_count,
                                uint32_t count)
{
    fail_prog_addr         = addr;
    fail_prog_count        = count;
    fail_prog_silent_count = silent_count;
}

/* All armed failures silent. */
static void arm_prog_fail(uint32_t addr, bool silent, uint32_t count)
{
    arm_prog_fail_mixed(addr, silent ? count : 0U, count);
}

/* ------------------------------------------------------------------ */
/* Baseline tests                                                     */
/* ------------------------------------------------------------------ */

static void test_init_virgin(void)
{
    sim_reset();
    ctx_setup(2U);

    check(log_is_initialized(&ctx), "T1: initialized");
    check(log_get_next_seq(&ctx) == 0U, "T1: virgin next_seq is 0");
    check(log_get_write_sector_index(&ctx) == 0U, "T1: head in sector 0");
    check(log_get_write_offset(&ctx) == 0U, "T1: head offset 0");
    check(read_all_collect() == 0U, "T1: no records readable");
}

static void test_write_read_all(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < 5U; i++) {
        check(write_one(i) == LOG_OK, "T2: write accepted");
    }

    read_all_collect();
    check(rec_n == 5U, "T2: read_all sees 5 records");
    bool seqs_ok = true;
    bool data_ok = true;
    for (uint32_t i = 0U; i < rec_n; i++) {
        seqs_ok = seqs_ok && (recs[i].seq == i);
        data_ok = data_ok && payload_matches(recs[i].payload, recs[i].seq);
    }
    check(seqs_ok, "T2: seqs chronological 0..4");
    check(data_ok, "T2: payloads intact");
}

static void test_read_last_paging(void)
{
    log_page_ctx_t page;
    uint32_t got;

    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < 10U; i++) {
        (void)write_one(i);
    }

    memset(&page, 0, sizeof(page));
    rec_n = 0U;
    got = 0U;
    (void)log_read_last(&ctx, 4U, collect_visit, NULL, &page);
    got = rec_n;
    check((got == 4U) && (page.page_count == 4U) && page.has_more,
          "T3: first page newest 4, has_more");
    check((recs[0].seq == 9U) && (recs[3].seq == 6U),
          "T3: first page ordered newest first");

    rec_n = 0U;
    (void)log_read_last(&ctx, 4U, collect_visit, NULL, &page);
    check((rec_n == 4U) && (recs[0].seq == 5U) && (recs[3].seq == 2U),
          "T3: second page continues older");

    rec_n = 0U;
    (void)log_read_last(&ctx, 4U, collect_visit, NULL, &page);
    check((rec_n == 2U) && (recs[0].seq == 1U) && (recs[1].seq == 0U)
          && !page.has_more,
          "T3: final page reaches the oldest");
}

static void test_wrap_two_sectors(void)
{
    sim_reset();
    ctx_setup(2U);

    /* 146 entries fill sector 0, the next 100 land in sector 1. */
    for (uint32_t i = 0U; i < (EPS + 100U); i++) {
        if (write_one(i) != LOG_OK) {
            check(false, "T4: every write accepted");
            return;
        }
    }
    check(true, "T4: every write accepted");

    read_all_collect();
    check(rec_n == (EPS + 100U), "T4: wrapped read_all sees all 246");
    check((recs[0].seq == 0U) && (recs[rec_n - 1U].seq == (EPS + 99U)),
          "T4: wrap keeps chronological order");

    bool data_ok = true;
    for (uint32_t i = 0U; i < rec_n; i++) {
        data_ok = data_ok && payload_matches(recs[i].payload, recs[i].seq);
    }
    check(data_ok, "T4: payloads intact across straddling entries");
    check(!sim_page_crossed, "T4: no program crossed a page boundary");
    check(!sim_out_of_area && canary_intact(), "T4: stayed in area");
}

static void test_torn_slot_skipped(void)
{
    sim_reset();
    ctx_setup(2U);

    /* One valid record, then simulate a power cut during the payload
     * phase of the next write: seq + payload land, CRC never does. */
    (void)write_one(0U);

    uint8_t torn[ENTRY];
    uint16_t torn_seq = 1U;
    memset(torn, 0xFF, sizeof(torn));
    memcpy(torn, &torn_seq, LOG_SEQ_SIZE);
    make_payload(&torn[LOG_SEQ_SIZE], 1U);
    (void)sim_program(head_addr(), torn, LOG_SEQ_SIZE + PAYLOAD);

    reboot_ctx();
    check(log_get_write_offset(&ctx) == 2U * ENTRY,
          "T5: reboot head placed after the torn hole");

    (void)write_one(1U);
    reboot_ctx();

    read_all_collect();
    check(rec_n == 2U, "T5: hole skipped, both records visible");
    check(log_get_next_seq(&ctx) == 2U, "T5: next_seq continues past hole");
}

/* ------------------------------------------------------------------ */
/* Reproduction tests for the reported findings                       */
/* ------------------------------------------------------------------ */

/*
 * T6 (finding 2, double fault): a silent program failure followed by a
 * failed poison attempt. The head must stay on the same slot so the next
 * write retries it in place - no EMPTY hole may be created behind the
 * head, and the slot is only consumed once a record really lands there.
 */
static void test_poison_fail_keeps_head(void)
{
    sim_reset();
    ctx_setup(2U);

    (void)write_one(0U);            /* record A */

    /* Fail the payload phase AND the poison byte at the same slot. */
    arm_prog_fail(head_addr(), true, 2U);
    check(write_one(1U) == LOG_ERR_FLASH_PROGRAM,
          "T6: double program failure reported");
    check(log_get_write_offset(&ctx) == ENTRY,
          "T6: head stayed on the failed slot");
    fail_prog_addr = 0U;

    check(write_one(2U) == LOG_OK, "T6: retry lands on the same slot");
    check(log_get_write_offset(&ctx) == 2U * ENTRY,
          "T6: head advanced by exactly one slot");

    reboot_ctx();
    read_all_collect();
    check(rec_n == 2U, "T6: both records visible after reboot");
    check(log_get_next_seq(&ctx) == 2U, "T6: next_seq did not regress");
    check(!sim_out_of_area && canary_intact(), "T6: stayed in area");
}

/*
 * T7 (lazy erase): filling a sector must NOT erase the next one. The
 * oldest records survive until a new record actually needs their space.
 */
static void test_erase_deferred_until_needed(void)
{
    sim_reset();
    ctx_setup(2U);

    /* Fill the whole ring: both sectors hold records, none erased. */
    for (uint32_t i = 0U; i < (2U * EPS); i++) {
        if (write_one(i) != LOG_OK) {
            check(false, "T7: full ring accepted");
            return;
        }
    }
    check(true, "T7: full ring accepted");
    check(read_all_collect() == (2U * EPS),
          "T7: idle full ring preserves all records");
    check(log_get_write_sector_index(&ctx) == 1U,
          "T7: head waits at the full sector");
    check(!sim_out_of_area && canary_intact(), "T7: stayed in area");

    /* The next record reclaims the oldest sector. */
    check(write_one(800U) == LOG_OK, "T7: next write after full ring");
    check(log_get_write_sector_index(&ctx) == 0U,
          "T7: head moved by the erase-at-need");
    read_all_collect();
    check(rec_n == (EPS + 1U), "T7: oldest sector reclaimed");
    check(recs[0].seq == EPS, "T7: oldest survivor is the second sector");
}

/*
 * T8 (lazy erase, boot): a power cycle with a completely full ring must
 * keep every record and continue the numbering - the boot scan must not
 * eagerly erase the oldest sector either.
 */
static void test_boot_full_ring_preserves_all(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < (2U * EPS); i++) {
        (void)write_one(i);
    }

    reboot_ctx();
    check(log_get_next_seq(&ctx) == (2U * EPS),
          "T8: numbering survives the reboot");
    check(read_all_collect() == (2U * EPS),
          "T8: boot keeps every record of the full ring");

    check(write_one(900U) == LOG_OK, "T8: writing continues after boot");
    check(read_all_collect() == (EPS + 1U),
          "T8: next record reclaims the oldest sector");
}

/*
 * T9 (chronological tail read): log_read_tail returns the newest n
 * entries oldest -> newest, clamps overshoot to the stored count and
 * rejects invalid arguments.
 */
static void test_read_tail_window(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < 40U; i++) {
        (void)write_one(i);
    }

    rec_n = 0U;
    check(log_read_tail(&ctx, 5U, collect_visit, NULL) == LOG_OK,
          "T9: tail read OK");
    check(rec_n == 5U, "T9: tail returns exactly the newest 5");
    bool seqs_ok = true;
    for (uint32_t i = 0U; i < rec_n; i++) {
        seqs_ok = seqs_ok && (recs[i].seq == (35U + i));
    }
    check(seqs_ok, "T9: tail order is oldest -> newest (35..39)");
    check(payload_matches(recs[0].payload, 35U), "T9: payload matches window");

    rec_n = 0U;
    (void)log_read_tail(&ctx, 1000U, collect_visit, NULL);
    check(rec_n == 40U, "T9: overshoot clamps to stored count");
    check((recs[0].seq == 0U) && (recs[39U].seq == 39U),
          "T9: overshoot yields the full chronological log");

    check(log_read_tail(&ctx, 0U, collect_visit, NULL) == LOG_ERR_INVALID_PARAM,
          "T9: count 0 rejected");
    check(log_read_tail(&ctx, 5U, NULL, NULL) == LOG_ERR_INVALID_PARAM,
          "T9: NULL visitor rejected");

    /* Empty log: OK, nothing visited. */
    sim_reset();
    ctx_setup(2U);
    rec_n = 0U;
    check((log_read_tail(&ctx, 5U, collect_visit, NULL) == LOG_OK)
          && (rec_n == 0U),
          "T9: empty log visits nothing");
}

/*
 * T10 (tail across wrap + hole): a full ring with a torn hole; the tail
 * walk must stay chronological across the sector boundary and skip the
 * hole in both the positioning and the visiting pass.
 */
static void test_read_tail_hole_and_wrap(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < (2U * EPS); i++) {
        (void)write_one(i);
    }

    /* Corrupt slot 1 of sector 0 (payload byte): valid -> torn hole. */
    uint8_t zero = 0x00U;
    (void)sim_program(ENTRY + LOG_SEQ_SIZE, &zero, 1U);

    rec_n = 0U;
    check(log_read_tail(&ctx, 2U * EPS, collect_visit, NULL) == LOG_OK,
          "T10: tail over full ring OK");
    check(rec_n == (2U * EPS - 1U), "T10: torn hole skipped");
    check((recs[0].seq == 0U) && (recs[rec_n - 1U].seq == (2U * EPS - 1U)),
          "T10: window spans the whole ring");

    bool ascending = true;
    for (uint32_t i = 1U; i < rec_n; i++) {
        ascending = ascending && (recs[i].seq > recs[i - 1U].seq);
    }
    check(ascending, "T10: order strictly ascending across wrap and hole");
}

/*
 * T11 (P1): the poison byte can land in flash yet the program still
 * reports an error (timeout-after-write). The retry must NOT blindly
 * rewrite the slot: it re-verifies, sees the slot is no longer EMPTY,
 * consumes it as a torn hole and writes the next slot instead.
 */
static void test_repro_poison_landed_but_failed(void)
{
    sim_reset();
    ctx_setup(2U);

    (void)write_one(0U);            /* record A */

    /* Phase-1 fails silently; the poison byte lands but errors. */
    arm_prog_fail_mixed(head_addr(), 1U, 2U);
    check(write_one(1U) == LOG_ERR_FLASH_PROGRAM,
          "T11: program failure reported");
    fail_prog_addr = 0U;

    check(write_one(2U) == LOG_OK, "T11: retry accepted");
    check(log_get_write_offset(&ctx) == 3U * ENTRY,
          "T11: landed poison consumed as a hole");

    reboot_ctx();
    read_all_collect();
    check(rec_n == 2U, "T11: both records readable after reboot");
    check(log_get_next_seq(&ctx) == 2U, "T11: next_seq did not regress");
}

/*
 * T12 (P1, read-error variant): the poison verification read fails right
 * after a torn phase-1. The slot state is unknown; the retry must
 * re-verify and skip the torn slot instead of rewriting over it.
 */
static void test_repro_poison_read_error(void)
{
    sim_reset();
    ctx_setup(2U);

    (void)write_one(0U);            /* record A */

    /* Phase-1 lands (torn: seq+payload, no CRC) and errors; the poison
     * verification read then fails too. */
    arm_prog_fail_mixed(head_addr(), 0U, 1U);
    fail_read_once = true;
    check(write_one(1U) == LOG_ERR_FLASH_PROGRAM,
          "T12: torn program failure reported");
    fail_prog_addr = 0U;

    check(write_one(2U) == LOG_OK, "T12: retry accepted");
    check(log_get_write_offset(&ctx) == 3U * ENTRY,
          "T12: unverifiable torn slot consumed as a hole");

    read_all_collect();
    check(rec_n == 2U, "T12: both records readable");
}

/*
 * T13 (P2): paged reads over a COMPLETELY full ring (possible since the
 * deferred-erase policy) must terminate after exactly one lap - each
 * record returned exactly once, has_more false at the end.
 */
static void test_paging_full_ring_terminates(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < (2U * EPS); i++) {
        (void)write_one(i);
    }

    log_page_ctx_t page;
    memset(&page, 0, sizeof(page));

    uint32_t total_visited = 0U;
    uint32_t pages = 0U;
    bool order_ok = true;
    uint32_t prev_seq = UINT32_MAX;

    for (;;)
    {
        rec_n = 0U;
        if (log_read_last(&ctx, 8U, collect_visit, NULL, &page) != LOG_OK)
        {
            break;
        }
        total_visited += rec_n;
        pages++;

        for (uint32_t i = 0U; i < rec_n; i++)
        {
            /* Pages continue one contiguous newest -> oldest walk. */
            if (prev_seq != UINT32_MAX)
            {
                order_ok = order_ok && (recs[i].seq == (prev_seq - 1U));
            }
            prev_seq = recs[i].seq;
        }

        if (!page.has_more || (page.page_count == 0U) || (pages > 100U))
        {
            break;
        }
    }

    check(total_visited == (2U * EPS),
          "T13: full-ring paging returns each record exactly once");
    check(!page.has_more, "T13: iteration ends with has_more false");
    check(order_ok, "T13: pages continue the newest -> oldest walk");
}

/*
 * R1 (finding 1): program failure at the LAST slot of the last sector.
 * The head must never let a later write leave the log area, regardless
 * of which error path left it there.
 */
static void test_repro_last_slot_program_fail(void)
{
    sim_reset();
    ctx_setup(2U);

    /* Position the head exactly at the last slot of sector 1. */
    for (uint32_t i = 0U; i < (EPS + (EPS - 1U)); i++) {
        (void)write_one(i);
    }
    check((log_get_write_sector_index(&ctx) == 1U)
          && (log_get_write_offset(&ctx) == (EPS - 1U) * ENTRY),
          "R1: head parked at last slot of last sector");

    arm_prog_fail(head_addr(), true, 1U);
    fail_erase_once = true;
    check(write_one(900U) == LOG_ERR_FLASH_PROGRAM,
          "R1: program failure reported");

    fail_prog_addr = 0U;            /* keep fail_erase_once armed */
    (void)write_one(901U);          /* current bug: writes out of area */
    check(!sim_out_of_area, "R1: no write left the log area");
    check(canary_intact(), "R1: canary region untouched");

    fail_erase_once = false;
    log_status_t st = LOG_ERR_FLASH_PROGRAM;
    for (uint32_t attempt = 0U; attempt < 3U; attempt++) {
        st = write_one(902U + attempt);
        if (st == LOG_OK) {
            break;
        }
    }
    check(st == LOG_OK, "R1: ring accepts writes again once faults clear");
    /* Sector 1 holds 145 valid records (the failed slot stays a hole),
     * the recovery record lands in the freshly erased sector 0. */
    check(read_all_collect() == EPS,
          "R1: one sector minus the hole plus the recovery record readable");
}

/*
 * R2 (finding 2): a program command that fails without touching flash
 * leaves an EMPTY slot behind the head. The boot scan stops at EMPTY,
 * so later records vanish and next_seq regresses.
 */
static void test_repro_silent_program_fail(void)
{
    sim_reset();
    ctx_setup(2U);

    (void)write_one(0U);            /* record A */

    arm_prog_fail(head_addr(), true, 1U);
    check(write_one(1U) == LOG_ERR_FLASH_PROGRAM,
          "R2: silent program failure reported");
    fail_prog_addr = 0U;

    (void)write_one(2U);            /* record C, after the failed slot */
    check(read_all_collect() == 2U, "R2: runtime sees both records");

    reboot_ctx();                   /* power cycle */
    read_all_collect();
    check(rec_n == 2U, "R2: boot scan still sees both records");
    check(log_get_next_seq(&ctx) == 2U, "R2: next_seq did not regress");
}

/*
 * R3 (finding 1, erase variant): with the deferred-erase policy the lazy
 * sector erase runs when the FIRST write after a full sector needs the
 * space. That erase may fail: the error must surface, no write may leave
 * the area, and the transition must be retried on the next call.
 */
static void test_repro_erase_fail_on_transition(void)
{
    sim_reset();
    ctx_setup(2U);

    for (uint32_t i = 0U; i < (EPS + (EPS - 1U)); i++) {
        (void)write_one(i);
    }

    check(write_one(700U) == LOG_OK,
          "R3: filling the last slot does not erase yet");

    fail_erase_once = true;
    check(write_one(701U) == LOG_ERR_FLASH_ERASE,
          "R3: deferred erase failure reported at the next write");
    check(!sim_out_of_area && canary_intact(),
          "R3: nothing written out of area");

    fail_erase_once = false;
    check(write_one(702U) == LOG_OK,
          "R3: transition retried, write accepted");
    check(log_get_write_sector_index(&ctx) == 0U,
          "R3: head moved to the erased sector");
    check(!sim_out_of_area, "R3: no write left the log area");
    check(canary_intact(), "R3: canary region untouched");
    check(read_all_collect() == (EPS + 1U),
          "R3: one full sector plus the retried record readable");
}

int main(void)
{
    test_init_virgin();
    test_write_read_all();
    test_read_last_paging();
    test_wrap_two_sectors();
    test_torn_slot_skipped();

    test_repro_last_slot_program_fail();
    test_repro_silent_program_fail();
    test_repro_erase_fail_on_transition();
    test_poison_fail_keeps_head();
    test_erase_deferred_until_needed();
    test_boot_full_ring_preserves_all();
    test_read_tail_window();
    test_read_tail_hole_and_wrap();
    test_repro_poison_landed_but_failed();
    test_repro_poison_read_error();
    test_paging_full_ring_terminates();

    printf("\n--------------------------------\npassed: %d   failed: %d\n",
           passed, failed);
    return (failed == 0) ? 0 : 1;
}
