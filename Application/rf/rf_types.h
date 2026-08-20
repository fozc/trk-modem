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
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ESKI fider modeli (uint32 cihaz kimligi). Yeden sozlesme (EUI-64 + 96B
 * blok) rf_feeder_t uzerindedir; web/NVRAM katmani gecene kadar (plan
 * Faz 1-2) bu yapi gecici olarak kalir ve sonra kaldirilir.
 */
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


/** EUI-64 device identity length in bytes  */
#define RF_EUI64_LEN    8U
/** EUI-64 as zero-terminated hex string: 16 digits + NUL. */
#define RF_EUI64_HEX_LEN 17U

/**
 * @brief 96 baytlik ayirici konfigurasyon blogu (spec R2 Ek-A).
 *
 * Packed, little-endian. Bu yapi SCP uzerinden birebir gelip gider
 * (0x20 CFG_READ_ALL govdesi / 0x22 CFG_WRITE govdesi) ve ayni zamanda
 * RAM store'un (rf_feeder_t.config) ayar birimidir.
 *
 * Veri alani: @0-66. RF-rezervi: @67-93 (0x00). CRC-16: @94-95
 * (kapsam: @0-93) - yalniz FRAM-boot icerik butunlugu; 0x22-yazim
 * yolunda cihaz tarafindan DENETLENMEZ (R2-ek4).
 *
 * Maskeli alanlar (spec R2 section 5.3-7): Zone/Fider/Phase (@0-2) ve
 * RF ailesi (@57-66) cihaz tarafinda RF-CFG yaziminda YOK SAYILIR; RMW
 * akisinda cihazdan okunan degerleriyle aynen geri konur.
 */
typedef struct
{
    /* Toploloji (ofset 0-2) - MASKELI (spec R2 section 5.3-7) */
    uint8_t  zone_id;                /**< Ofset 0:  Bolge kimligi (0-7) */
    uint8_t  fider_id;               /**< Ofset 1:  Fider (0 = provizyonsuz) */
    uint8_t  phase_id;               /**< Ofset 2:  Faz L1=1, L2=2, L3=3 */

    /* Koruma ayarlari (ofset 3-22) - yazilabilir */
    float    nominal_current;        /**< Ofset 3:  Nominal hat akimi (A) */
    float    ia_threshold;           /**< Ofset 7:  Asiri akim esigi (A) */
    float    is_safety;              /**< Ofset 11: Guvenlik akim esigi (A) */
    float    di_dt_threshold;        /**< Ofset 15: di/dt esigi (A/s) */
    float    line_break_threshold;   /**< Ofset 19: Hat kopma esigi (A) */

    /* Zamanlama ve karar motoru (ofset 23-41) - yazilabilir */
    uint8_t  line_frequency;         /**< Ofset 23: Sebeke frekansi {50,60} (Hz) */
    uint16_t threshold_ms;           /**< Ofset 24: Esik teyit (ms) */
    uint16_t t_reclaim_sec;          /**< Ofset 26: Ariza sayaci sifirlama (s) */
    uint16_t t_mem_dead_sec;         /**< Ofset 28: Olu hat bellek (s) */
    uint16_t inrush_timer_ms;        /**< Ofset 30: Inrush penceresi (ms) */
    float    inrush_multiplier;      /**< Ofset 32: Inrush carpani (x) */
    uint16_t dead_line_verify_ms;    /**< Ofset 36: Olu hat dogrulama (ms) */
    uint16_t sync_trip_delay_ms;     /**< Ofset 38: Senkron trip gecikmesi (ms) */
    uint16_t trip_pulse_duration_ms; /**< Ofset 40: Acma darbe suresi (ms) */

    /* Algoritma ayarlari (ofset 42-45) - yazilabilir */
    uint8_t  set_count;              /**< Ofset 42: Acmaya kadar ariza sayisi (1-4) */
    uint8_t  operating_mode;         /**< Ofset 43: Calisma modu {0,1} */
    uint8_t  trip_mode;              /**< Ofset 44: Acma modu {0,1} */
    uint8_t  inrush_100hz_ratio;     /**< Ofset 45: Inrush 100Hz orani (%) */

    /* Cold Load Pickup (ofset 46-52) - yazilabilir */
    uint8_t  clp_enabled;            /**< Ofset 46: CLP etkin {0,1} */
    float    clp_multiplier;         /**< Ofset 47: CLP carpani (x) */
    uint16_t clp_duration_ms;        /**< Ofset 51: CLP pencere (ms) */

    /* Guc elektronigi (ofset 53-56) - yazilabilir */
    float    vtrip_target;           /**< Ofset 53: Acma kond. hedef gerilimi (V) */

    /* RF ailesi (ofset 57-66) - MASKELI (spec R2 section 5.3-7) */
    uint8_t  rf_channel;             /**< Ofset 57: RF kanali (kesif/atama kaynagi) */
    uint8_t  rf_atim;                /**< Ofset 58: RF atim (kesif/atama kaynagi) */
    uint8_t  last_modem_eui[8];      /**< Ofset 59-66: MSB-first */

    /* Rezerv ve butunluk */
    uint8_t  rf_reserved[27];        /**< Ofset 67-93: RF-rezervi (0x00) */
    uint16_t crc16;                  /**< Ofset 94-95: CRC-16 (kapsam 0-93) */
} __attribute__((packed)) rf_feeder_config_t;

