/*
 * rf_discovery.h
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
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
