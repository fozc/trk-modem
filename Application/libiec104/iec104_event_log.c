/*
 * iec104_event_log.c
 *
 *  Created on: Mar 16, 2026
 *      Author: fatih
 *
 * Circular flash-backed event log for IEC 104 fault events.
 *
 * Layout (6 flash sectors, 24 KB):
 *   - Sector 0  : Superblock primary
 *   - Sector 1  : Superblock backup
 *   - Sector 2-3: Data primary  (2 × 4 KB)
 *   - Sector 4-5: Data backup   (2 × 4 KB)
 *
 * Entries are stored as fault_log_t (20 bytes) in a circular ring.
 * write_index always points to the NEXT slot to write (mod CAPACITY).
 * total_events is a monotonic counter that records every event ever written.
 *
 * "Sent" state is tracked via a bitmap in the superblock.
 * bit = 0 → unsent, bit = 1 → sent.
 * The bitmap is persisted by iec104_event_log_sync(), which should be
 * called when the IEC 104 session closes (at-least-once delivery).
 *
 * Every iec104_event_log_add() writes both primary and backup data
 * areas and immediately persists the updated superblock.
 */

#include "iec104_event_log.h"
#include <string.h>
#include <stdlib.h>
#include "crc32.h"
#include "bsp.h"
#include "cp56time2a.h"
#include "shell.h"

/* ────────────────────────────────────────────────────────── compile-time checks */
_Static_assert(sizeof(iec104_evtlog_superblock_t) <= 4096U,
               "iec104_evtlog_superblock_t exceeds one flash sector");
_Static_assert(IEC104_EVTLOG_CAPACITY > 0U,
               "IEC104_EVTLOG_CAPACITY must be > 0");

/* ────────────────────────────────────────────────────────── module-level state */
static iec104_evtlog_cfg_t        g_cfg         = {0};
static iec104_evtlog_superblock_t g_sb          = {0};
static bool                       g_initialized = false;
static bool                       g_sb_dirty    = false; /* bitmap changed but not yet synced */

/* ────────────────────────────────────────────────────────── internal helpers */

static void flash_read(uint32_t addr, void *buf, uint32_t len)
{
    g_cfg.io_if.read(addr, buf, len);
}

static int flash_write(uint32_t addr, const void *buf, uint32_t len)
{
    return g_cfg.io_if.write(addr, buf, len);
}

static uint32_t sb_calculate_crc(const iec104_evtlog_superblock_t *sb)
{
    crc32_t crc = crc32_init();
    crc = crc32_update(crc, sb, sizeof(iec104_evtlog_superblock_t) - sizeof(sb->crc));
    return crc32_finalize(crc);
}

static bool sb_validate(const iec104_evtlog_superblock_t *sb)
{
    return (sb->crc == sb_calculate_crc(sb));
}

static int sb_write_to_flash(uint32_t addr)
{
    g_sb.crc = sb_calculate_crc(&g_sb);
    return flash_write(addr, &g_sb, sizeof(g_sb));
}

/* Physical flash address of slot n in the primary data area */
static uint32_t slot_to_addr_primary(uint16_t slot)
{
    return g_cfg.data_addr + (uint32_t)slot * (uint32_t)sizeof(fault_log_t);
}

/* Physical flash address of slot n in the backup data area */
#if IEC104_EVTLOG_BACKUP_ENABLE
static uint32_t slot_to_addr_backup(uint16_t slot)
{
    return g_cfg.data_backup_addr + (uint32_t)slot * (uint32_t)sizeof(fault_log_t);
}
#endif

/* ── bitmap helpers ─────────────────────────────────────────────────────── */

static void bitmap_clear_bit(uint16_t slot)   /* mark unsent */
{
    g_sb.sent_bitmap[slot / 8u] &= (uint8_t)~(1u << (slot % 8u));
}

static void bitmap_set_bit(uint16_t slot)     /* mark sent */
{
    g_sb.sent_bitmap[slot / 8u] |= (uint8_t)(1u << (slot % 8u));
}

static bool bitmap_is_sent(uint16_t slot)
{
    return (g_sb.sent_bitmap[slot / 8u] & (uint8_t)(1u << (slot % 8u))) != 0u;
}

/* Calculate CRC for a fault_log_t record (covers all fields except crc itself) */
static uint32_t record_calc_crc(const fault_log_t *r)
{
    crc32_t c = crc32_init();
    c = crc32_update(c, r, sizeof(fault_log_t) - sizeof(r->crc));
    return crc32_finalize(c);
}

/* Validate a fault_log_t record's own embedded CRC */
static bool record_crc_ok(const fault_log_t *r)
{
    return (r->crc == record_calc_crc(r));
}

/* ────────────────────────────────────────────────────────── public API */

