/*
 * rf_types.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */

#ifndef RF_RF_TYPES_H_
#define RF_RF_TYPES_H_

#include <stdint.h>
#include "modem_types.h"

typedef struct
{
    uint32_t r_device_id;
    uint32_t s_device_id;
    uint32_t t_device_id;
    float sistem_nominal_akimi;
    float set_edilebilir_actirma_esik_akimi;
    float artimli_akim_esigi;
    uint16_t hat_kopuk_hat_bosta;
    uint16_t olu_hat_akimi_dogrulama_suresi;
    uint8_t in_use;
    uint8_t mode;
    uint8_t set_edilebilir_acma_ariza_sayisi;
    uint8_t yenilenme_sifirlama_suresi;
    uint8_t hat_frekansi;
    uint8_t hat_id;
    uint8_t zone_id;
}__attribute__((packed)) rf_config_t;

typedef struct
{
    uint8_t hat_id[PHASE_MAX];
    uint8_t zone_id[PHASE_MAX];
    uint32_t device_id[PHASE_MAX];
    uint8_t calisma_modu[PHASE_MAX];
    uint8_t hat_frekansi[PHASE_MAX];
    int8_t sistem_sicakligi[PHASE_MAX];
    uint8_t sistem_dc_gerilimi[PHASE_MAX];
    uint16_t v5vdc[PHASE_MAX];
    uint16_t v3v3dc[PHASE_MAX];
    uint8_t actirma_dc_gerilimi[PHASE_MAX];
    uint16_t faz_akimi[PHASE_MAX];
    uint16_t faz_hata_akimi[PHASE_MAX];
    uint8_t aktif_sifirlama_zamanlayici_durumu[PHASE_MAX];
    uint8_t aktif_ariza_sayaci[PHASE_MAX];
    uint16_t gecmis_acma_sayisi[PHASE_MAX];
    uint32_t last_tx[PHASE_MAX];
    uint8_t rssi[PHASE_MAX];
    uint8_t lqi[PHASE_MAX];
}rf_monitor_t;

#endif /* RF_RF_TYPES_H_ */