/* Derleyici-hakemli yerlesim bekcileri (spec R2 Ek-A). */
_Static_assert(sizeof(rf_feeder_config_t) == 96U,
               "rf_feeder_config_t must be exactly 96 bytes (spec Ek-A)");
_Static_assert(offsetof(rf_feeder_config_t, zone_id) == 0U, "Ek-A: Zone_ID@0");
_Static_assert(offsetof(rf_feeder_config_t, fider_id) == 1U, "Ek-A: Fider_ID@1");
_Static_assert(offsetof(rf_feeder_config_t, phase_id) == 2U, "Ek-A: Phase_ID@2");
_Static_assert(offsetof(rf_feeder_config_t, nominal_current) == 3U, "Ek-A: Nominal@3");
_Static_assert(offsetof(rf_feeder_config_t, ia_threshold) == 7U, "Ek-A: Ia@7");
_Static_assert(offsetof(rf_feeder_config_t, is_safety) == 11U, "Ek-A: Is_Safety@11");
_Static_assert(offsetof(rf_feeder_config_t, di_dt_threshold) == 15U, "Ek-A: di/dt@15");
_Static_assert(offsetof(rf_feeder_config_t, line_break_threshold) == 19U, "Ek-A: LineBreak@19");
_Static_assert(offsetof(rf_feeder_config_t, line_frequency) == 23U, "Ek-A: Freq@23");
_Static_assert(offsetof(rf_feeder_config_t, threshold_ms) == 24U, "Ek-A: Threshold_ms@24");
_Static_assert(offsetof(rf_feeder_config_t, t_reclaim_sec) == 26U, "Ek-A: T_Reclaim@26");
_Static_assert(offsetof(rf_feeder_config_t, t_mem_dead_sec) == 28U, "Ek-A: T_Mem_Dead@28");
_Static_assert(offsetof(rf_feeder_config_t, inrush_timer_ms) == 30U, "Ek-A: Inrush_Timer@30");
_Static_assert(offsetof(rf_feeder_config_t, inrush_multiplier) == 32U, "Ek-A: Inrush_Mult@32");
_Static_assert(offsetof(rf_feeder_config_t, dead_line_verify_ms) == 36U, "Ek-A: DeadLine@36");
_Static_assert(offsetof(rf_feeder_config_t, sync_trip_delay_ms) == 38U, "Ek-A: SyncTrip@38");
_Static_assert(offsetof(rf_feeder_config_t, trip_pulse_duration_ms) == 40U, "Ek-A: TripPulse@40");
_Static_assert(offsetof(rf_feeder_config_t, set_count) == 42U, "Ek-A: Set_Count@42");
_Static_assert(offsetof(rf_feeder_config_t, operating_mode) == 43U, "Ek-A: OpMode@43");
_Static_assert(offsetof(rf_feeder_config_t, trip_mode) == 44U, "Ek-A: TripMode@44");
_Static_assert(offsetof(rf_feeder_config_t, inrush_100hz_ratio) == 45U, "Ek-A: Inrush100Hz@45");
_Static_assert(offsetof(rf_feeder_config_t, clp_enabled) == 46U, "Ek-A: CLP_En@46");
_Static_assert(offsetof(rf_feeder_config_t, clp_multiplier) == 47U, "Ek-A: CLP_Mult@47");
_Static_assert(offsetof(rf_feeder_config_t, clp_duration_ms) == 51U, "Ek-A: CLP_Dur@51");
_Static_assert(offsetof(rf_feeder_config_t, vtrip_target) == 53U, "Ek-A: Vtrip@53");
_Static_assert(offsetof(rf_feeder_config_t, rf_channel) == 57U, "Ek-A: RF_Channel@57");
_Static_assert(offsetof(rf_feeder_config_t, rf_atim) == 58U, "Ek-A: RF_Atim@58");
_Static_assert(offsetof(rf_feeder_config_t, last_modem_eui) == 59U, "Ek-A: LastModemEUI@59");
_Static_assert(offsetof(rf_feeder_config_t, rf_reserved) == 67U, "Ek-A: RF-reserve@67");
_Static_assert(offsetof(rf_feeder_config_t, crc16) == 94U, "Ek-A: CRC16@94");

