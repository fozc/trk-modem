/*
 * rf_config.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 *
 * RF ayirici konfigurasyonu: 96 baytlik blok codec (default / CRC / RMW)
 * + RAM SSOT store + staging API + EUI-64 yardimcilari.
 */

#ifndef RF_RF_CONFIG_H_
#define RF_RF_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "rf_types.h"
#include "types.h"

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
void rf_config_defaults(rf_feeder_t *cfg);

/**
 * @brief Yalniz yazilabilir araligi (@3-56) kopyala.
 */
void rf_config_copy_writable(const rf_feeder_config_t *src,
                             rf_feeder_config_t *dst);

/**
 * @brief Cihaza yazilacak blogu uret (RMW merge; spec R2 section 5.4 + 5.3-7).
 *
 * - device NULL degilse: cikisin tamami device'dan kopyalanir; yalniz
 *   yazilabilir alanlar (@3-56) ram'den uzerine yazilir. MASKELI alanlar
 *   (toploloji @0-2 + RF ailesi @57-66) cihaz degerinde KORUNUR - cihaz
 *   zaten yok sayar, RTU override etmez.
 * - device NULL ise (test / ilk imaj): taban default blok; yazilabilir
 *   alanlar ram'den; phase_id bilgi amacli ram'den alinir.
 *
 * RF-rezervi @67-93 sifirlanir; blok CRC-16'si (0-93) yeniden hesaplanir
 * (0x22-yazim yolunda cihaz denetlemez - R2-ek4; hesap zararsizdir).
 *
 * @return true basarili; false parametre hatasi.
 */
bool rf_config_for_write(const rf_feeder_config_t *ram,
                         const rf_feeder_config_t *device,
                         rf_feeder_config_t *out);

/**
 * @brief Blok CRC-16'sini dogrula (ofset 0-93, CCITT-FALSE).
 *
 * Dikkat: SCP GET cevap yolunda cihaz @94'u 0/farkli gonderebilir
 * (R2-ek4) - bu kontrol yalniz FRAM-boot benzeri baglamda kullanilmalidir.
 */
bool rf_config_crc_ok(const rf_feeder_config_t *blk);

/**
 * @brief Blok CRC-16'sini hesapla (kapsam: ofset 0-93).
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
 */
uint16_t rf_config_crc_compute(const rf_feeder_config_t *blk);

/**
 * @brief Yazilabilir-CRC (cfg_crc) - write-verify teyidi (R2-ek3).
 *
 * Yalniz maske-DISI (@3-56) yazilabilir alanlar uzerinden CRC-16/
 * CCITT-FALSE. 96B blogun tam-CRC'si DEGILDIR; maskeli alanlar cihaz-ozel
 * oldugundan karsilastirilamaz. 0x21/0x28 govdesindeki cfg_crc ile
 * esitligi kontrol edilir.
 */
uint16_t rf_config_writable_crc(const rf_feeder_config_t *blk);

/* --- RAM SSOT store + staging (plan Faz 1) ------------------------------ */

/**
 * @brief Store'u NVRAM last-known aynasindan yukle (app_main, nvram_init
 *        sonrasi cagrılır). Lazily ilk get'te de cagrilir (test kolayligi).
 */
void rf_store_init(void);

/** @brief Committed fider kaydina okunur erisim. */
const rf_feeder_t* rf_store_get(feeder_id_t line_id);

/**
 * @brief Yazilabilir erisim: staging aktifken staging kopyasini dondurur
 *        (POST parse yalniz oraya yazar), degilse RAM store'u.
 */
rf_feeder_t* rf_store_get_mutable(feeder_id_t line_id);

/** @brief Kaydi oldurucu sekilde guncelle (staging disinda, dikkatli). */
bool rf_store_set(feeder_id_t line_id, const rf_feeder_t* cfg);

/** @brief RAM store'u NVRAM aynasina yazip kalici hale getirir. */
int rf_store_sync(void);

/** @brief Staging (atomik POST guncellemesi): parse oncesi cagrilir. */
bool rf_store_stage_begin(void);
/** @brief Staging kopyasini committed store'a uygular. */
void rf_store_stage_commit(void);
/** @brief Yarim kalan parse'i iptal eder; committed deger korunur. */
void rf_store_stage_abort(void);

/* --- EUI-64 yardimcilari ------------------------------------------------ */

/** @brief Ham 8 bayt (MSB-first) -> 16-hex string (+ NUL). */
void rf_eui64_to_hex(const uint8_t eui[RF_EUI64_LEN], char out[RF_EUI64_HEX_LEN]);

/**
 * @brief 16-hex string -> ham 8 bayt. Bos string = atanmamis (tum-sifir).
 * @return true gecerli; false gecersiz karakter/uzunluk.
 */
bool rf_eui64_from_hex(uint8_t eui[RF_EUI64_LEN], const char *hex);

/** @return true tum baytlar sifir (atanmamis). */
bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN]);

#endif /* RF_RF_CONFIG_H_ */
