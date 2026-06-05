/*
 * app_boot_log.h
 *
 * Application-side read-only access to the bootloader audit log
 * stored on SPI flash.  Types are shared with the bootloader via
 * boot_log.h; this module adds stateless scan/read/dump functions
 * that do not require boot_log_init() or any bootloader state.
 *
 *  Created on: May 09, 2026
 *      Author: fatih.ozcan
 */

#ifndef APP_BOOT_LOG_H
#define APP_BOOT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "boot_log_defs.h"   /* boot_log_entry_t, boot_log_event_t */

/* ------------------------------------------------------------------ */
/*  Public API (read-only, stateless)                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Read the last N entries from the boot log (newest first).
 *
 * Scans SPI flash directly — no prior init required.
 *
 * @param[out] p_entries  Destination array (caller-allocated).
 * @param[in]  count      Maximum number of entries to retrieve.
 *
 * @return Number of valid entries actually read (may be less than count).
 */
uint32_t app_boot_log_read_last(boot_log_entry_t *p_entries, uint32_t count);

/**
 * @brief Return the total number of valid log entries on flash.
 *
 * Scans both sectors — no prior init required.
 */
uint32_t app_boot_log_count(void);

/**
 * @brief Print the last N entries via CSLOG (shell diagnostic).
 *
 * @param[in] count  Number of recent entries to print (0 = all).
 */
void app_boot_log_dump(uint32_t count);

/**
 * @brief Register the "bootlog" shell command.
 */
void app_boot_log_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOOT_LOG_H */

/*** end of file ***/
