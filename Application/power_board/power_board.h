/**
 * @file  power_board.h
 * @brief PowerBoard I2C-slave protocol access API (upper-board side).
 *
 * The upper board is the I2C SLAVE (7-bit 0x48). The PowerBoard is the I2C
 * MASTER and pushes three blocks into the slave register file:
 *   - TELEMETRY  0x00..0x5F  (96 B, ~1 s)
 *   - POWER      0xA0..0xB4  (21 B, ~1 s, separate burst after telemetry)
 *   - LASTGASP   0x80..0x9A  (27 B, power-fail event)
 * The master reads two blocks the upper board publishes:
 *   - RESTORE    0x60..0x71  (18 B, boot only)
 *   - STATBLK    0x72..0x78  (7 B, ~8 s; REC_ACK + config)
 *
 * Reference: doc/PowerBoard_I2C_Protocol.md (Rev 1.0, PROT_VER 0x09).
 * All multi-byte fields are MSB-first (big-endian). Every block's LAST byte
 * is an integrity byte: XOR(all preceding bytes) ^ 0x5A (salt). Block layout
 * authority: power-card repo, upper_board_reference/pwr_i2c_packets.h.
 */

#ifndef POWER_BOARD_POWER_BOARD_H_
#define POWER_BOARD_POWER_BOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ======================================================================
 *  Constants
 * ====================================================================== */

/** Telemetry region base + size pushed by the master (0x00..0x5F). */
#define POWER_BOARD_TLM_BASE           ((uint8_t)0x00U)
#define POWER_BOARD_TLM_SIZE           ((uint8_t)96U)

/** Expected protocol version (single contract; frames at another
 *  PROT_VER are rejected - see power_board_decode.c). */
#define POWER_BOARD_PROT_VER           ((uint8_t)0x09U)

/** Integrity salt applied to every block's last-byte XSUM. */
#define POWER_BOARD_XSUM_SALT          ((uint8_t)0x5AU)

/** PUSH block bases / lengths / markers (PROT_VER 0x09). */
#define POWER_BOARD_PWR_BASE           ((uint8_t)0xA0U)
#define POWER_BOARD_PWR_LEN            ((uint8_t)21U)
#define POWER_BOARD_LG_BASE            ((uint8_t)0x80U)
#define POWER_BOARD_LG_LEN             ((uint8_t)27U)
#define POWER_BOARD_LG_MARKER          ((uint8_t)0xB5U)

/** Restore block base address / marker / length (doc section 7). */
#define POWER_BOARD_RESTORE_BASE       ((uint8_t)0x60U)
#define POWER_BOARD_RESTORE_LEN        ((uint8_t)18U)   /* 0x60..0x71 */
#define POWER_BOARD_RESTORE_MARKER     ((uint8_t)0xA5U)

/** STATBLK (config) base / length (PULL, 0x72..0x78). */
#define POWER_BOARD_STATBLK_BASE       ((uint8_t)0x72U)
#define POWER_BOARD_STATBLK_LEN        ((uint8_t)7U)

/** Config defaults (doc section 9b.3 = PowerBoard's own defaults). */
#define POWER_BOARD_CFG_DEF_REC_ACK    ((uint8_t)0x00U)
#define POWER_BOARD_CFG_DEF_GEN        ((uint8_t)0x01U)
#define POWER_BOARD_CFG_DEF_CAP_AH     ((uint8_t)12U)
#define POWER_BOARD_CFG_DEF_CRATE      ((uint8_t)10U)
#define POWER_BOARD_CFG_DEF_TCAL_Q15   ((uint16_t)0x0000U)

/** Config field limits (PowerBoard also clamps to these). */
#define POWER_BOARD_CFG_CAP_AH_MIN     ((uint8_t)7U)
#define POWER_BOARD_CFG_CAP_AH_MAX     ((uint8_t)50U)
#define POWER_BOARD_CFG_CRATE_MIN      ((uint8_t)5U)   /* 0.05C */
#define POWER_BOARD_CFG_CRATE_MAX      ((uint8_t)20U)  /* 0.20C */
#define POWER_BOARD_CFG_TCAL_UNSET     ((uint16_t)0x0000U)

