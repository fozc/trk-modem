/**
 * @file  iec104_log.h
 * @brief IEC-104 diagnostic event log (connection-level events only).
 *
 * Mirrors the elog architecture (same envelope, same append-only ring
 * storage engine) but records ONLY IEC-104 related events into its own
 * flash area, so protocol diagnostics stay separate from system-level
 * records. Storage is fixed: W25QXX SPI flash, area defined in
 * spi_flash_organization.h (IEC104_LOG_* macros).
 *
 * Payload contracts and log levels are defined HERE and nowhere else;
 * callers report plain event parameters through the iec104_log_* functions.
 */
#ifndef LIBIEC104_IEC104_LOG_H_
#define LIBIEC104_IEC104_IEC104_LOG_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 *  Entry envelope (same layout as elog)
 * ====================================================================== */

typedef enum
{
    IEC104_LOG_LEVEL_INFO = 0,
    IEC104_LOG_LEVEL_WARN = 1,
} iec104_log_level_t;

/** Event codes. */
typedef enum
{
    IEC104_LOG_CONN = 1,   /* info: up=1(1) reason=1(1) peer_ip(4 BE) */
    IEC104_LOG_DISC = 2,   /* info: up=0(1) reason=0(1) reserved(2)   */
} iec104_log_code_t;

typedef struct
{
    uint32_t timestamp;
    uint16_t entry_id;
    uint8_t level;
    uint8_t code;
    uint8_t info[16];
}__attribute__((packed)) iec104_log_entry_t;

/* ======================================================================
 *  API
 * ====================================================================== */

/** Initialize the ring storage. Call once at boot. */
void iec104_log_init(void);

/**
 * @brief Record an established IEC-104 connection.
 *
 * info: up=1(1) reason=1 client connected(1) peer_ip(4, big-endian)
 *
 * @param peer_ip  Remote SCADA IP (network byte order as read from GSM).
 */
void iec104_log_connected(uint32_t peer_ip);

/**
 * @brief Record a lost IEC-104 connection.
 *
 * info: up=0(1) reason=0 closed by remote(1) reserved(2)
 *
 * Flap-suppressed: at most one record per 60 s - a flapping SCADA link
 * must not flood the ring.
 */
void iec104_log_disconnected(void);

/** Copy up to count entries (newest first), skipping skip_newest newest.
 *  Returns 0 on success; *out_count reports entries copied. */
int iec104_log_read_recent(uint32_t skip_newest, uint32_t count,
                           iec104_log_entry_t *entries, uint32_t *out_count);

/** Erase all entries and re-init the ring. */
void iec104_log_clear(void);

/** Total entries ever written (16-bit wrap like elog). */
uint16_t iec104_log_get_entry_count(void);

/** Ring capacity in entries. */
uint16_t iec104_log_get_max_entries(void);

/** Decode an entry's info payload into readable text (static buffer). */
const char *iec104_log_info_to_text(const iec104_log_entry_t *entry);

/** Format a timestamp as "YYYY-MM-DD HH:MM:SS" (static buffer). */
const char *iec104_log_ts_to_text(uint32_t unix_ts);

/** Register the "iec104log" shell command (dump/clear/info). */
void iec104_log_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBIEC104_IEC104_LOG_H_ */
