/*
 * iec104_event_log.h
 *
 *  Created on: Mar 16, 2026
 *      Author: fatih
 */

#ifndef LIBIEC104_IEC104_EVENT_LOG_H_
#define LIBIEC104_IEC104_EVENT_LOG_H_

#include <stdint.h>
#include <stdbool.h>
#include "fault_log.h"
#include "w25qxx_memory_organization.h"

/* ---------------------------------------------------------------------------
 * Capacity calculations
 *   sizeof(fault_log_t) == 20 bytes (7 cp56time2a + 4 float + 2 uint16 +
 *                                    1 bitfield + 1 pad + 4 crc + 1 pad)
 *   Two 4096-byte data sectors → 409 records max
 * --------------------------------------------------------------------------- */
#define IEC104_EVTLOG_RECORD_SIZE       sizeof(fault_log_t)
#define IEC104_EVTLOG_CAPACITY          ((uint16_t)(IEC104_EVTLOG_DATA_SIZE / IEC104_EVTLOG_RECORD_SIZE))
#define IEC104_EVTLOG_BITMAP_BYTES      ((IEC104_EVTLOG_CAPACITY + 7U) / 8U)

/* ---------------------------------------------------------------------------
 * Backup enable/disable
 *   1 → writes are mirrored to the backup data area and backup superblock;
 *       reads fall back to backup on primary CRC failure.
 *   0 → only primary sectors are used (fewer flash writes, less robust).
 * --------------------------------------------------------------------------- */
#define IEC104_EVTLOG_BACKUP_ENABLE     0

/* ---------------------------------------------------------------------------
 * IO interface — same pattern as fault_log / elog
 * --------------------------------------------------------------------------- */
typedef struct
{
    void (*read)(uint32_t addr, void *buf, uint32_t len);
    int  (*write)(uint32_t addr, const void *buf, uint32_t len);
} iec104_evtlog_io_if_t;

typedef struct
{
    iec104_evtlog_io_if_t io_if;
    uint32_t superblock_addr;         /* Primary superblock sector address    */
    uint32_t superblock_backup_addr;  /* Backup  superblock sector address    */
    uint32_t data_addr;               /* Primary data area start address      */
    uint32_t data_backup_addr;        /* Backup  data area start address      */
} iec104_evtlog_cfg_t;

/* ---------------------------------------------------------------------------
 * Superblock — stored in flash (primary + backup)
 * --------------------------------------------------------------------------- */
typedef struct
{
    uint16_t write_index;                          /* Next slot to write (0..CAPACITY-1)        */
    uint32_t total_events;                         /* Monotonic total-events-ever counter        */
    uint8_t  sent_bitmap[IEC104_EVTLOG_BITMAP_BYTES]; /* bit=1 → sent, bit=0 → unsent          */
    uint32_t crc;
} iec104_evtlog_superblock_t;

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

/**
 * Initialise the event log. Must be called once at startup.
 * Loads the superblock from flash (primary → backup → reinit on both corrupt).
 */
bool iec104_event_log_init(const iec104_evtlog_cfg_t *cfg);

/**
 * Append a fault_log_t record. Overwrites the oldest entry when full.
 * Marks the new entry as unsent. Writes both primary and backup data sectors
 * and updates the superblock (primary + backup).
 */
bool iec104_event_log_add(const fault_log_t *entry);

/**
 * Return the number of valid stored entries (capped at CAPACITY).
 */
uint16_t iec104_event_log_get_count(void);

/**
 * Read the n-th newest entry (n=0 → newest, n=count-1 → oldest).
 * Optionally returns the physical slot index via *slot_out (may be NULL).
 * Returns false if n is out of range or the entry has a CRC error.
 */
bool iec104_event_log_read_nth(uint16_t n, fault_log_t *out, uint16_t *slot_out);

/**
 * Return the number of unsent entries.
 */
uint16_t iec104_event_log_get_unsent_count(void);

/**
 * Read the n-th newest *unsent* entry (n=0 → newest unsent).
 * Optionally returns the physical slot index via *slot_out (may be NULL).
 * Returns false if n is out of range or the entry has a CRC error.
 */
bool iec104_event_log_read_nth_unsent(uint16_t n, fault_log_t *out, uint16_t *slot_out);

/**
 * Mark slot as sent (RAM only — call iec104_event_log_sync() to persist).
 */
void iec104_event_log_mark_sent(uint16_t slot);

/**
 * Persist the superblock to flash (primary + backup).
 * Call this when the IEC104 session closes.
 */
int iec104_event_log_sync(void);

/**
 * Clear all log entries and reset the superblock.
 */
void iec104_event_log_clear(void);

/**
 * Add count synthetic test records. Values are deterministically derived
 * from the global total_events counter so each run produces unique,
 * easily identifiable entries (feeder, phase, current, duration, type).
 */
void iec104_event_log_test(uint16_t count);

/**
 * Print all stored entries to the console (newest first).
 */
void iec104_event_log_dump(void);

/**
 * Register the "iec104evtlog" shell command.
 * Subcommands: dump (default) | test <N> | clear
 */
void iec104_event_log_shell_init(void);


#endif /* LIBIEC104_IEC104_EVENT_LOG_H_ */
