/**
 * @file  iec104_elog.h
 * @brief IEC-104 diagnostic event log (connection-level events only).
 *
 * Mirrors the elog architecture (same envelope, same append-only ring
 * storage engine) but records ONLY IEC-104 related events into its own
 * flash area, so protocol diagnostics stay separate from system-level
 * records. Storage is fixed: W25QXX SPI flash, area defined in
 * spi_flash_organization.h (IEC104_LOG_* macros).
 *
 * Payload contracts and log levels are defined HERE and nowhere else;
 * callers report plain event parameters through the iec104_elog_* functions.
 */
#ifndef LIBIEC104_IEC104_ELOG_H_
#define LIBIEC104_IEC104_ELOG_H_

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
    IEC104_ELOG_LEVEL_INFO = 0,
    IEC104_ELOG_LEVEL_WARN = 1,
} iec104_elog_level_t;

/** Event codes. */
typedef enum
{
    IEC104_ELOG_CONN = 1,   /* info: up=1(1) reason=1(1) peer_ip(4 BE) */
    IEC104_ELOG_DISC = 2,   /* info: up=0(1) reason=0(1) reserved(2)   */
} iec104_elog_code_t;

typedef struct
{
    uint32_t timestamp;
    uint16_t entry_id;
    uint8_t level;
    uint8_t code;
    uint8_t info[16];
}__attribute__((packed)) iec104_elog_entry_t;

/* ======================================================================
 *  API
 * ====================================================================== */

/** Initialize the ring storage. Call once at boot. */
void iec104_elog_init(void);

/**
 * @brief Record an established IEC-104 connection.
 *
 * info: up=1(1) reason=1 client connected(1) peer_ip(4, big-endian)
 *
 * Flap-suppressed: at most one record per 60 s, symmetric with
 * iec104_elog_disconnected() - a flapping SCADA link must not flood
 * the ring.
 *
 * @param peer_ip  Remote SCADA IP (network byte order as read from GSM).
 */
void iec104_elog_connected(uint32_t peer_ip);

/** Disconnect reason stored in iec104_elog_disconnected() records.
 *  Value 0 matches the historical "closed by remote" records. */
typedef enum
{
    IEC104_ELOG_DISC_CLOSED_BY_REMOTE = 0, /**< Remote end closed the link     */
    IEC104_ELOG_DISC_NO_CARRIER,           /**< Modem URC: connection dropped  */
    IEC104_ELOG_DISC_CONN_TIMEOUT,         /**< First-data / idle / AT timeout */
    IEC104_ELOG_DISC_LOCAL_CLOSE,          /**< Local close (protocol/modem)   */
    IEC104_ELOG_DISC_SOCKET_CLOSED,        /**< Socket state dropped to 0      */
    IEC104_ELOG_DISC_UNKNOWN               /**< No reason recorded             */
} iec104_elog_disc_reason_t;

/**
 * @brief Record a lost IEC-104 connection.
 *
 * info: up=0(1) disc reason(1) reserved(2)
 *
 * Flap-suppressed: at most one record per 60 s, symmetric with
 * iec104_elog_connected() - a flapping SCADA link must not flood the ring.
 */
void iec104_elog_disconnected(iec104_elog_disc_reason_t reason);

/** Copy up to count entries (newest first), skipping skip_newest newest.
 *  Returns 0 on success; *out_count reports entries copied. */
int iec104_elog_read_recent(uint32_t skip_newest, uint32_t count,
                            iec104_elog_entry_t *entries, uint32_t *out_count);

/** Erase all entries and re-init the ring. */
void iec104_elog_clear(void);

/** Total entries ever written (16-bit wrap like elog). */
uint16_t iec104_elog_get_entry_count(void);

/** Ring capacity in entries. */
uint16_t iec104_elog_get_max_entries(void);

/** Decode an entry's info payload into readable text (static buffer). */
const char *iec104_elog_info_to_text(const iec104_elog_entry_t *entry);

/** Format a timestamp as "YYYY-MM-DD HH:MM:SS" (static buffer). */
const char *iec104_elog_ts_to_text(uint32_t unix_ts);

/** Register the "iec104elog" shell command (dump/clear/info). */
void iec104_elog_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBIEC104_IEC104_ELOG_H_ */
