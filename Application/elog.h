/*
 * elog.h
 *
 *  Created on: 14 Eyl 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef ELOG_H_
#define ELOG_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "elog_codes.h"

typedef enum
{
    ELOG_LEVEL_DEBUG,
    ELOG_LEVEL_INFO,
    ELOG_LEVEL_WARN,
    ELOG_LEVEL_ERROR,
    ELOG_LEVEL_FATAL
} elog_level_t;

/* One log record. Stored as the payload of the spi_flash_log ring; the
 * library adds its own sequence number and CRC around it on flash, so the
 * entry_id field is informational only (low 16 bits of the ring seq). */
typedef struct
{
    uint32_t timestamp;
    uint16_t entry_id;
    uint8_t level;
    uint8_t code;
    uint8_t info[16];

}__attribute__((packed)) elog_entry_t;

/* Storage is fixed (W25QXX SPI flash, layout in spi_flash_organization.h). */
void elog_init(void);
void elog_add(elog_code_t _code, elog_level_t level, const void *data, size_t data_len);
void elog_add_entry(const elog_entry_t *entry);

/* Copy up to count entries into entries[], skipping the skip_newest newest
 * ones. Entries are returned newest first; the number of entries actually
 * copied is reported through *out_count. Returns 0 on success, non-zero on
 * error. Single cursor pass: cost is O(skip_newest + count) slot reads. */
int elog_read_recent(uint32_t skip_newest, uint32_t count,
                     elog_entry_t *entries, uint32_t *out_count);

void elog_print(elog_level_t level, const char *message);
void elog_clear(void);
uint16_t elog_get_entry_count(void);
uint16_t elog_get_max_entries(void);
void elog_shell_init(void);

/**
 * @brief Source of a configuration change.
 */
typedef enum
{
    ELOG_SOURCE_WEB    = 0,
    ELOG_SOURCE_SERIAL = 1
} elog_config_source_t;

/**
 * @brief Log a configuration change event.
 *
 * info payload (16 bytes):
 *   [0..3]  client IP (big-endian, 0 when source is serial)
 *   [4]     source  (elog_config_source_t)
 *   [5..15] area label (null-terminated, truncated if needed)
 *
 * @param[in] code    Elog code identifying the config area.
 * @param[in] source  ELOG_SOURCE_WEB or ELOG_SOURCE_SERIAL.
 * @param[in] ip      Remote client IP (uint32_t, big-endian). Pass 0 for serial.
 * @param[in] p_area  Short area label (e.g. "device", "rf").
 */
void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area);

/* ======================================================================
 * Semantic logging API
 *
 * Modules must NOT pack info[] payloads or pick log levels themselves;
 * they call one of the functions below with plain event parameters. The
 * payload layout, level policy and rate limiting for every event code
 * live in elog.c only.
 * ====================================================================== */

/* ---- ELOG_SYSTEM_RESET_CAUSE / _HARDFAULT ---------------------------- */

/* info: flags(4, big-endian) raw_csr(4, big-endian) abnormal(1) */
void elog_log_reset_cause(uint32_t flags, uint32_t raw_csr, bool abnormal);

/* info: pc(4) lr(4) cfsr(4) hfsr(4), all big-endian. Called at boot with
 * the fault trace stashed by the hardfault handler, never from the
 * handler itself (flash writes are unsafe in fault context). */
void elog_log_hardfault(uint32_t pc, uint32_t lr, uint32_t cfsr, uint32_t hfsr);

/* ---- ELOG_SYSTEM_NVRAM_RECOVERED ------------------------------------- */

/* NVRAM recovery actions for elog_log_nvram_recovery(). */
#define ELOG_NVRAM_RESTORED_FROM_BACKUP 1U
#define ELOG_NVRAM_DEFAULTS_REWRITTEN   2U

/* info: action(1) stored_crc(4) calc_crc(4), CRCs big-endian */
void elog_log_nvram_recovery(uint8_t action, uint32_t stored_crc, uint32_t calc_crc);

/* ---- ELOG_SYSTEM_FW_UPDATE ------------------------------------------- */

/* Firmware update sources / results for elog_log_fw_update(). */
#define ELOG_FW_SRC_XMODEM    1U
#define ELOG_FW_SRC_RFWU      2U
#define ELOG_FW_RESULT_START     0U
#define ELOG_FW_RESULT_OK        1U
#define ELOG_FW_RESULT_FAIL      2U
#define ELOG_FW_RESULT_AUTH_FAIL 3U

/* info: source(1) result(1) size(4, big-endian) */
void elog_log_fw_update(uint8_t source, uint8_t result, uint32_t size);

/* ---- ELOG_PWR_ALARM / ELOG_BAT_STATE --------------------------------- */

/* Context fields of a latched power-board alarm transition. */
typedef struct
{
    uint8_t latch;      /* new alarm_latch value */
    uint8_t live;       /* live alarm bits */
    uint8_t sys_fault;  /* power board SYS_FAULT */
    uint8_t bq_fault0;  /* charger BQ REG20 */
    uint8_t bq_fault1;  /* charger BQ REG21 */
    bool    rising;     /* true = new alarm bits appeared */
} elog_power_alarm_t;

/* info: latch(1) live(1) sys_fault(1) bq0(1) bq1(1) rising(1) */
void elog_log_power_alarm(const elog_power_alarm_t *alarm);

/* Battery state sources for elog_log_battery_state_change(). */
#define ELOG_BAT_SRC_POWER_BOARD 1U
#define ELOG_BAT_SRC_BMS         2U

/* info: src(1) event=0(1) old(1) new(1) soc(1) soh(1).
 * Level: WARN when the new state is non-present (power board source). */
void elog_log_battery_state_change(uint8_t source, uint8_t old_state,
                                   uint8_t new_state, uint8_t soc, uint8_t soh);

/* info: src=BMS(1) event=1(1) threshold(1) set(1) soc(1) soh(1).
 * Level: ERROR at the 10 % threshold, WARN at 20 %. */
void elog_log_battery_soc_threshold(uint8_t threshold, bool set,
                                    uint8_t soc, uint8_t soh);

/* ---- ELOG_IEC104_CONN ------------------------------------------------ */

/* info: up=1(1) reason=1 client connected(1) peer_ip(4, big-endian) */
void elog_log_iec104_connected(uint32_t peer_ip);

/* info: up=0(1) reason=0 closed by remote(1).
 * Rate limited to one record per 60 s. */
void elog_log_iec104_disconnected(void);

/* ---- ELOG_WEB_LOGIN_FAIL --------------------------------------------- */

/* info: client_ip(4, big-endian) burst_count(2).
 * Burst-limited: first failure logs at once, then one record per 60 s
 * window carrying the number of failures in the burst. */
void elog_log_web_login_fail(uint32_t client_ip);

#endif /* ELOG_H_ */
