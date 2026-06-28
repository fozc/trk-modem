/*
 * gsm_info.h
 *
 * Centralised runtime storage for GSM module information.
 * All AT-parsed data (IMEI, ICCID, IMSI, signal, network state, etc.)
 * is written here by the init/periodical callbacks and read by the
 * application layer through getter functions.
 */

#ifndef GSM2_GSM_INFO_H_
#define GSM2_GSM_INFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "gsm_types.h"

/* ---------- Module model identifier ---------- */

typedef enum
{
    GSM_MODULE_UNDEFINED = 0,
    GSM_MODULE_LE910R1,
} gsm_module_model_t;

/* ---------- SIM card state ---------- */

typedef enum
{
    GSM_SIM_STATE_UNKNOWN = 0,
    GSM_SIM_STATE_READY,
    GSM_SIM_STATE_PIN_REQUIRED,
    GSM_SIM_STATE_PUK_REQUIRED,
    GSM_SIM_STATE_BUSY,
    GSM_SIM_STATE_NOT_INSERTED,
    GSM_SIM_STATE_FAILURE,
    GSM_SIM_STATE_ERROR,
} gsm_sim_state_t;

/* ---------- Network registration state ---------- */

typedef enum
{
    GSM_NET_REG_NOT_REGISTERED = 0,
    GSM_NET_REG_REGISTERED,
    GSM_NET_REG_SEARCHING,
    GSM_NET_REG_DENIED,
    GSM_NET_REG_UNKNOWN,
    GSM_NET_REG_ROAMING,
} gsm_net_reg_state_t;

/* ---------- Cell information ---------- */

typedef struct
{
    uint16_t mcc;   /**< Mobile Country Code  */
    uint16_t mnc;   /**< Mobile Network Code  */
    uint16_t lac;   /**< Location Area Code   */
    uint16_t ci;    /**< Cell ID              */
} gsm_info_cell_t;



/**
 * @brief Zero-initialise all stored information.
 *        Call once at startup before the init sequence starts.
 */
void gsm_info_init(void);

/* ---------- IMEI ---------- */

/**
 * @brief Store the IMEI string (up to 15 digits + null terminator).
 * @param p_imei  Null-terminated IMEI string. Must not be NULL.
 */
void gsm_info_set_imei(const char *p_imei);

/**
 * @brief Get the stored IMEI string.
 * @return Pointer to internal null-terminated buffer (15 chars + '\0').
 */
const char *gsm_info_get_imei(void);

/* ---------- ICCID ---------- */

/**
 * @brief Store the ICCID string (up to 20 digits + null terminator).
 * @param p_iccid  Null-terminated ICCID string. Must not be NULL.
 */
void gsm_info_set_iccid(const char *p_iccid);

/**
 * @brief Get the stored ICCID string.
 * @return Pointer to internal null-terminated buffer.
 */
const char *gsm_info_get_iccid(void);

/* ---------- IMSI ---------- */

/**
 * @brief Store the IMSI string (up to 15 digits + null terminator).
 * @param p_imsi  Null-terminated IMSI string. Must not be NULL.
 */
void gsm_info_set_imsi(const char *p_imsi);

/**
 * @brief Get the stored IMSI string.
 * @return Pointer to internal null-terminated buffer.
 */
const char *gsm_info_get_imsi(void);

/* ---------- Signal quality ---------- */

void    gsm_info_set_signal_quality(uint8_t level);      /**< +CSQ value (0-31, 99=unknown) */
uint8_t gsm_info_get_signal_quality(void);

void    gsm_info_set_signal_quality_2G(uint8_t level);   /**< +CESQ rxlev (0-63, 99=unknown) */
uint8_t gsm_info_get_signal_quality_2G(void);

void    gsm_info_set_signal_quality_3G(uint8_t level);   /**< +CESQ rscp  (0-96, 255=unknown) */
uint8_t gsm_info_get_signal_quality_3G(void);

void    gsm_info_set_signal_quality_4G(uint8_t level);   /**< +CESQ rsrp  (0-97, 255=unknown) */
uint8_t gsm_info_get_signal_quality_4G(void);

void    gsm_info_set_2G_ber(uint8_t value);   /**< +CESQ ber  (2G, 0-7 RXQUAL, 99=unknown) */
uint8_t gsm_info_get_2G_ber(void);

void    gsm_info_set_3G_ecno(uint8_t value);  /**< +CESQ ecno (3G, 0-49, 255=unknown) */
uint8_t gsm_info_get_3G_ecno(void);

void    gsm_info_set_4G_rsrq(uint8_t value);  /**< +CESQ rsrq (4G, 0-34, 255=unknown) */
uint8_t gsm_info_get_4G_rsrq(void);

