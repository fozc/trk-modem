/*
 * rf_config.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 *
 * RF ayirici konfigurasyonu: 96 baytlik blok codec (default / CRC / RMW).
 * RAM store + staging API (plan Faz 1) ve EUI-64 yardimcilari da bu
 * modulun icinde yasayacaktir.
 */

#ifndef RF_RF_CONFIG_H_
#define RF_RF_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "rf_types.h"
#include "types.h"

/*
 * ESKI API - NVRAM ince sarmalayici (uint32 cihaz kimligi modeli).
 * Yeni tek-blok modeli (rf_feeder_t) altinda calisana kadar (plan
 * Faz 1-2) gecici olarak kalir.
 */
int rf_config_sync(void);
const rf_config_t* rf_config_get(feeder_id_t line_id);
rf_config_t* rf_config_get_mutable(feeder_id_t line_id);
bool rf_config_set(feeder_id_t line_id, const rf_config_t* config);

/* --- 96 baytlik blok codec (spec R2 Ek-A) ------------------------------ */

/**
 * @brief Spec default blogu (R2 section 3; const). Boot/defaults kaynagi.
 * @return 96 baytlik default bloga const isaretci.
 */
const rf_feeder_config_t *rf_config_default_block_get(void);

/**
 * @brief Feeder kaydi default'u: default blok + atanmamis EUI-64'ler +
 *        in_use = false.
 */
void rf_config_defaults(rf_feeder_t *p_cfg);

/**
 * @brief Yalniz yazilabilir araligi (@3-56) kopyala.
 */
void rf_config_copy_writable(const rf_feeder_config_t *p_src,
                             rf_feeder_config_t *p_dst);

/**
 * @brief Cihaza yazilacak blogu uret (RMW merge; spec R2 section 5.4 + 5.3-7).
 *
 * - p_device NULL degilse: cikisin tamami p_device'dan kopyalanir; yalniz
 *   yazilabilir alanlar (@3-56) p_ram'den uzerine yazilir. MASKELI alanlar
 *   (toploloji @0-2 + RF ailesi @57-66) cihaz degerinde KORUNUR - cihaz
 *   zaten yok sayar, RTU override etmez.
 * - p_device NULL ise (test / ilk imaj): taban default blok; yazilabilir
 *   alanlar p_ram'den; phase_id bilgi amacli p_ram'den alinir.
 *
 * RF-rezervi @67-93 sifirlanir; blok CRC-16'si (0-93) yeniden hesaplanir
 * (0x22-yazim yolunda cihaz denetlemez - R2-ek4; hesap zararsizdir).
 *
 * @return true basarili; false parametre hatasi.
 */
bool rf_config_for_write(const rf_feeder_config_t *p_ram,
                         const rf_feeder_config_t *p_device,
                         rf_feeder_config_t *p_out);

/**
 * @brief Blok CRC-16'sini dogrula (ofset 0-93, CCITT-FALSE).
 *
 * Dikkat: SCP GET cevap yolunda cihaz @94'u 0/farkli gonderebilir
 * (R2-ek4) - bu kontrol yalniz FRAM-boot benzeri baglamda kullanilmalidir.
 */
bool rf_config_crc_ok(const rf_feeder_config_t *p_blk);

/**
 * @brief Blok CRC-16'sini hesapla (kapsam: ofset 0-93).
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
 */
uint16_t rf_config_crc_compute(const rf_feeder_config_t *p_blk);

/**
 * @brief Yazilabilir-CRC (cfg_crc) - write-verify teyidi (R2-ek3).
 *
 * Yalniz maske-DISI (@3-56) yazilabilir alanlar uzerinden CRC-16/
 * CCITT-FALSE. 96B blogun tam-CRC'si DEGILDIR; maskeli alanlar cihaz-ozel
 * oldugundan karsilastirilamaz. 0x21/0x28 govdesindeki cfg_crc ile
 * esitligi kontrol edilir.
 */
uint16_t rf_config_writable_crc(const rf_feeder_config_t *p_blk);

#endif /* RF_RF_CONFIG_H_ */