/** @brief Blok CRC-16'sinin hesaplandigi bayt araligi (ofset 0-93). */
#define RF_CONFIG_CRC_RANGE_LEN   94U

/** @brief Yazilabilir alan araligi @3-56 (maske disi; spec R2 Ek-A). */
#define RF_CONFIG_WRITABLE_OFFS   3U
#define RF_CONFIG_WRITABLE_LEN    54U

/** @brief Phase_ID gecerli degerleri (spec R2 section 4.1). */
#define RF_CONFIG_PHASE_L1        1U
#define RF_CONFIG_PHASE_L2        2U
#define RF_CONFIG_PHASE_L3        3U

/*
 * Feeder-centric RF separator configuration store unit (RAM model) -
 * web arayuzunun calistigi ust katman kaydi.
 *
 * Tek-blok modeli: ayar SSOT'u rf_feeder_config_t'dir (SCP wire
 * sozlesmesi = RAM birimi). Blok icindeki maskeli alanlar (Zone/Fider/
 * Phase @0-2, RF ailesi @57-66) cihazdan okunan son imaji tasir -
 * yazimda cihaz yok sayar (spec R2 section 5.3-7). EUI-64 kimlikleri
 * blokta YOKTUR (spec R2 section 6: atama katmani); in_use UI durumudur
 * (Fider_ID=0 karsiligi).
 */
typedef struct
{
    rf_feeder_config_t config;    /**< 96B ayar blogu: writable @3-56 RTU'nun; maskeli = cihazdan son-okunan */

    /* Device identity: factory EUI-64 per phase, wire order MSB-first.
     * All-zero array = unassigned (spec R2 section 6). */
    uint8_t r_eui64[RF_EUI64_LEN];
    uint8_t s_eui64[RF_EUI64_LEN];
    uint8_t t_eui64[RF_EUI64_LEN];

    bool    in_use;               /**< UI durumu; cihaz karsiligi Fider_ID=0 */
} rf_feeder_t;

/* Istemeyerek yerlesim degisikligini derleme hatasi yap: bu deger
 * degisirse NVRAM_SCHEMA_VERSION ile birlikte artirilmalidir. */
_Static_assert(sizeof(rf_feeder_t) == (96U + 24U + 1U),
               "rf_feeder_t layout changed - bump NVRAM_SCHEMA_VERSION");







#ifdef __cplusplus
}
#endif

#endif /* RF_RF_TYPES_H_ */
