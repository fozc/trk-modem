/*
 * gsm_listener_process.h
 *
 * Unified listener socket state machine for all TCP listener sockets.
 * Replaces the duplicated web and IEC104 listener processes.
 */

#ifndef GSM_LISTENER_PROCESS_H
#define GSM_LISTENER_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gsm_engine.h"

/* ---------------------------------------------------------------------------
 *  Callback type — all listener event callbacks share this signature
 * ------------------------------------------------------------------------- */
typedef void (*gsm_listener_cb_t)(void);

/* ---------------------------------------------------------------------------
 *  Listener AT command operation indices
 * ------------------------------------------------------------------------- */
typedef enum {
	LS_AT_CHECK_INFO,
	LS_AT_DATA_MODE,
	LS_AT_SEND_DATA,
	LS_AT_CHECK_SOCKET,
	LS_AT_CLOSE_SOCKET,
	LS_AT_OPEN_SOCKET,
	LS_AT_READ_DATA,
	LS_AT_ACCEPT_CONN,
	LS_AT_OP_COUNT
} ls_at_op_t;

/* ---------------------------------------------------------------------------
 *  Per-listener configuration (const, one instance per socket)
 * ------------------------------------------------------------------------- */
typedef struct {
	gsm_listener_id_t   id;              /* GSM_LISTENER_WEB / GSM_LISTENER_IEC104 */
	socket_type_t        socket_id;       /* LISTENER_SOCKET / IEC104_LISTENER_SOCKET */
	uint8_t              tx_direction;    /* GSM_TX_DIR_LISTENER_SOCKET / GSM_TX_DIR_IEC104_SOCKET */

	/* AT command query IDs indexed by ls_at_op_t */
	uint8_t              at_cmd[LS_AT_OP_COUNT];

	/* Event callbacks */
	gsm_listener_cb_t    on_connected;    /* Called when a new client connects */
	gsm_listener_cb_t    on_closed;       /* Called when the socket closes */
	gsm_listener_cb_t    reset_session;   /* Called to clear session info */
} gsm_listener_cfg_t;

/* ---------------------------------------------------------------------------
 *  Shared timing constants
 * ------------------------------------------------------------------------- */
#define GSM_LS_SOCKET_TIMER_MS       (15U * 1000U)
#define GSM_LS_GPRS_CHECK_TIMER_MS   (45U * 1000U)
#define GSM_LS_DATA_CHECK_TIMER_MS   (10U * 1000U)
#define GSM_LS_ACK_THRESHOLD_BYTES   (1500U)      /* If >1.5kB pending, force ACK check */

/* Stagger offset applied to IEC104 initial timer to avoid simultaneous triggering */
#define GSM_LS_STAGGER_OFFSET_MS     (GSM_LS_SOCKET_TIMER_MS / 2U)

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */
void gsm_listener_socket_process(const gsm_listener_cfg_t *p_cfg);

/* Convenience wrappers — call these from gsm_normal_mode() */
void gsm_listener_web_process(void);
void gsm_listener_iec104_process(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_LISTENER_PROCESS_H */
