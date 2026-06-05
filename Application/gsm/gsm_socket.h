/**
 * @file  gsm_socket.h
 * @brief Centralized socket state and session tracking.
 *
 * Provides per-socket state, last-activity timestamp, session history
 * and connection statistics.  Indexed by socket_type_t values
 * (LISTENER_SOCKET, IEC104_LISTENER_SOCKET, DIALER_SOCKET).
 */
#ifndef GSM_GSM_SOCKET_H_
#define GSM_GSM_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @brief Number of managed sockets (LISTENER, IEC104, DIALER). */
#define GSM_SOCKET_COUNT  3U

/** @brief Disconnect reason codes. */
typedef enum
{
    SOCK_DISC_NONE = 0,       /**< No disconnect recorded yet       */
    SOCK_DISC_NO_CARRIER,     /**< Remote closed / NO CARRIER URC   */
    SOCK_DISC_TIMEOUT,        /**< AT command timeout                */
    SOCK_DISC_MANUAL,         /**< Local close (#SH) or reconnect   */
    SOCK_DISC_ERROR,          /**< AT ERROR response                 */
    SOCK_DISC_SOCKET_CLOSED   /**< State went to '0' unexpectedly   */
} sock_disconnect_reason_t;

/** @brief Per-socket session statistics (non-volatile during runtime). */
typedef struct
{
    /* Current session */
    uint32_t connect_tick;          /**< Tick (ms) when current session started        */
    uint32_t connect_epoch;         /**< RTC epoch (s) when current session started    */
    bool     is_connected;          /**< true if session is active                     */

    /* Last session */
    uint32_t last_connect_epoch;    /**< RTC epoch when last session started           */
    uint32_t last_disconnect_epoch; /**< RTC epoch when last session ended             */
    uint32_t last_session_ms;       /**< Duration of last completed session (ms)       */
    uint8_t  last_disconnect_reason;/**< sock_disconnect_reason_t                      */

    /* Lifetime counters */
    uint32_t total_sessions;        /**< Total connection count since boot             */
    uint32_t total_connected_ms;    /**< Cumulative connected time (ms)                */
    uint32_t total_disconnects;     /**< Total disconnect count                        */
    uint32_t max_session_ms;        /**< Longest single session duration (ms)          */
    uint32_t connect_attempts;      /**< Total dial/listen attempts                    */
    uint32_t error_count;           /**< Total error events                            */
} gsm_socket_stats_t;

void     gsm_socket_init(void);
void     gsm_socket_set_state(uint8_t socket, uint8_t state);
void     gsm_socket_touch_activity(uint8_t socket);
uint8_t  gsm_socket_get_state(uint8_t socket);
uint32_t gsm_socket_get_last_activity(uint8_t socket);
bool     gsm_socket_is_check_expired(uint8_t socket);

/** @brief Record a disconnect event with reason. */
void     gsm_socket_record_disconnect(uint8_t socket, sock_disconnect_reason_t reason);

/** @brief Increment connect attempt counter. */
void     gsm_socket_record_attempt(uint8_t socket);

/** @brief Increment error counter. */
void     gsm_socket_record_error(uint8_t socket);

/** @brief Get read-only pointer to socket statistics. */
const gsm_socket_stats_t *gsm_socket_get_stats(uint8_t socket);

#ifdef __cplusplus
}
#endif

#endif /* GSM_GSM_SOCKET_H_ */