/** Sentinel: BATT_TEMP (0x24) invalid / no NTC. */
#define POWER_BOARD_BATT_TEMP_INVALID  ((int16_t)-9990)

/** Sentinel: BOARD_TEMP (0x22) sensor error. */
#define POWER_BOARD_BOARD_TEMP_ERROR   ((int8_t)-128)

/**
 * @brief CHG_REAL (0x4A): derived "really charging" verdict.
 *
 * The chip's charge-phase claim compared against measured current. Only
 * PB_CHG_REAL_CHARGING means current actually flows into the battery.
 */
typedef enum
{
    PB_CHG_REAL_NO_VERDICT   = 0,  /**< not enough evidence yet            */
    PB_CHG_REAL_NOT_CHARGING = 1,  /**< chip says not charging              */
    PB_CHG_REAL_CHARGING     = 2,  /**< chip says charging AND current flows */
    PB_CHG_REAL_CONFLICT     = 3   /**< chip says charging BUT no current    */
} pb_chg_real_t;

/**
 * @brief PSYS_ST (0x4E): validity of PSYS_MW (0xA8).
 *
 * Only PB_PSYS_ST_MEASURED may be trusted for decisions; other states are
 * the startup seed, stale or explicitly invalid values.
 */
typedef enum
{
    PB_PSYS_ST_SEED      = 0,  /**< startup seed, not measured             */
    PB_PSYS_ST_MEASURED  = 1,  /**< measured average (n=64) - trust this   */
    PB_PSYS_ST_STALE     = 2,  /**< measurement went stale                 */
    PB_PSYS_ST_INVALID   = 3   /**< explicitly invalid                     */
} pb_psys_st_t;

/** Minimum periodic-log interval accepted by the monitor. */
#define POWER_BOARD_LOG_MIN_PERIOD_MS  ((uint32_t)200U)

/** Default periodic-log interval when none is supplied. */
#define POWER_BOARD_LOG_DEF_PERIOD_MS  ((uint32_t)1000U)

/* ======================================================================
 *  Decoded block types
 * ====================================================================== */

/**
 * @brief Decoded PowerBoard telemetry snapshot (0x00..0x5F).
 *
 * Fields mirror the register map (doc section 3b). Values scaled by ten keep
 * the @c _x10 suffix (e.g. @c soh_x10 = 1000 means 100.0 %).
 */
