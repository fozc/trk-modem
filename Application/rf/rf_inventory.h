/*
 * rf_inventory.h
 *
 *  Created on: Aug 26, 2026
 *      Author: fatih
 *
 * Envanter push siralayici: rf_store'daki atanmis cihazlari hub'a
 * tanitir (0x04 x N + 0x05 END). Komut mekanizmasini (rf_comm) kullanir;
 * kendi ic durumu bir imlectir (feeder x phase), state-machine degil.
 */

#ifndef RF_RF_INVENTORY_H_
#define RF_RF_INVENTORY_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Envanter push'u baslat: store'u tara, her cihaz icin 0x04, sonunda 0x05. */
void rf_inventory_start(void);

/** @return true en az bir kez basariyla tamamlandi (inventory_loaded). */
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
