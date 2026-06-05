/*
 * gsm_dialer_process.h
 *
 * Dialer (outgoing) socket state machine for connecting to a remote
 * SCADA / headend server over TCP.
 */

#ifndef GSM_DIALER_PROCESS_H
#define GSM_DIALER_PROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------------------------------------------------------------------------
 *  Dialer socket state machine states
 * ------------------------------------------------------------------------- */
typedef enum
{
	GSM_HES_IDLE,              /* Server'a baglanti icin ilk ayarlari yap */
	GSM_HES_CONNECT_HEADEND,   /* Server'a baglan */
	GSM_HES_CHECK_SOCKET_INFO,
	GSM_HES_WAIT_TX_DATA,      /* Server'a gonderim icin data bekle */
	GSM_HES_DATA_MODE,         /* GSM modulu data gonderim moduna al */
	GSM_HES_CHECK_SOCKET,
	GSM_HES_SEND_DATA,         /* Datayi server'a gonder */
	GSM_HES_CLOSE_SOCKET,      /* Acik soketi kapat */
	GSM_HES_GET_SOCKET_DATA,
	GSM_HES_ERROR,             /* Soketin acilamamsi durumunda isletilir */

	GSM_HES_GET_RES_CONNECT_HEADEND,
	GSM_HES_GET_RES_CHECK_SOCKET_INFO,
	GSM_HES_GET_RES_WAIT_TX_DATA,
	GSM_HES_GET_RES_DATA_MODE,
	GSM_HES_GET_RES_CHECK_SOCKET,
	GSM_HES_GET_RES_SEND_DATA,
	GSM_HES_GET_RES_CLOSE_SOCKET,
	GSM_HES_GET_RES_GET_SOCKET_DATA,
	GSM_HES_GET_RES_ERROR,

	GSM_HES_CHECK_NET,
	GSM_HES_GET_RES_CHECK_NET,

	GSM_HES_RECONNECT_TO_NET,
	GSM_HES_GET_RES_RECONNECT_TO_NET
} gsm_join_state_t;

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief Run one iteration of the dialer socket state machine.
 *
 * Must be called periodically from gsm_normal_mode() and
 * gsm_power_outage_mode().
 */
void gsm_dialer_socket_process(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_DIALER_PROCESS_H */