typedef struct
{
    bool     valid;          /**< true if XSUM (0x5F) matched.              */
    uint8_t  prot_ver;       /**< 0x1E PROT_VER (expect 0x06).             */
    uint8_t  seq;            /**< 0x1D sample counter (wraps).             */
    uint8_t  rec_flag;       /**< 0x1C REC_FLAG bitfield.                  */
    uint8_t  blk_xsum;       /**< 0x5F received integrity byte.            */

    uint8_t  sys_state;      /**< 0x00 SYS_STATE.                          */
    uint8_t  sys_fault;      /**< 0x01 SYS_FAULT (BQ FAULT0 mirror).       */
    uint8_t  sys_flags;      /**< 0x02 SYS_FLAGS (live alarms).            */
    uint8_t  chg_stat_raw;   /**< 0x03 raw charge phase (REG1C[7:5]).      */

    uint16_t soh_x10;        /**< 0x04 State-of-health x10 (%).            */
    uint16_t efc;            /**< 0x06 equivalent full cycles.             */
    uint32_t equiv_hours;    /**< 0x08 25C-equivalent ageing hours.        */
    uint32_t gross_mah;      /**< 0x0C gross throughput mAh.               */

    uint16_t vbat_mv;        /**< 0x10 Battery voltage (mV).               */
    uint16_t vpv_mv;         /**< 0x12 PV voltage (mV).                    */
    uint16_t vdc_mv;         /**< 0x14 DC voltage (mV).                    */
    uint16_t rem_efc;        /**< 0x16 remaining equivalent cycles.        */
    uint16_t rem_years_x10;  /**< 0x18 remaining calendar years x10.       */
    int16_t  soc_x10;        /**< 0x1A State-of-charge x10 (%%), SIGNED:
                                  -1000..+1000, negative is normal.       */

    uint16_t ichg_ma;        /**< 0x20 Charge current (mA).                */
    int16_t  ibat_ma;        /**< 0x56 Battery current (+chg/-dis, mA).    */
    int16_t  ibus_ma;        /**< 0x58 Bus current (mA, signed).           */

    int8_t   board_temp_c;   /**< 0x22 Board NTC (degC, -128 = error).     */
    int16_t  batt_temp_x10;  /**< 0x24 Battery NTC x10 (degC, -9990 n/a).  */
    uint8_t  batt_ts;        /**< 0x23 JEITA zone 0..4.                    */

    uint8_t  chg_stat;       /**< 0x28 Charge phase 0..7 (debounced).      */
    uint8_t  chg_phase;      /**< 0x32 Charge phase mirror.                */
    uint8_t  ico_stat;       /**< 0x29 ICO state 0..3.                     */
    uint8_t  heater_state;   /**< 0x2A 0=OFF 1=ON 2=FAULT_NTC 3=NO_PV.     */
    uint8_t  batt_state;     /**< 0x2B 0=PRESENT 2=ABSENT 3=PROBING.       */
    uint8_t  batt_cap_ah;    /**< 0x2C Active capacity echo (Ah).          */
    uint8_t  batt_crate;     /**< 0x2D Active C-rate % echo.               */
    uint16_t ichg_target_ma; /**< 0x2E Computed ICHG target (mA).          */

    uint8_t  bq_fault0;      /**< 0x26 BQ REG20 raw.                       */
    uint8_t  bq_fault1;      /**< 0x27 BQ REG21 raw.                       */
    uint8_t  alarm_live;     /**< 0x30 Live alarm bits.                    */
    uint8_t  alarm_latch;    /**< 0x31 Latched alarm bits.                 */

    int32_t  delta_uwh;      /**< 0x33 Last 1 s energy delta (uWh).        */
    int32_t  total_mwh;      /**< 0x37 Running total energy (mWh).         */
    int32_t  total_mah;      /**< 0x3B Running total charge (mAh).         */
    uint8_t  bms_present;    /**< 0x3F Active BMS flag echo.               */

    uint8_t  bq_reg1b;       /**< 0x40 BQ Charger Status 0 raw.            */
    uint8_t  bq_reg1d;       /**< 0x41 BQ Charger Status 2 raw.            */
    uint8_t  bq_reg1e;       /**< 0x42 BQ Charger Status 3 raw.            */
    uint8_t  bq_reg1f;       /**< 0x43 BQ TS_STAT raw.                     */

    uint8_t  pwr_io;         /**< 0x44 STM GPIO bitmask.                   */
    uint8_t  hiz_trig;       /**< 0x45 EN_HIZ watchdog trigger count.      */
    uint8_t  mppt_trig;      /**< 0x46 MPPT watchdog trigger count.        */
    uint8_t  iindpm_trig;    /**< 0x47 IINDPM watchdog trigger count.      */
    uint8_t  acdrv_trig;     /**< 0x48 DIS_ACDRV watchdog trigger count.   */
    uint8_t  pwr_src;        /**< 0x49 Active input source + quality.      */

    uint8_t  chg_real;       /**< 0x4A pb_chg_real_t: 2 = really charging. */
    uint8_t  bq_yas_ds;      /**< 0x4B Telemetry age (0 = fresh, 0.1 s).   */
    uint8_t  bq_err_n;       /**< 0x4C BQ read error count (mod-256).      */
    uint8_t  panic_cause;    /**< 0x4D Panic cause bitmask (b0 VBAT<11 V,
                                  b1 VSYS<11 V, b2 cold, b3 hot).          */
    uint8_t  psys_st;        /**< 0x4E pb_psys_st_t: PSYS_MW validity.     */
    uint8_t  cal_ver;        /**< 0x4F Field calibration set.              */

    uint16_t bq_vsys_mv;     /**< 0x50 BQ VSYS (mV).                       */
    uint16_t bq_vbus_mv;     /**< 0x52 BQ VBUS (mV).                       */
    uint16_t stm_vbat_mv;    /**< 0x54 STM ADC VBAT (mV).                  */
    uint16_t bq_vac1_mv;     /**< 0x5A BQ VAC1 = DC input (mV).            */
    uint16_t bq_vac2_mv;     /**< 0x5C BQ VAC2 = PV input (mV).            */
    int8_t   bq_tdie_c;      /**< 0x5E BQ die temperature (degC).          */
} power_board_telemetry_t;