/* ---------- Access technology (RAT) ---------- */

void    gsm_info_set_access_technology(uint8_t rat);
uint8_t gsm_info_get_access_technology(void);
network_generation_t get_network_generation(void);
const char* get_access_tech_str(gsm_access_technology_t tech);
/* ---------- Module model & firmware ---------- */

void                gsm_info_set_module_model(gsm_module_model_t model);
gsm_module_model_t  gsm_info_get_module_model(void);

/**
 * @param p_ver  Null-terminated version string (max 15 chars).
 */
void        gsm_info_set_fw_version(const char *p_ver);
const char *gsm_info_get_fw_version(void);

/* ---------- SIM card state ---------- */

void             gsm_info_set_sim_state(gsm_sim_state_t state);
gsm_sim_state_t  gsm_info_get_sim_state(void);

/* ---------- Network registration ---------- */

void                gsm_info_set_creg(gsm_net_reg_state_t state);
gsm_net_reg_state_t gsm_info_get_creg(void);

void                gsm_info_set_cgreg(gsm_net_reg_state_t state);
gsm_net_reg_state_t gsm_info_get_cgreg(void);

void                gsm_info_set_cereg(gsm_net_reg_state_t state);
gsm_net_reg_state_t gsm_info_get_cereg(void);

/* ---------- Cell information ---------- */

void gsm_info_set_cell(const gsm_info_cell_t *p_cell);
void gsm_info_get_cell(gsm_info_cell_t *p_cell);

/* ---------- GPRS / IP ---------- */

void     gsm_info_set_gprs_state(uint8_t state);   /**< 0 = disconnected, 1 = connected */
uint8_t  gsm_info_get_gprs_state(void);

void     gsm_info_set_ip(uint32_t ip_u32);          /**< Host byte order */
uint32_t gsm_info_get_ip(void);

/* ---------- RX/TX counters ---------- */

void     gsm_info_add_rx_bytes(uint32_t count);
void     gsm_info_add_tx_bytes(uint32_t count);
void     gsm_info_get_rxtx_counters(uint32_t *p_tx, uint32_t *p_rx);

/* ---------- +CESQ telemetry report ---------- */

/** Bir CESQ alaninin fiziksel birimi. */
typedef enum
{
    GSM_CESQ_UNIT_NONE = 0,  /**< ber — boyutsuz RXQUAL indeksi */
    GSM_CESQ_UNIT_DBM,       /**< rxlev / rscp / rsrp */
    GSM_CESQ_UNIT_DB,        /**< ecno / rsrq */
} gsm_cesq_unit_t;

/** Tek bir +CESQ alani: ham deger + fiziksel karsiligi + kalitatif etiket. */
typedef struct
{
    uint8_t          raw;        /**< Ham +CESQ degeri                          */
    int16_t          physical;   /**< x10 birim (or. -800 = -80.0 dBm, -75=-7.5 dB) */
    bool             available;  /**< Sentinel (99/255) degilse true            */
    gsm_cesq_unit_t  unit;       /**< Fiziksel birim                            */
    const char      *label;      /**< "Excellent".."Very Weak" veya "N/A"       */
} gsm_cesq_field_t;

/** Tum +CESQ alanlarinin yorumlanmis hali. Ornek ciktiya karsilik gelir. */
typedef struct
{
    const char      *active_technology;  /**< "4G / LTE" | "3G / WCDMA" | "2G / GSM" | "None" */
    gsm_cesq_field_t rxlev;   /**< 2G GSM   - sinyal gucu   */
    gsm_cesq_field_t ber;     /**< 2G GSM   - kalite        */
    gsm_cesq_field_t rscp;    /**< 3G WCDMA - sinyal gucu   */
    gsm_cesq_field_t ecno;    /**< 3G WCDMA - kalite        */
    gsm_cesq_field_t rsrq;    /**< 4G LTE   - kalite        */
    gsm_cesq_field_t rsrp;    /**< 4G LTE   - sinyal gucu   */
} gsm_cesq_report_t;

/**
 * @brief Tum +CESQ alanlarini okuyup fiziksel degere (dBm/dB) ve kalitatif
 *        etikete ("Very Weak".."Excellent") cevirir.
 *
 * Aktif teknoloji onceligi: 4G (rsrp) > 3G (rscp) > 2G (rxlev).
 * Fiziksel deger x10 (deci) birimindedir (0.5 adimli rsrq/ecno icin float gerekmez).
 *
 * @param p_report  Doldurulacak cikti yapisi. NULL ise no-op.
 */
void gsm_info_get_cesq_report(gsm_cesq_report_t *p_report);

#ifdef __cplusplus
}
#endif

#endif /* GSM2_GSM_INFO_H_ */
