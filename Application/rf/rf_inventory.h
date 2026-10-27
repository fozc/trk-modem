/*
 * rf_inventory.h
 *
 *  Created on: Aug 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Envanter push siralayici: rf_store'daki atanmis cihazlari hub'a
 * tanitir (0x04 x N + 0x05 END). Komut mekanizmasini (rf_comm) kullanir;
 * Live changes drain the old event queues before uploading.
 */

#ifndef RF_RF_INVENTORY_H_
#define RF_RF_INVENTORY_H_

#include <stdbool.h>
#include <stdint.h>
#include "rf_comm.h"
#include "rf_scp_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Queue a full upload. False means BOOT protection, active configuration
 * or another inventory operation blocks it. Existing assignments first
 * enter DRAINING; cold BOOT upload starts after its protection handshake. */
bool rf_inventory_start(void);

typedef enum
{
    RF_INVENTORY_IDLE = 0,
    RF_INVENTORY_LOADING,
    RF_INVENTORY_EMPTY,
    RF_INVENTORY_PARTIAL,
    RF_INVENTORY_READY,
    RF_INVENTORY_ERROR,
    RF_INVENTORY_DRAINING
} rf_inventory_status_t;

typedef struct
{
    uint8_t zone;
    uint8_t feeder;
    uint8_t phase;
    uint8_t eui64[8];
    uint8_t channel;
    uint8_t line_index; /* Accepted RTU row; UINT8_MAX if not mapped. */
} rf_inventory_entry_t;

/** Clear volatile upload state when BOOT invalidates the hub inventory. */
void rf_inventory_reset(void);

rf_inventory_status_t rf_inventory_get_status(void);

typedef struct
{
    const char *reason;
    uint32_t total_ms;
    uint32_t reason_ms;
} rf_inventory_wait_t;

/* RAM-only drain diagnostics; reads never restart the timers. */
void rf_inventory_get_wait(rf_inventory_wait_t *out);


/** Track transmitted upload requests for the documented 10 s gap. */
void rf_inventory_request_sent(uint8_t cmd);

/** Mirror only acknowledged MH assignments, never an unsent desired store. */
void rf_inventory_record_ack(const scp_packet_t *request);
bool rf_inventory_get_binding(uint8_t source, rf_inventory_entry_t *out);

/* True only when the complete accepted inventory matches desired rows. */
bool rf_inventory_matches_config(void);
/* Stage old identities before rf_store_sync publishes a new desired image. */
bool rf_inventory_prepare_save(void);
bool rf_inventory_has_pending(void);
/* Includes a durable interrupted service move; ordinary save/config cannot clear it. */
bool rf_inventory_service_pending(void);

typedef enum
{
    RF_MOVE_IDLE = 0,
    RF_MOVE_DRAINING,
    RF_MOVE_ASSIGNING,
    RF_MOVE_EPOCH,
    RF_MOVE_WAIT_EVENTS,
    RF_MOVE_RESET,
    RF_MOVE_WAIT_LIVE,
    RF_MOVE_COMPLETE,
    RF_MOVE_INTERRUPTED,
    RF_MOVE_STORAGE_ERROR,
    RF_MOVE_SAVE_FAILED
} rf_move_state_t;

typedef struct
{
    rf_move_state_t state;
    uint8_t line;
    uint8_t from_phase;
    uint8_t to_phase;
} rf_move_status_t;

/* Zero-based row/phases. Operator must attest that the empty target was never used. */
bool rf_inventory_move_begin(uint8_t line, uint8_t from_phase, uint8_t to_phase, bool confirm_unused);
void rf_inventory_move_status(rf_move_status_t *out);
/* Retry final marker persistence only while this RTU session retains all proof. */
bool rf_inventory_move_finish(void);
/* Normal configuration writes cannot perform the R2.3 service relocation sequence. */
bool rf_inventory_has_relocation(void);

/** Send an operator-selected assignment/deletion (feeder zero deletes).
 * The caller owns persistence; this transport API does not alter NVRAM.
 */
bool rf_inventory_update(const rf_inventory_entry_t *entry,
                          scp_cmd_done_fn_t done);

/** Queue epoch refresh only after the nonempty inventory is accepted. */
bool rf_inventory_refresh_epoch(uint8_t feeder, scp_cmd_done_fn_t done);

/* Explicit MH replacement maintenance, never inferred from BOOT.
 * Arms one epoch retry only for the first following configuration's
 * TIMEOUT reason 5. It does not retry the configuration itself. */
bool rf_inventory_hub_replaced(uint8_t feeder, scp_cmd_done_fn_t done);
/* R2.3 event 144: coalesce a verified AY's rejection into maintenance. */
void rf_inventory_note_auth_failure(uint8_t feeder);
void rf_inventory_config_finished(uint8_t feeder, uint8_t reason);

/* False while event-144 maintenance is queued or during the minimum local
 * wait after its acknowledged epoch refresh; not RF proof. */
bool rf_inventory_epoch_ready(uint8_t feeder);

typedef struct
{
    bool active;
    bool has_report;
    bool retry_used;
    bool uncertain;
    uint8_t expected_phases;
    uint8_t report_phases;
    uint8_t received_phases;
    uint8_t checked_phases;
    uint8_t missing_phases;
} rf_epoch_status_t;

/* Diagnostic observations, never APPLIED evidence or a campaign identity. */
bool rf_inventory_get_epoch_status(uint8_t feeder, rf_epoch_status_t *out);
void rf_inventory_epoch_event(const rf_scp_event_t *event, bool source_matches, uint32_t now_ms);
/* Observe a validated, bound LIVE before rf_handle_live updates its cache. */
void rf_inventory_epoch_live(const rf_scp_live_t *live, uint32_t now_ms);
void rf_inventory_epoch_head_started(void);
void rf_inventory_epoch_ring_empty(void);
bool rf_inventory_get_boundary(uint32_t *total);

/** True for accepted nonempty READY/PARTIAL inventory, false for EMPTY. */
bool rf_inventory_is_loaded(void);

/** @return true siralayici su an calisiyor. */
bool rf_inventory_is_active(void);

/**
 * @brief Poll dongusunden cagrilir: siralayici aktif ama komut mekanizmasi
 *        mesgulse bekler, bosalinca sonraki adimi gonderir.
 */
void rf_inventory_continue(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_INVENTORY_H_ */

/*** end of file ***/