/**
 * @brief Decoded instantaneous power block (0xA0..0xB4, 21 B).
 * @note PBAT + = charging / - = discharging; PSYS negative = consumption.
 *       PIN_MW is authoritative for total input; PPV+PDC need not equal PIN.
 */
typedef struct
{
    bool     valid;          /**< true if XSUM (0xB4) matched.              */
    int32_t  ppv_mw;         /**< 0xA0 PV power (mW).                      */
    int32_t  pdc_mw;         /**< 0xA4 DC power (mW).                      */
    int32_t  psys_mw;        /**< 0xA8 System power (mW; - = consumption). */
    int32_t  pbat_mw;        /**< 0xAC Battery power (mW; + chg / - dis).  */
    int32_t  pin_mw;         /**< 0xB0 Total input power (mW).             */
} power_board_power_t;

/**
 * @brief Decoded last-gasp block (0x80..0x9A, 27 B) — power-fail snapshot.
 */
typedef struct
{
    bool     valid;          /**< true if marker 0xB5 and XSUM (0x9A) ok.   */
    uint8_t  reason;         /**< [1] power-fail reason code.              */
    uint16_t soh_x10;        /**< [2-3] State-of-health x10.               */
    uint16_t efc;            /**< [4-5] equivalent full cycles.            */
    uint32_t equiv_hours;    /**< [6-9] 25C-equivalent ageing hours.       */
    uint32_t gross_mah;      /**< [10-13] gross throughput mAh.            */
    uint32_t total_mwh;      /**< [14-17] running total energy mWh.        */
    uint32_t total_mah;      /**< [18-21] running total charge mAh.        */
    uint16_t vbat_mv;        /**< [22-23] battery voltage mV.              */
    uint8_t  cap_ah;         /**< [24] capacity Ah.                        */
    uint8_t  soc_pct;        /**< [25] state-of-charge %.                  */
} power_board_lastgasp_t;

/**
 * @brief Restore block payload published at boot for the master (0x60..0x71).
 * @note 0x61-0x62 (16K RST_SOH_X10) is left 0x0000; the 32K master reads
 *       SoC at 0x63 and the energy counters.
 */
typedef struct
{
    int16_t  soc_x10;        /**< 0x63 SoC x10, signed contract; published
                                  clamped to 0..1000.                      */
    uint32_t equiv_hours;    /**< 0x65 25C-equivalent ageing hours.        */
    uint32_t gross_mah;      /**< 0x69 gross throughput mAh.               */
    int32_t  total_mwh;      /**< 0x6D running total energy mWh.           */
} power_board_restore_t;

/**
 * @brief STATBLK payload published for the master (0x72..0x78).
 */
typedef struct
{
    uint8_t  rec_ack;        /**< 0x72 last persisted SEQ echo.            */
    uint8_t  cfg_gen;        /**< 0x73 config pulse (change -> master reads). */
    uint8_t  cap_ah;         /**< 0x74 battery capacity 7..50 Ah.          */
    uint16_t tcal_q15;       /**< 0x75-76 time calibration Q15 (0=unset).  */
    uint8_t  crate_pct;      /**< 0x77 charge C-rate % 5..20.              */
} power_board_config_t;

/* ======================================================================
 *  Public API
 * ====================================================================== */

/**
 * @brief Initialise the PowerBoard module and start its Contiki process.
 *
 * Publishes an empty RESTORE block (no stored state yet) and the default
 * STATBLK, brings up the I2C slave, and registers the @c pwrboard command.
 * Must be called after the Contiki process system is running.
 */
void power_board_init(void);

/**
 * @brief Read and decode the latest telemetry pushed by the PowerBoard.
 *
 * Takes a tear-free snapshot of the telemetry region, verifies its integrity
 * byte (0x5F) and decodes all fields (MSB-first).
 *
 * @param[out] p_out  Destination telemetry structure.
 * @return true if @p p_out is valid and the integrity byte matched; false if
 *         @p p_out is NULL or the integrity check failed (fields still decoded).
 */
bool power_board_get_telemetry(power_board_telemetry_t *p_out);

