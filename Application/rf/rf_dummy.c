/*
 * rf_dummy.c
 *
 * Dummy RF data generator.
 *
 * Simulation model (per enabled feeder, per phase):
 *
 *   faz_akimi       : sawtooth ramp  10 % -> 110 % of nominal over 100 steps.
 *   faz_hata_akimi  : non-zero only when faz_akimi exceeds 80 % of nominal.
 *   sistem_sicakligi: linear ramp 25 -> 65 degC over 200 steps, wraps.
 *   sistem_dc_gerilimi: 24 V + LFSR noise.
 *   v5vdc / v3v3dc  : 500/330 + LFSR noise (unit = 0.01 V).
 *   actirma_dc_gerilimi: fixed 12 V.
 *   rssi / lqi      : base values with LFSR noise.
 *   aktif_ariza_sayaci: increments every 50 steps (capped at 10).
 *   gecmis_acma_sayisi: increments every 200 steps.
 *   last_tx         : bsp_get_tick() / 1000 + 1 (always non-zero).
 *
 * Config-derived fields (hat_id, zone_id, mode, hat_frekansi):
 *   taken from rf_config when non-zero, else synthetic non-zero defaults.
 *   Phase EUI-64 identity is seeded in config only; monitor device_id
 *   display values stay synthetic uint32 (rf_monitor_t, scope disi).
 *
 * On init, rf_config is seeded with synthetic defaults for any feeder
 * that has never been configured (hat_id == 0 and all device_ids == 0).
 * rf_store_sync() is NOT called, so these values are not persisted
 * to flash until the user explicitly saves from the web UI.
 *
 * Pseudo-random noise: 16-bit Galois LFSR (no stdlib dependency).
 * Rate control: caller (Contiki timer in app_main.c) controls call frequency;
 * rf_dummy_tick() updates unconditionally on every call.
 */

#include "rf_dummy.h"
#include "rf.h"
#include "rf_config.h"
#include "rf_types.h"
#include "modem_types.h"
#include "bsp.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================================
 * COMPILE-TIME CONFIGURATION
 * ============================================================================ */

/** Number of steps in one current ramp cycle (10 % -> 110 % of nominal). */
#define RF_DUMMY_RAMP_STEPS      100U

/** Temperature ramp: steps to go from 25 C to 65 C. */
#define RF_DUMMY_TEMP_STEPS      200U

/** Fault counter increment interval in simulation steps. */
#define RF_DUMMY_FAULT_INTERVAL  50U

/** Trip counter increment interval in simulation steps. */
#define RF_DUMMY_TRIP_INTERVAL   200U

/** Nominal current fallback when config reports 0 (unit: 0.1 A - 100 = 10 A). */
#define RF_DUMMY_FALLBACK_NOMINAL_x10  100U

/* ============================================================================
 * INTERNAL TYPES
 * ============================================================================ */

typedef struct
{
    uint16_t lfsr;             /**< Per-feeder 16-bit Galois LFSR state.   */
    uint32_t step;             /**< Simulation step counter.               */
    uint8_t  ariza_sayaci;     /**< Active fault counter (capped at 10).   */
    uint16_t acma_sayisi;      /**< Historical trip counter.               */
} rf_dummy_feeder_t;

/* ============================================================================
 * MODULE STATE
 * ============================================================================ */

static rf_dummy_feeder_t feeder_state[MAX_POWER_LINE_COUNT];
static bool              initialized = false;

/* ============================================================================
 * PRIVATE HELPERS
 * ============================================================================ */

/**
 * @brief Advance a 16-bit Galois LFSR by one step.
 *
 * Polynomial 0xB400 provides a period of 65535.
 * Guaranteed to never return 0 for a non-zero seed.
 *
 * @param lfsr  Current LFSR state (must be non-zero).
 * @return      Next LFSR state.
 */
