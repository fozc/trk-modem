/*
 * rf_discovery.h
 *
 *  Created on: Aug 20, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Kesif (discovery) kuyrugu: hatta katilan ama atanmamis cihazlarin
 * EUI-64 kimlikleri (spec R2 section 6.4b).
 */

#ifndef RF_RF_DISCOVERY_H_
#define RF_RF_DISCOVERY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "rf_types.h"   /* RF_EUI64_LEN */

#ifdef __cplusplus
extern "C" {
#endif

/** Kuyruk kapasitesi (ilk etap 12 ayirici + pay). */
#define RF_DISCOVERY_MAX   16U

typedef struct
{
    uint8_t eui64[RF_EUI64_LEN];
    int8_t rssi;
    bool has_rssi;
} rf_discovery_entry_t;

/** A repeated report refreshes RSSI even when the queue is full. */
bool rf_discovery_report(const uint8_t *eui64, int8_t rssi);

/** Remove only after an acknowledged inventory update/deletion. */
bool rf_discovery_remove(const uint8_t *eui64);

/** Copy the discovered identity and optional signal measurement. */
bool rf_discovery_get(size_t index, rf_discovery_entry_t *out);

/**
 * @brief Kuyrugu bosaltir (test / yeniden devreye alma).
 */
void rf_discovery_reset(void);

/**
 * @brief Kesif bildirimi isle: EUI-64'yu kuyruga ekle.
 *
 * Kuyruk doluysa yeni EUI yok sayilir (en eski bekleyen kurulum silinmez).
 *
 * @param[in] eui64  Kesfedilen cihazin kimligi (8 bayt, MSB-first).
 */
bool rf_discovery_add(const uint8_t *eui64);

/**
 * @brief Kuyruktaki tek bir EUI'yi kopyala.
 * @param[in] index  Kuyruktaki sirasi (0..count-1).
 * @param[out] eui64_out  EUI-64 cikis tamponu (8 bayt, MSB-first).
 */
bool rf_discovery_get_device(uint8_t index, uint8_t *eui64_out);

/**
 * @brief Kuyrukta bekleyen EUI sayisi.
 */
uint8_t rf_discovery_get_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_DISCOVERY_H_ */