/**
 * @brief Read and decode the latest power block (0xA0..0xB4).
 * @return true if @p p_out is valid and the integrity byte matched.
 */
bool power_board_get_power(power_board_power_t *p_out);

/**
 * @brief Read and decode the latest last-gasp block (0x80..0x9A).
 * @return true if @p p_out is valid (marker + integrity byte matched).
 */
bool power_board_get_lastgasp(power_board_lastgasp_t *p_out);

/**
 * @brief Print the latest decoded telemetry via the console logger.
 */
void power_board_log_telemetry(void);

/**
 * @brief Print the latest decoded power block via the console logger.
 */
void power_board_log_power(void);

/**
 * @brief Print the latest decoded last-gasp block via the console logger.
 */
void power_board_log_lastgasp(void);

/**
 * @brief Hex-dump the whole slave register file via the console logger.
 */
void power_board_log_raw(void);

/**
 * @brief Print the I2C slave diagnostic counters via the console logger.
 */
void power_board_log_stats(void);

/**
 * @brief Publish the restore block (0x60..0x71) for the master.
 *
 * The master reads this block once at boot to seed SoC and the energy
 * counters. It must be valid before the slave starts listening. Valid data
 * (marker 0xA5) is produced from @p p_in; call @ref power_board_set_restore_empty
 * when no stored state exists so the master falls back to clean defaults.
 *
 * @param[in] p_in  Restore payload (ignored if NULL).
 */
void power_board_set_restore(const power_board_restore_t *p_in);

/**
 * @brief Publish an empty restore block (marker 0x00).
 *
 * Signals "no stored state" so the PowerBoard starts from clean-battery
 * defaults (SoC 100 %). Correct behaviour when NVM is not wired.
 */
void power_board_set_restore_empty(void);

/**
 * @brief Apply a new battery config and announce it to the master.
 *
 * Updates the STATBLK shadow with @p cap_ah / @p crate_pct / @p tcal_q15
 * (clamped to valid ranges), bumps CFG_GEN so the master re-reads the full
 * config on its next ~8 s poll, and atomically publishes the block.
 *
 * @param[in] cap_ah     Capacity 7..50 Ah (clamped).
 * @param[in] crate_pct  C-rate % 5..20 (clamped); 0/0xFF = unset -> default.
 * @param[in] tcal_q15   Time calibration Q15 (0x0000 = unset -> nominal).
 */
void power_board_set_config(uint8_t cap_ah, uint8_t crate_pct, uint16_t tcal_q15);

/**
 * @brief Read the current STATBLK shadow.
 * @param[out] p_out  Destination (ignored if NULL).
 */
void power_board_get_config(power_board_config_t *p_out);

/**
 * @brief Persist stub — called when the master requests a record (REC_FLAG).
 *
 * Wiring point for NVM/Flash/FRAM. Today it only logs the request to the
 * console; a future task performs the actual persistent write of the SoC /
 * energy counters carried in @p p_t.
 *
 * @param[in] p_t         Telemetry snapshot to persist.
 * @param[in] seq         Sample sequence the master expects echoed.
 * @param[in] alarm       true if a new real alarm (REC_FLAG bit0).
 * @param[in] checkpoint  true if a periodic checkpoint (REC_FLAG bit1).
 */
void power_board_persist_request(const power_board_telemetry_t *p_t,
                                 uint8_t seq, bool alarm, bool checkpoint);

/**
 * @brief Persist stub — called when a last-gasp block arrives (power fail).
 *
 * Wiring point for NVM/Flash/FRAM. Today it only logs the block to the
 * console; a future task persists @p p_g immediately.
 */
void power_board_persist_lastgasp(const power_board_lastgasp_t *p_g);

/**
 * @brief Start periodic telemetry logging from the Contiki process.
 *
 * @param[in] period_ms  Logging interval in milliseconds (clamped to at
 *                       least @ref POWER_BOARD_LOG_MIN_PERIOD_MS).
 */
void power_board_periodic_log_start(uint32_t period_ms);

/**
 * @brief Stop periodic telemetry logging.
 */
void power_board_periodic_log_stop(void);

/**
 * @brief Query whether periodic telemetry logging is active.
 *
 * @return true if the periodic logger is running.
 */
bool power_board_periodic_log_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_BOARD_POWER_BOARD_H_ */
