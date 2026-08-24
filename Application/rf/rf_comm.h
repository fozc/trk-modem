/*
 * rf_comm.h
 *
 *  Created on: 23 Aug 2026
 *      Author: fatih
 *
 * RF hub communication: Contiki process, UART transport, SCP frame
 * dispatch, and the single-outstanding command mechanism
 * (send_command / process / is_free / on_reply).
 */

#ifndef RF_RF_COMM_H_
#define RF_RF_COMM_H_

#include <stdint.h>
#include <stdbool.h>
#include "scp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Command mechanism types
 * ====================================================================== */

/** Komut sonucu - callback'e tasinir */
typedef enum
{
    SCP_CMD_OK = 0,     /* ACK alindi                        */
    SCP_CMD_ERR,        /* ERROR alindi (kod data[0]'da)     */
    SCP_CMD_TIMEOUT     /* tum denemeler tukendi              */
} scp_cmd_result_t;

/**
 * Komut bitince cagrilir - scp_process / scp_on_reply baglaminda
 * calisir (Contiki process poll dongusu, ISR degil).
 * rsp yalnizca callback suresince gecerlidir; kalici bilgi
 * gerekiyorsa kopyalayin. TIMEOUT'ta rsp = NULL'dur.
 */
typedef void (*scp_cmd_done_fn_t)(scp_cmd_result_t result,
                                  const scp_packet_t *rsp);

/* ======================================================================
 * Command mechanism API
 * ====================================================================== */

/**
 * @brief Yeni komut baslat. Tek aktif komut kurali: mesgulse false.
 *
 * @param type       SCP_TYPE_GET veya SCP_TYPE_SET
 * @param cmd        Komut kodu (0x01, 0x07, 0x20...)
 * @param body       Govde (NULL = govdesiz istek)
 * @param body_len   Govde uzunlugu
 * @param timeout_ms Deneme basina yanit bekleme suresi (ms)
 * @param retries    Ilk gonderimden sonra ek deneme sayisi
 * @param done       Bitince cagrilacak fonksiyon (NULL olabilir)
 *
 * @return true basarili; false mesgul ya da gecersiz parametre.
 */
bool scp_send_command(uint8_t type, uint8_t cmd,
                      const uint8_t *body, uint8_t body_len,
                      uint32_t timeout_ms, uint8_t retries,
                      scp_cmd_done_fn_t done);

/**
 * @brief Poll dongusunden cagirilir: timeout kontrolu, retry,
 *        tamamlanan komutun callback'i.
 */
void scp_process(uint32_t now_ms);

/**
 * @brief RX dispatch'ten beslenir: ACK/ERROR geldiginde cagrilir.
 *        CMD+SEQ eslesmesi yalnizca beklenen yanit ise isler.
 */
void scp_on_reply(const scp_packet_t *pkt);

/** @return true yeni komut gonderilebilir. */
bool scp_is_free(void);

/* ======================================================================
 * Process lifecycle
 * ====================================================================== */

void rf_comm_init(uint8_t device_address);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_COMM_H_ */