bool iec104_event_log_init(const iec104_evtlog_cfg_t *cfg)
{
    if (cfg == NULL || cfg->io_if.read == NULL || cfg->io_if.write == NULL)
    {
        CSLOG("iec104_event_log: init failed — NULL config or IO interface\r\n");
        return false;
    }

    iec104_event_log_shell_init();

    g_cfg         = *cfg;
    g_initialized = false;
    g_sb_dirty    = false;

    /* ── try primary superblock ─────────────────────────────────────────── */
    flash_read(g_cfg.superblock_addr, &g_sb, sizeof(g_sb));

    if (sb_validate(&g_sb))
    {
    	// Superblock'taki write_index'in flash kapasitesini asmadigini dogrula.
    	// Eger indeks gecersizse, sistemi korumak icin sifirla.
    	if (g_sb.write_index >= IEC104_EVTLOG_CAPACITY)
    	{
    		CSLOG("iec104_event_log: invalid write_index detected (%u), resetting to 0\r\n", g_sb.write_index);
    		g_sb.write_index = 0;
    		sb_write_to_flash(g_cfg.superblock_addr); // Duzeltilen degeri flash'a geri yaz
    	}
        g_initialized = true;
        CSLOG("iec104_event_log: init OK (primary SB) count=%u total=%lu\r\n",
              iec104_event_log_get_count(), (unsigned long)g_sb.total_events);
        return true;
    }

    CSLOG("iec104_event_log: primary SB CRC mismatch, trying backup...\r\n");

#if IEC104_EVTLOG_BACKUP_ENABLE
    /* ── try backup superblock ──────────────────────────────────────────── */
    flash_read(g_cfg.superblock_backup_addr, &g_sb, sizeof(g_sb));

    if (sb_validate(&g_sb))
    {
    	if (g_sb.write_index >= IEC104_EVTLOG_CAPACITY)
    	{
    		CSLOG("iec104_event_log: invalid write_index detected (%u), resetting to 0\r\n", g_sb.write_index);
    		g_sb.write_index = 0;
    		sb_write_to_flash(g_cfg.superblock_addr); // Duzeltilen degeri flash'a geri yaz
    	}

        CSLOG("iec104_event_log: recovered from backup SB\r\n");
        sb_write_to_flash(g_cfg.superblock_addr); /* restore primary */
        g_initialized = true;
        CSLOG("iec104_event_log: init OK (backup SB) count=%u total=%lu\r\n",
              iec104_event_log_get_count(), (unsigned long)g_sb.total_events);
        return true;
    }
    CSLOG("iec104_event_log: both SBs corrupt — reinitialising\r\n");
#else
    CSLOG("iec104_event_log: primary SB corrupt — reinitialising\r\n");
#endif

    memset(&g_sb, 0, sizeof(g_sb));
    sb_write_to_flash(g_cfg.superblock_addr);
#if IEC104_EVTLOG_BACKUP_ENABLE
    sb_write_to_flash(g_cfg.superblock_backup_addr);
#endif

    g_initialized = true;

    return true;
}

/* ─────────────────────────────────────────────────────────────────────────── */

bool iec104_event_log_add(const fault_log_t *entry)
{
    if (!g_initialized || entry == NULL)
    {
        return false;
    }

    uint32_t now = bsp_get_tick();

    uint16_t slot = g_sb.write_index;

    /* Write to primary data area (and backup if enabled) */
    int r1 = flash_write(slot_to_addr_primary(slot), entry, sizeof(fault_log_t));
#if IEC104_EVTLOG_BACKUP_ENABLE
    int r2 = flash_write(slot_to_addr_backup(slot), entry, sizeof(fault_log_t));
#else
    int r2 = 0;
#endif

    if (r1 != 0 || r2 != 0)
    {
        CSLOG("iec104_event_log: flash write error (slot %u, r1=%d r2=%d)\r\n",
              slot, r1, r2);
        return false;
    }

    /* New entry is unsent */
    bitmap_clear_bit(slot);

    /* Advance write pointer */
    g_sb.write_index  = (uint16_t)((slot + 1u) % IEC104_EVTLOG_CAPACITY);
    g_sb.total_events++;

    /* Persist superblock after every write */
    int rs1 = sb_write_to_flash(g_cfg.superblock_addr);
#if IEC104_EVTLOG_BACKUP_ENABLE
    int rs2 = sb_write_to_flash(g_cfg.superblock_backup_addr);
#else
    int rs2 = 0;
#endif
    g_sb_dirty = false;

    uint32_t latency = bsp_get_tick() - now;
    CSLOG("iec104_event_log: added entry at slot %u (latency %lu ms)\r\n",
          slot, (unsigned long)latency);

    if (rs1 != 0 || rs2 != 0)
    {
        CSLOG("iec104_event_log: SB write error after add (slot %u)\r\n", slot);
        return false;
    }

    return true;
}

