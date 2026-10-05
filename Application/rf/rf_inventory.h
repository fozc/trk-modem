/*
 * rf_inventory.h
 *
 *  Created on: Aug 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Envanter push siralayici: rf_store'daki atanmis cihazlari hub'a
 * tanitir (0x04 x N + 0x05 END). Komut mekanizmasini (rf_comm) kullanir;
 * kendi ic durumu bir imlectir (feeder x phase), state-machine degil.
 */

#ifndef RF_RF_INVENTORY_H_
#define RF_RF_INVENTORY_H_

#include <stdbool.h>
#include <stdint.h>
#include "rf_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Envanter push'u baslat: store'u tara, her cihaz icin 0x04, sonunda 0x05. */
void rf_inventory_start(void);

typedef enum
{
    RF_INVENTORY_IDLE = 0,
    RF_INVENTORY_LOADING,
    RF_INVENTORY_EMPTY,
    RF_INVENTORY_PARTIAL,
    RF_INVENTORY_READY,
    RF_INVENTORY_ERROR
} rf_inventory_status_t;

typedef struct
{
    uint8_t zone;
    uint8_t feeder;
    uint8_t phase;
    uint8_t eui64[8];
    uint8_t channel;
} rf_inventory_entry_t;

/** Clear volatile upload state when BOOT invalidates the hub inventory. */
void rf_inventory_reset(void);

rf_inventory_status_t rf_inventory_get_status(void);

/** Track transmitted upload requests for the documented 10 s gap. */
void rf_inventory_request_sent(uint8_t cmd);

/** Mirror only acknowledged MH assignments, never an unsent desired store. */
void rf_inventory_record_ack(const scp_packet_t *request);
bool rf_inventory_get_binding(uint8_t source, rf_inventory_entry_t *out);

/** Send an operator-selected assignment/deletion (feeder zero deletes).
 * The caller owns persistence; this transport API does not alter NVRAM.
 */
bool rf_inventory_update(const rf_inventory_entry_t *entry,
                          scp_cmd_done_fn_t done);

/** Queue epoch refresh only after the nonempty inventory is accepted. */
bool rf_inventory_refresh_epoch(uint8_t feeder, scp_cmd_done_fn_t done);

/* Minimum local wait after an acknowledged epoch refresh; not RF proof. */
bool rf_inventory_epoch_ready(uint8_t feeder);

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