static uint16_t lfsr_next(uint16_t lfsr)
{
    const uint8_t lsb = (uint8_t)(lfsr & 1U);
    lfsr >>= 1U;
    if (lsb != 0U)
    {
        lfsr ^= 0xB400U;
    }
    return lfsr;
}

/**
 * @brief Return a signed noise value in the range [-half, +half].
 *
 * Advances the feeder's LFSR and maps it to a symmetric range.
 *
 * @param[in,out] feeder  Feeder state (LFSR updated in place).
 * @param         half      Half-range (inclusive).  Must be > 0.
 * @return                  Noise value in [-half, +half].
 */
static int16_t noise_sym(rf_dummy_feeder_t *feeder, uint16_t half)
{
    feeder->lfsr = lfsr_next(feeder->lfsr);
    return (int16_t)((int32_t)(feeder->lfsr % ((uint16_t)(2U * half + 1U))) - (int32_t)half);
}

/**
 * @brief Saturating cast of int32_t to uint16_t (clamp to [0, 65535]).
 */
static uint16_t sat_u16(int32_t v)
{
    if (v < 0)          { return 0U;     }
    if (v > 65535)      { return 65535U; }
    return (uint16_t)v;
}

/**
 * @brief Saturating cast of int32_t to uint8_t (clamp to [0, 255]).
 */
static uint8_t sat_u8(int32_t v)
{
    if (v < 0)    { return 0U;   }
    if (v > 255)  { return 255U; }
    return (uint8_t)v;
}

/**
 * @brief Saturating cast of int32_t to int8_t (clamp to [-128, 127]).
 */
static int8_t sat_i8(int32_t v)
{
    if (v < -128) { return -128; }
    if (v > 127)  { return 127;  }
    return (int8_t)v;
}

/* ============================================================================
 * SIMULATION STEP
 * ============================================================================ */

/**
 * @brief Return a non-zero pseudo-Unix timestamp.
 *
 * Uses bsp_get_tick() / 1000 + 1 so last_tx is always >= 1, which
 * makes the RSynced flag true in the web UI regardless of RTC state.
 */
static uint32_t dummy_now(void)
{
    return (bsp_get_tick() / 1000U) + 1U;
}

/**
 * @brief Seed rf_config with synthetic non-zero values for an unconfigured feeder.
 *
 * Called only when hat_id == 0 AND all phase EUI-64s are zero.
 * Does NOT call rf_store_sync() so nothing is persisted to flash.
 *
 * @param line_id  Feeder index [0, MAX_POWER_LINE_COUNT).
 */
static void seed_config(uint32_t line_id)
{
    rf_feeder_t *cfg = rf_store_get_mutable((feeder_id_t)line_id);
    if (cfg == NULL) { return; }

    /* Sentetik EUI-64'ler (MSB-first; gercek cihaz kimlikleri degil). */
    const uint8_t base_eui[RF_EUI64_LEN] = {0xA4U, 0x05U, 0x67U, 0x82U,
                                            0x1CU, 0x3BU, 0x00U, 0x00U};

    cfg->in_use                             = true;
    cfg->config.fider_id                       = (uint8_t)(line_id + 1U);
    cfg->config.zone_id                        = 1U;
    memcpy(cfg->r_eui64, base_eui, RF_EUI64_LEN);
    memcpy(cfg->s_eui64, base_eui, RF_EUI64_LEN);
    memcpy(cfg->t_eui64, base_eui, RF_EUI64_LEN);
    cfg->r_eui64[7] = (uint8_t)(0x10U + (line_id * 3U) + 0U);
    cfg->s_eui64[7] = (uint8_t)(0x10U + (line_id * 3U) + 1U);
    cfg->t_eui64[7] = (uint8_t)(0x10U + (line_id * 3U) + 2U);
    cfg->config.operating_mode                 = 1U;
    cfg->config.line_frequency                 = 50U;
    cfg->config.nominal_current                = 100.0f;
    cfg->config.ia_threshold                   = 150.0f;
    cfg->config.di_dt_threshold                = 1000.0f;   /* spec araligi [250,2500] */
    cfg->config.line_break_threshold           = 2.0f;      /* spec araligi [0.30,5.00] */
    cfg->config.dead_line_verify_ms            = 200U;      /* spec araligi [80,200] */
    cfg->config.set_count                      = 3U;
    cfg->config.t_reclaim_sec                  = 30U;       /* spec araligi [10,300] */
}