/* ─────────────────────────────────────────────────────────────────────────── */

uint16_t iec104_event_log_get_count(void)
{
    if (!g_initialized)
    {
        return 0u;
    }
    uint32_t count = g_sb.total_events;
    if (count > IEC104_EVTLOG_CAPACITY)
    {
        count = IEC104_EVTLOG_CAPACITY;
    }
    return (uint16_t)count;
}

/* ─────────────────────────────────────────────────────────────────────────── */

bool iec104_event_log_read_nth(uint16_t n, fault_log_t *out, uint16_t *slot_out)
{
    if (!g_initialized || out == NULL)
    {
        return false;
    }

    uint16_t count = iec104_event_log_get_count();
    if (n >= count)
    {
        return false;
    }

    /* n=0 → newest entry (slot written most recently) */
    uint16_t slot = (uint16_t)((g_sb.write_index + IEC104_EVTLOG_CAPACITY - 1u - n)
                               % IEC104_EVTLOG_CAPACITY);

    /* Read from primary; on CRC failure fall back to backup */
    fault_log_t tmp;
    flash_read(slot_to_addr_primary(slot), &tmp, sizeof(tmp));

    if (!record_crc_ok(&tmp))
    {
#if IEC104_EVTLOG_BACKUP_ENABLE
        CSLOG("iec104_event_log: primary slot %u CRC error, trying backup\r\n", slot);
        flash_read(slot_to_addr_backup(slot), &tmp, sizeof(tmp));

        if (!record_crc_ok(&tmp))
        {
            CSLOG("iec104_event_log: backup slot %u CRC error\r\n", slot);
            return false;
        }
#else
        CSLOG("iec104_event_log: primary slot %u CRC error\r\n", slot);
        return false;
#endif
    }

    *out = tmp;
    if (slot_out != NULL)
    {
        *slot_out = slot;
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────── */

uint16_t iec104_event_log_get_unsent_count(void)
{
    if (!g_initialized)
    {
        return 0u;
    }

    uint16_t count  = iec104_event_log_get_count();
    uint16_t unsent = 0u;

    for (uint16_t i = 0u; i < count; i++)
    {
        uint16_t slot = (uint16_t)((g_sb.write_index + IEC104_EVTLOG_CAPACITY - 1u - i)
                                   % IEC104_EVTLOG_CAPACITY);
        if (!bitmap_is_sent(slot))
        {
            unsent++;
        }
    }
    return unsent;
}

/* ─────────────────────────────────────────────────────────────────────────── */

bool iec104_event_log_read_nth_unsent(uint16_t n, fault_log_t *out, uint16_t *slot_out)
{
    if (!g_initialized || out == NULL)
    {
        return false;
    }

    uint16_t count        = iec104_event_log_get_count();
    uint16_t unsent_found = 0u;

    for (uint16_t i = 0u; i < count; i++)
    {
        uint16_t slot = (uint16_t)((g_sb.write_index + IEC104_EVTLOG_CAPACITY - 1u - i)
                                   % IEC104_EVTLOG_CAPACITY);

        if (!bitmap_is_sent(slot))
        {
            if (unsent_found == n)
            {
                return iec104_event_log_read_nth(i, out, slot_out);
            }
            unsent_found++;
        }
    }
    return false;
}

/* ─────────────────────────────────────────────────────────────────────────── */

void iec104_event_log_mark_sent(uint16_t slot)
{
    if (!g_initialized || slot >= IEC104_EVTLOG_CAPACITY)
    {
        return;
    }
    bitmap_set_bit(slot);
    g_sb_dirty = true;
}

/* ─────────────────────────────────────────────────────────────────────────── */

int iec104_event_log_sync(void)
{
    if (!g_initialized)
    {
        return -1;
    }

    if (!g_sb_dirty)
    {
        return 0;
    }

    int r1 = sb_write_to_flash(g_cfg.superblock_addr);
#if IEC104_EVTLOG_BACKUP_ENABLE
    int r2 = sb_write_to_flash(g_cfg.superblock_backup_addr);
#else
    int r2 = 0;
#endif
    g_sb_dirty = false;

    if (r1 != 0 || r2 != 0)
    {
        CSLOG("iec104_event_log: sync error (r1=%d r2=%d)\r\n", r1, r2);
        return -1;
    }

    CSLOG("iec104_event_log: synced (unsent=%u)\r\n",
          iec104_event_log_get_unsent_count());
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */

void iec104_event_log_clear(void)
{
    if (!g_initialized)
    {
        return;
    }

    memset(&g_sb, 0, sizeof(g_sb));
    sb_write_to_flash(g_cfg.superblock_addr);
#if IEC104_EVTLOG_BACKUP_ENABLE
    sb_write_to_flash(g_cfg.superblock_backup_addr);
#endif
    g_sb_dirty = false;

    CSLOG("iec104_event_log: cleared\r\n");
}

/* ─────────────────────────────────────────────────────────────────────────── */

void iec104_event_log_test(uint16_t count)
{
    if (!g_initialized || count == 0u)
    {
        return;
    }

    for (uint16_t i = 0u; i < count; i++)
    {
        /* Use the *current* total_events so every call produces globally unique,
         * deterministic values that are easy to identify in a dump. */
        uint32_t idx = g_sb.total_events;  /* captured before add() increments it */

        fault_log_t e;
        memset(&e, 0, sizeof(e));

        e.tm                        = cp56time2a_now();
        e.fault_current             = 100.0f + (float)(idx % 900u) / 10.0f; /* 100.0 … 189.9 A   */
        e.fault_duration_ms         = (uint16_t)(100u + (idx % 50u) * 20u); /* 100 … 1080 ms     */
        e.info.feeder               = (uint8_t)(idx % 8u);                  /* feeder 0-7        */
        e.info.phase                = (uint8_t)(idx % 3u);                  /* phase 0-2         */
        e.info.type                 = (uint8_t)(idx % 2u);                  /* 0=temp, 1=perm    */
        e.info.nominal_current_status = (uint8_t)((idx % 3u) == 0u ? 1u : 0u);
        e.info.power_status         = (uint8_t)(idx % 2u);
        e.crc                       = record_calc_crc(&e);

        if (!iec104_event_log_add(&e))
        {
            SHELL_LOG("iec104_event_log_test: add failed at i=%u\r\n", i);
            break;
        }
    }

    SHELL_LOG("iec104_event_log_test: added %u entries — total_events=%lu  count=%u  unsent=%u\r\n",
          count,
          (unsigned long)g_sb.total_events,
          iec104_event_log_get_count(),
          iec104_event_log_get_unsent_count());
}

/* ─────────────────────────────────────────────────────────────────────────── */

void iec104_event_log_dump(void)
{
    uint16_t count = iec104_event_log_get_count();

    SHELL_LOG("\r\n=== iec104_event_log ==============================================\r\n");
    SHELL_LOG("  log size	 : %u bytes\r\n", (unsigned)sizeof(fault_log_t));
    SHELL_LOG("  capacity    : %u\r\n", (unsigned)IEC104_EVTLOG_CAPACITY);
    SHELL_LOG("  count       : %u\r\n", count);
    SHELL_LOG("  total_events: %lu\r\n", (unsigned long)g_sb.total_events);
    SHELL_LOG("  write_index : %u\r\n", g_sb.write_index);
    SHELL_LOG("  unsent      : %u\r\n", iec104_event_log_get_unsent_count());
    SHELL_LOG("  (newest first)\r\n");
    SHELL_LOG("-------------------------------------------------------------------\r\n");

    for (uint16_t n = 0u; n < count; n++)
    {
        fault_log_t e;
        uint16_t    slot;

        if (!iec104_event_log_read_nth(n, &e, &slot))
        {
            SHELL_LOG("  [%3u] slot=%3u  CRC ERROR\r\n", n, slot);
            continue;
        }

        SHELL_LOG("  [%3u] slot=%3u sent=%u  F%u Ph%u %s  I=%.1fA  T=%ums  P=%s N=%s  %02u-%02u-%04u %02u:%02u:%02u\r\n",
              n,
              slot,
              bitmap_is_sent(slot) ? 1u : 0u,
              (unsigned)(e.info.feeder + 1u),
              (unsigned)(e.info.phase  + 1u),
              e.info.type ? "P" : "T",
              e.fault_current / 10.0f,
              (unsigned)e.fault_duration_ms,
              e.info.power_status           ? "1"    : "0",
              e.info.nominal_current_status ? "0" : "1",
              (unsigned)e.tm.day,
              (unsigned)e.tm.month,
              (unsigned)(e.tm.year + 2000u),
              (unsigned)e.tm.hour,
              (unsigned)e.tm.minute,
              (unsigned)cp56time2a_get_second(&e.tm));
    }
    SHELL_LOG("===================================================================\r\n");
}

/* ─────────────────────────────────────────────────────────────────────────── */

static int shell_iec104evtlog(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "test") == 0)
    {
        uint16_t cnt = (argc > 2) ? (uint16_t)atoi(argv[2]) : 10u;
        iec104_event_log_test(cnt);
    }
    else if (argc > 1 && strcmp(argv[1], "clear") == 0)
    {
        iec104_event_log_clear();
    }
    else
    {
        iec104_event_log_dump();
    }
    return 0;
}

void iec104_event_log_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd  = "iec104log",
        .desc = "IEC104 event log: [dump] | test <N> | clear",
        .func = shell_iec104evtlog
    });
}

