/**
 * @file  power_board.h
 * @brief PowerBoard I2C-slave telemetry access API (upper-board side).
 *
 * The upper board is the I2C SLAVE (7-bit 0x48). The PowerBoard is the
 * I2C MASTER and pushes a 96-byte telemetry block (register 0x00..0x5F)
 * roughly once per second. This module exposes a decoded, tear-free view
 * of that telemetry plus optional periodic logging.
 *
 * Reference: doc/I2C_SLAVE_ENTEGRASYON_16K.md (BLTX-PWRB-I2C-SLV-16K R0.00).
 * All multi-byte fields are MSB-first (big-endian) per contract rule K3.
 */

#ifndef POWER_BOARD_POWER_BOARD_H_
#define POWER_BOARD_POWER_BOARD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** Size of the telemetry region pushed by the master (0x00..0x5F). */
#define POWER_BOARD_TLM_SIZE           ((uint8_t)96U)

/** Expected protocol versions (both accepted, per doc section 11). */
#define POWER_BOARD_PROT_VER_A         ((uint8_t)0x02U)
#define POWER_BOARD_PROT_VER_B         ((uint8_t)0x03U)

/** Sentinel: BATT_TEMP (0x24) invalid / no NTC. */
#define POWER_BOARD_BATT_TEMP_INVALID  ((int16_t)-9990)

/** Sentinel: BOARD_TEMP (0x22) sensor error. */
#define POWER_BOARD_BOARD_TEMP_ERROR   ((int8_t)-128)

/** Minimum periodic-log interval accepted by the monitor. */
#define POWER_BOARD_LOG_MIN_PERIOD_MS  ((uint32_t)200U)

/** Default periodic-log interval when none is supplied. */
#define POWER_BOARD_LOG_DEF_PERIOD_MS  ((uint32_t)1000U)

/** Restore block base address / marker / length (doc section 7). */
#define POWER_BOARD_RESTORE_BASE       ((uint8_t)0x60U)
#define POWER_BOARD_RESTORE_MARKER     ((uint8_t)0xA5U)
#define POWER_BOARD_RESTORE_LEN        ((uint8_t)18U)   /* 0x60..0x71 */

/** Default SoH x10 used until a stored value is wired in (100.0 %). */
#define POWER_BOARD_SOH_DEFAULT_X10    ((uint16_t)1000U)

/**
 * @brief Decoded PowerBoard telemetry snapshot.
 *
 * Fields mirror the register map in doc section 4. Values scaled by ten
 * keep the @c _x10 suffix (e.g. @c soh_x10 = 1000 means 100.0 %).
 */
typedef struct
{
    bool     valid;          /**< true if BLK_XSUM (0x1F) matched.        */
    uint8_t  prot_ver;       /**< 0x1E PROT_VER.                          */
    uint8_t  seq;            /**< 0x1D sample counter (wraps).            */
    uint8_t  rec_flag;       /**< 0x1C REC_FLAG bitfield.                 */
    uint8_t  blk_xsum;       /**< 0x1F received checksum byte.            */

    uint8_t  sys_state;      /**< 0x00 SYS_STATE.                         */
    uint8_t  sys_fault;      /**< 0x01 SYS_FAULT (BQ FAULT0 mirror).      */
    uint8_t  sys_flags;      /**< 0x02 SYS_FLAGS (live alarms).           */

    uint16_t soh_x10;        /**< 0x04 State-of-health x10 (%).           */
    uint16_t soc_x10;        /**< 0x1A State-of-charge x10 (%).           */

    uint16_t vbat_mv;        /**< 0x10 Battery voltage (mV).              */
    uint16_t vpv_mv;         /**< 0x12 PV voltage (mV).                   */
    uint16_t vdc_mv;         /**< 0x14 DC voltage (mV).                   */

    uint16_t ichg_ma;        /**< 0x20 Charge current (mA).               */
    int16_t  ibat_ma;        /**< 0x56 Battery current (+chg/-dis, mA).   */
    uint16_t ibus_ma;        /**< 0x58 Bus current (mA).                  */

    int8_t   board_temp_c;   /**< 0x22 Board NTC (degC, -128 = error).    */
    int16_t  batt_temp_x10;  /**< 0x24 Battery NTC x10 (degC, -9990 n/a). */
    uint8_t  batt_ts;        /**< 0x23 JEITA zone 0..4.                   */

    uint8_t  chg_stat;       /**< 0x28 Charge phase 0..7.                 */
    uint8_t  chg_phase;      /**< 0x32 Charge phase mirror.               */
    uint8_t  batt_cap_ah;    /**< 0x2C Active capacity echo (Ah).         */

    uint8_t  bq_fault0;      /**< 0x26 BQ REG20 raw.                      */
    uint8_t  bq_fault1;      /**< 0x27 BQ REG21 raw.                      */
    uint8_t  alarm_live;     /**< 0x30 Live alarm bits.                   */
    uint8_t  alarm_latch;    /**< 0x31 Latched alarm bits.                */
    uint8_t  pwr_io;         /**< 0x44 GPIO mirror (see doc 5.2).         */

    uint16_t bq_vsys_mv;     /**< 0x50 BQ VSYS (mV).                      */
    uint16_t bq_vbus_mv;     /**< 0x52 BQ VBUS (mV).                      */
    uint16_t bq_vac1_mv;     /**< 0x5A BQ VAC1 = DC input (mV).           */
    uint16_t bq_vac2_mv;     /**< 0x5C BQ VAC2 = PV input (mV).           */
    int8_t   bq_tdie_c;      /**< 0x5E BQ die temperature (degC).         */
} power_board_telemetry_t;

/**
 * @brief Initialise the PowerBoard module and start its Contiki process.
 *
 * Brings up the I2C slave and registers the @c pwrboard shell command.
 * Must be called after the Contiki process system is running.
 */
void power_board_init(void);

/**
 * @brief Read and decode the latest telemetry pushed by the PowerBoard.
 *
 * Takes a tear-free snapshot of the slave register file, verifies the
 * telemetry checksum (doc 5.1) and decodes all fields (MSB-first).
 *
 * @param[out] p_out  Destination telemetry structure.
 *
 * @return true if @p p_out is valid and the checksum matched; false if
 *         @p p_out is NULL or the checksum failed (fields still decoded).
 */
bool power_board_get_telemetry(power_board_telemetry_t *p_out);

/**
 * @brief Print the latest decoded telemetry via the console logger.
 */
void power_board_log_telemetry(void);

/**
 * @brief Hex-dump the whole slave register file via the console logger.
 *
 * Prints all I2C_SLAVE_REG_COUNT bytes (telemetry region plus the
 * read-back blocks) as a 16-column table for raw inspection.
 */
void power_board_log_raw(void);

/**
 * @brief Print the I2C slave diagnostic counters via the console logger.
 */
void power_board_log_stats(void);

/**
 * @brief Build and publish the restore block (0x60..0x71) for the master.
 *
 * The master reads this block once at boot to seed SoH (doc section 7).
 * It must be valid before the slave starts listening, so it is populated
 * during @ref power_board_init. Call again whenever the stored SoH
 * changes. The block layout is marker 0xA5 + SoH x10 (MSB-first) + zero
 * padding + a 17-byte XOR checksum (marker included).
 *
 * @param[in] soh_x10  State-of-health scaled by ten (e.g. 1000 = 100.0 %).
 */
void power_board_set_restore(uint16_t soh_x10);

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