/**
 * @brief Build one rf_monitor_t sample for a single feeder.
 */
static void build_monitor(uint32_t line_id,
                          const rf_feeder_t   *cfg,
                          rf_monitor_t        *mon)
{
    rf_dummy_feeder_t *fstate = &feeder_state[line_id];
    const uint32_t     now = dummy_now();
    uint32_t           ph;

    /* ---- effective config values: use non-zero synthetic defaults ---- */
    const uint8_t  eff_hat_id  = (cfg->config.fider_id  != 0U) ? cfg->config.fider_id  : (uint8_t)(line_id + 1U);
    const uint8_t  eff_zone_id = (cfg->config.zone_id != 0U) ? cfg->config.zone_id : 1U;
    const uint8_t  eff_mode    = (cfg->config.operating_mode != 0U) ? cfg->config.operating_mode     : 1U;
    const uint8_t  eff_freq    = (cfg->config.line_frequency >= 45U) ? cfg->config.line_frequency : 50U;
    /* Monitor device_id alani (uint32) sentetik kalir; EUI-64 kimligi
     * config'te tasinir ve monitor tablosunda gosterilmez (scope disi). */
    const uint32_t base_dev    = 0x1001U + (uint32_t)line_id * 3U;
    const uint32_t eff_r_dev   = base_dev;
    const uint32_t eff_s_dev   = base_dev + 1U;
    const uint32_t eff_t_dev   = base_dev + 2U;

    /* ---- nominal current (0.1-A units) ---- */
    const uint16_t nominal_x10 =
        (cfg->config.nominal_current > 0.0f)
        ? sat_u16((int32_t)(cfg->config.nominal_current * 10.0f))
        : RF_DUMMY_FALLBACK_NOMINAL_x10;

    /* ---- phase current: sawtooth 10 % -> 110 % of nominal ----------- */
    const uint32_t ramp_pct   = 10U + (fstate->step % RF_DUMMY_RAMP_STEPS);
    const uint16_t faz_akimi  = sat_u16((int32_t)((uint32_t)nominal_x10 * ramp_pct / 100U));

    /* ---- fault current: non-zero when >80 % of nominal -------------- */
    const uint16_t threshold_x10 = sat_u16((int32_t)((uint32_t)nominal_x10 * 80U / 100U));
    const uint16_t faz_hata = (faz_akimi > threshold_x10)
                              ? (uint16_t)(faz_akimi - threshold_x10)
                              : 0U;

    /* ---- temperature: linear ramp 25..65 C, wraps ------------------ */
    const int32_t temp_raw = 25 + (int32_t)((fstate->step % RF_DUMMY_TEMP_STEPS) * 40U / RF_DUMMY_TEMP_STEPS);

    /* ---- event counters ---------------------------------------------- */
    if ((fstate->step > 0U) && ((fstate->step % RF_DUMMY_FAULT_INTERVAL) == 0U))
    {
        if (fstate->ariza_sayaci < 10U) { fstate->ariza_sayaci++; }
    }
    if ((fstate->step > 0U) && ((fstate->step % RF_DUMMY_TRIP_INTERVAL) == 0U))
    {
        fstate->acma_sayisi++;     /* wraps naturally at uint16 max */
    }

    /* ---- populate per-phase fields ----------------------------------- */
    for (ph = 0U; ph < (uint32_t)PHASE_MAX; ph++)
    {
        const int16_t v_noise   = noise_sym(fstate, 10U);
        const int16_t v33_noise = noise_sym(fstate,  5U);
        const int16_t rssi_n    = noise_sym(fstate,  8U);
        const int16_t lqi_n     = noise_sym(fstate, 10U);
        const int16_t dc_noise  = noise_sym(fstate,  1U);

        mon->hat_id[ph]         = eff_hat_id;
        mon->zone_id[ph]        = eff_zone_id;
        mon->calisma_modu[ph]   = eff_mode;
        mon->hat_frekansi[ph]   = eff_freq;
        mon->last_tx[ph]        = now;

        /* Device IDs: L1=R, L2=S, L3=T */
        if (ph == (uint32_t)PHASE_L1)      { mon->device_id[ph] = eff_r_dev; }
        else if (ph == (uint32_t)PHASE_L2) { mon->device_id[ph] = eff_s_dev; }
        else                               { mon->device_id[ph] = eff_t_dev; }

        mon->sistem_sicakligi[ph]            = sat_i8(temp_raw);
        mon->sistem_dc_gerilimi[ph]          = sat_u8(24 + (int32_t)dc_noise);
        mon->v5vdc[ph]                       = sat_u16(500 + (int32_t)v_noise);
        mon->v3v3dc[ph]                      = sat_u16(330 + (int32_t)v33_noise);
        mon->actirma_dc_gerilimi[ph]         = 12U;
        mon->faz_akimi[ph]                   = faz_akimi;
        mon->faz_hata_akimi[ph]              = faz_hata;
        mon->aktif_sifirlama_zamanlayici_durumu[ph] = 0U;
        mon->aktif_ariza_sayaci[ph]          = fstate->ariza_sayaci;
        mon->gecmis_acma_sayisi[ph]          = fstate->acma_sayisi;
        mon->rssi[ph]                        = sat_u8(185 + (int32_t)rssi_n);
        mon->lqi[ph]                         = sat_u8(220 + (int32_t)lqi_n);
    }
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

void rf_dummy_init(void)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *cfg;

        /* Seed each feeder's LFSR with a distinct non-zero value. */
        feeder_state[i].lfsr         = (uint16_t)(0xACE1U ^ (i * 0x3571U));
        feeder_state[i].step         = 0U;
        feeder_state[i].ariza_sayaci = 0U;
        feeder_state[i].acma_sayisi  = 0U;

        /* Ensure seed is never 0 (LFSR invariant). */
        if (feeder_state[i].lfsr == 0U) { feeder_state[i].lfsr = 0xDEADU; }

        /* Seed rf_config with synthetic defaults for truly unconfigured feeders.
         * A feeder is considered unconfigured when hat_id == 0 and all phase
         * EUI-64s are zero (fresh/erased NVRAM state).
         * rf_store_sync() is NOT called here, so nothing is persisted
         * to flash until the user explicitly saves from the web UI. */
        cfg = rf_store_get((feeder_id_t)i);
        if ((cfg != NULL) &&
            (cfg->config.fider_id == 0U) &&
            rf_eui64_is_zero(cfg->r_eui64) &&
            rf_eui64_is_zero(cfg->s_eui64) &&
            rf_eui64_is_zero(cfg->t_eui64))
        {
            seed_config(i);
        }
    }

    initialized = true;

    /* Pre-populate rf_monitor immediately so the first HTTP GET already
     * shows non-zero values without waiting for the first Contiki tick. */
    rf_dummy_tick();
}

void rf_dummy_tick(void)
{
    uint32_t     i;
    rf_monitor_t mon;

    if (!initialized) { return; }

    /* No internal rate gate: the caller (Contiki timer in app_main.c)
     * is responsible for controlling call frequency. */

    for (i = 0U; i < (uint32_t)MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *cfg = rf_store_get((feeder_id_t)i);
        if (cfg == NULL) { continue; }

        (void)memset(&mon, 0, sizeof(mon));
        build_monitor(i, cfg, &mon);
        (void)rf_set_monitor(i, &mon);

        feeder_state[i].step++;
    }
}
