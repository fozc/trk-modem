/*
 * boot_log_defs.h
 *
 * Shared type definitions for the bootloader audit log.
 * Used by both the bootloader (boot_log.h) and the application
 * (app_boot_log.h).  Requires Bootloader/ in the include path.
 *
 *  Created on: May 09, 2026
 *      Author: fatih.ozcan
 */

#ifndef BOOT_LOG_DEFS_H
#define BOOT_LOG_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Flash geometry constants                                          */
/* ------------------------------------------------------------------ */
#define BOOT_LOG_SECTOR_SIZE         0x1000U                                         /* 4 KB  */
#define BOOT_LOG_ENTRY_SIZE          ((uint32_t)sizeof(boot_log_entry_t))             /* 16 B  */
#define BOOT_LOG_ENTRIES_PER_SECTOR  (BOOT_LOG_SECTOR_SIZE / BOOT_LOG_ENTRY_SIZE)     /* 256   */
#define BOOT_LOG_NUM_SECTORS         2U
#define BOOT_LOG_TOTAL_ENTRIES       (BOOT_LOG_ENTRIES_PER_SECTOR * BOOT_LOG_NUM_SECTORS)

/* ------------------------------------------------------------------ */
/*  Event types                                                       */
/* ------------------------------------------------------------------ */
typedef enum
{
    BOOT_LOG_FW_INSTALL_OK        = 0x01U,
    BOOT_LOG_FW_INSTALL_FAIL      = 0x02U,
    BOOT_LOG_FW_VERIFY_CRC_FAIL   = 0x03U,
    BOOT_LOG_FW_VERIFY_ECDSA_FAIL = 0x04U,
    BOOT_LOG_BOOT_ERROR           = 0x05U,
    BOOT_LOG_RECOVERY_ATTEMPT     = 0x06U,
    BOOT_LOG_RECOVERY_OK          = 0x07U,
    BOOT_LOG_RECOVERY_FAIL        = 0x08U,
    BOOT_LOG_AUTH_FAIL            = 0x09U,
    BOOT_LOG_SYSTEM_RESET         = 0x0AU,
    BOOT_LOG_DOWNGRADE_REJECTED   = 0x0BU,
} boot_log_event_t;

/* ------------------------------------------------------------------ */
/*  Log entry (16 bytes, packed, power-loss safe)                     */
/*                                                                    */
/*  sequence  : monotonically increasing, 0xFFFFFFFF = empty slot     */
/*  timestamp : packed YY(6) MM(4) DD(5) hh(5) mm(6) ss(6)           */
/*  event_id  : boot_log_event_t                                      */
/*  detail    : event-specific detail byte                            */
/*  payload   : event-specific 16-bit value                           */
/*  _reserved : padding / future use                                  */
/*  bcc       : XOR of bytes 0..14 (block check character)            */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed))
{
    uint32_t sequence;
    uint32_t timestamp;
    uint8_t  event_id;
    uint8_t  detail;
    uint16_t payload;
    uint16_t _reserved;
    uint8_t  _pad;
    uint8_t  bcc;
} boot_log_entry_t;

_Static_assert(sizeof(boot_log_entry_t) == 16U,
               "boot_log_entry_t must be exactly 16 bytes");

/* ------------------------------------------------------------------ */
/*  BCC helpers (shared between bootloader and application)           */
/* ------------------------------------------------------------------ */

/**
 * @brief Compute XOR block check character over the first 15 bytes.
 */
static inline uint8_t boot_log_calc_bcc(const boot_log_entry_t *p_entry)
{
    const uint8_t *p_raw = (const uint8_t *)p_entry;
    uint8_t bcc = 0U;

    for (uint32_t idx = 0U; idx < (BOOT_LOG_ENTRY_SIZE - 1U); idx++)
    {
        bcc ^= p_raw[idx];
    }

    return bcc;
}

/**
 * @brief Check whether an entry has a valid BCC and non-empty sequence.
 */
static inline bool boot_log_entry_is_valid(const boot_log_entry_t *p_entry)
{
    if (p_entry->sequence == 0xFFFFFFFFU)
    {
        return false;
    }

    return (boot_log_calc_bcc(p_entry) == p_entry->bcc);
}

#ifdef __cplusplus
}
#endif

#endif /* BOOT_LOG_DEFS_H */

/*** end of file ***/
