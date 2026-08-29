/**
 * @file  power_board.c
 * @brief PowerBoard I2C-slave protocol: telemetry/power/lastgasp decode,
 *        restore/statblk publish, REC handshake and shell.
 *
 * The upper board acts as the I2C slave (0x48); the PowerBoard master pushes
 * TELEMETRY (0x00..0x5F), POWER (0xA0..0xB4) and LASTGASP (0x80..0x9A) into
 * the slave register file, and reads RESTORE (0x60..0x71) and STATBLK
 * (0x72..0x78) the upper board publishes. This module snapshots each PUSH
 * block, verifies its integrity byte, decodes the fields (MSB-first) and
 * drives the REC/config handshake on the PULL blocks.
 *
 * Reference: doc/PowerBoard_I2C_Protocol.md (Rev0.7, PROT_VER 0x06).
 * Block-layout authority: doc/pwr/pwr_i2c_packets.h.
 */

#include <string.h>
#include "power_board.h"
#include "i2c_slave.h"
#include "bsp.h"
#include "contiki.h"
#include "shell.h"
#include "utils.h"
#include "console_logger.h"
#include "elog.h"

/* ======================================================================
 *  Telemetry register offsets (doc section 3b)
 * ====================================================================== */

#define PB_REG_SYS_STATE     ((uint8_t)0x00U)
#define PB_REG_SYS_FAULT     ((uint8_t)0x01U)
#define PB_REG_SYS_FLAGS     ((uint8_t)0x02U)
#define PB_REG_CHG_STAT_RAW  ((uint8_t)0x03U)
#define PB_REG_SOH_X10       ((uint8_t)0x04U)
#define PB_REG_EFC           ((uint8_t)0x06U)
#define PB_REG_EQUIV_HOURS   ((uint8_t)0x08U)
#define PB_REG_GROSS_MAH     ((uint8_t)0x0CU)
#define PB_REG_VBAT_MV       ((uint8_t)0x10U)
#define PB_REG_VPV_MV        ((uint8_t)0x12U)
#define PB_REG_VDC_MV        ((uint8_t)0x14U)
#define PB_REG_REM_EFC       ((uint8_t)0x16U)
#define PB_REG_REM_YEARS_X10 ((uint8_t)0x18U)
#define PB_REG_SOC_X10       ((uint8_t)0x1AU)
#define PB_REG_REC_FLAG      ((uint8_t)0x1CU)
#define PB_REG_SEQ           ((uint8_t)0x1DU)
#define PB_REG_PROT_VER      ((uint8_t)0x1EU)
/* 0x1F reserved (0x00), included in the integrity XOR. */
#define PB_REG_ICHG_MA       ((uint8_t)0x20U)
#define PB_REG_BOARD_TEMP    ((uint8_t)0x22U)
#define PB_REG_BATT_TS       ((uint8_t)0x23U)
#define PB_REG_BATT_TEMP     ((uint8_t)0x24U)
#define PB_REG_BQ_FAULT0     ((uint8_t)0x26U)
#define PB_REG_BQ_FAULT1     ((uint8_t)0x27U)
#define PB_REG_CHG_STAT      ((uint8_t)0x28U)
#define PB_REG_ICO_STAT      ((uint8_t)0x29U)
#define PB_REG_HEATER_STATE  ((uint8_t)0x2AU)
#define PB_REG_BATT_STATE    ((uint8_t)0x2BU)
#define PB_REG_BATT_CAP_AH   ((uint8_t)0x2CU)
#define PB_REG_BATT_CRATE    ((uint8_t)0x2DU)
#define PB_REG_ICHG_TARGET   ((uint8_t)0x2EU)
#define PB_REG_ALARM_LIVE    ((uint8_t)0x30U)
#define PB_REG_ALARM_LATCH   ((uint8_t)0x31U)
#define PB_REG_CHG_PHASE     ((uint8_t)0x32U)
#define PB_REG_DELTA_UWH     ((uint8_t)0x33U)
#define PB_REG_TOTAL_MWH     ((uint8_t)0x37U)
#define PB_REG_TOTAL_MAH     ((uint8_t)0x3BU)
#define PB_REG_BMS_PRESENT   ((uint8_t)0x3FU)
#define PB_REG_BQ_REG1B      ((uint8_t)0x40U)
#define PB_REG_BQ_REG1D      ((uint8_t)0x41U)
#define PB_REG_BQ_REG1E      ((uint8_t)0x42U)
#define PB_REG_BQ_REG1F      ((uint8_t)0x43U)
#define PB_REG_PWR_IO        ((uint8_t)0x44U)
#define PB_REG_HIZ_TRIG      ((uint8_t)0x45U)
#define PB_REG_MPPT_TRIG     ((uint8_t)0x46U)
#define PB_REG_IINDPM_TRIG   ((uint8_t)0x47U)
#define PB_REG_ACDRV_TRIG    ((uint8_t)0x48U)
#define PB_REG_PWR_SRC       ((uint8_t)0x49U)
#define PB_REG_BQ_VSYS_MV    ((uint8_t)0x50U)
#define PB_REG_BQ_VBUS_MV    ((uint8_t)0x52U)
#define PB_REG_STM_VBAT_MV   ((uint8_t)0x54U)
#define PB_REG_IBAT_MA       ((uint8_t)0x56U)
#define PB_REG_IBUS_MA       ((uint8_t)0x58U)
#define PB_REG_BQ_VAC1_MV    ((uint8_t)0x5AU)
#define PB_REG_BQ_VAC2_MV    ((uint8_t)0x5CU)
#define PB_REG_BQ_TDIE_C     ((uint8_t)0x5EU)
#define PB_REG_TLM_XSUM      ((uint8_t)0x5FU)   /* integrity byte */

/* Power block in-block offsets (base 0xA0). */
#define PB_PWR_PPV           ((uint8_t)0x00U)
#define PB_PWR_PDC           ((uint8_t)0x04U)
#define PB_PWR_PSYS          ((uint8_t)0x08U)
#define PB_PWR_PBAT          ((uint8_t)0x0CU)
#define PB_PWR_PIN           ((uint8_t)0x10U)

/* ======================================================================
 *  Module state
 * ====================================================================== */

/** Config source of truth; published as STATBLK bytes via s_statblk. */
static power_board_config_t s_cfg;
/** STATBLK shadow published for the master (read atomically by the slave). */
static uint8_t              s_statblk[POWER_BOARD_STATBLK_LEN];

/** Periodic logging control (written by any context, read by process). */
static volatile bool     s_log_active;
static volatile uint32_t s_log_period_ticks;
static struct etimer     s_log_timer;
static struct etimer     s_drain_timer;

PROCESS(power_board_process, "pwrboard");

/* ======================================================================
 *  Internal helpers (MSB-first / salted XSUM)
 * ====================================================================== */

/**
 * @brief Read a big-endian (MSB-first) unsigned 16-bit field.
 */
static uint16_t pb_rd_u16(const uint8_t *p_reg, uint8_t off)
{
    return (uint16_t)(((uint16_t)p_reg[off] << 8) | (uint16_t)p_reg[off + 1U]);
}

/**
 * @brief Read a big-endian (MSB-first) signed 16-bit field.
 */
static int16_t pb_rd_i16(const uint8_t *p_reg, uint8_t off)
{
    return (int16_t)pb_rd_u16(p_reg, off);
}

/**
 * @brief Read a big-endian (MSB-first) unsigned 32-bit field.
 */
static uint32_t pb_rd_u32(const uint8_t *p_reg, uint8_t off)
{
    return ((uint32_t)p_reg[off]      << 24)
         | ((uint32_t)p_reg[off + 1U] << 16)
         | ((uint32_t)p_reg[off + 2U] <<  8)
         |  (uint32_t)p_reg[off + 3U];
}

/**
 * @brief Write a big-endian (MSB-first) unsigned 16-bit value into a buffer.
 */
static void pb_wr_u16(uint8_t *p_buf, uint8_t off, uint16_t v)
{
    p_buf[off]      = (uint8_t)(v >> 8);
    p_buf[off + 1U] = (uint8_t)v;
}

/**
 * @brief Write a big-endian (MSB-first) unsigned 32-bit value into a buffer.
 */
static void pb_wr_u32(uint8_t *p_buf, uint8_t off, uint32_t v)
{
    p_buf[off]      = (uint8_t)(v >> 24);
    p_buf[off + 1U] = (uint8_t)(v >> 16);
    p_buf[off + 2U] = (uint8_t)(v >> 8);
    p_buf[off + 3U] = (uint8_t)v;
}

/**
 * @brief Salted XOR integrity over @c len bytes: XOR(b[0..len-1]) ^ 0x5A.
 *
 * Every block's LAST byte carries this value over all preceding bytes
 * (doc section 2). The caller compares it to the block's last byte.
 */
static uint8_t pb_xsum(const uint8_t *p_buf, uint8_t len)
{
    uint8_t x = 0U;
    for (uint8_t i = 0U; i < len; i++)
    {
        x = (uint8_t)(x ^ p_buf[i]);
    }
    return (uint8_t)(x ^ POWER_BOARD_XSUM_SALT);
}

/* ======================================================================
 *  PUSH blocks: decode
 * ====================================================================== */

bool power_board_get_telemetry(power_board_telemetry_t *p_out)
{
    if (p_out == NULL)
    {
        return false;
    }

    uint8_t reg[POWER_BOARD_TLM_SIZE];
    i2c_slave_snapshot(reg, POWER_BOARD_TLM_BASE, POWER_BOARD_TLM_SIZE);

    /* Integrity: XOR(0x00..0x5E) ^ 0x5A == byte 0x5F. 0x1F (reserved, 0x00)
     * lies inside the covered range and is part of the XOR. */
    p_out->valid = (pb_xsum(reg, PB_REG_TLM_XSUM) == reg[PB_REG_TLM_XSUM]);

    p_out->prot_ver    = reg[PB_REG_PROT_VER];
    p_out->seq         = reg[PB_REG_SEQ];
    p_out->rec_flag    = reg[PB_REG_REC_FLAG];
    p_out->blk_xsum    = reg[PB_REG_TLM_XSUM];

    p_out->sys_state   = reg[PB_REG_SYS_STATE];
    p_out->sys_fault   = reg[PB_REG_SYS_FAULT];
    p_out->sys_flags   = reg[PB_REG_SYS_FLAGS];
    p_out->chg_stat_raw = reg[PB_REG_CHG_STAT_RAW];

    p_out->soh_x10     = pb_rd_u16(reg, PB_REG_SOH_X10);
    p_out->efc         = pb_rd_u16(reg, PB_REG_EFC);
    p_out->equiv_hours = pb_rd_u32(reg, PB_REG_EQUIV_HOURS);
    p_out->gross_mah   = pb_rd_u32(reg, PB_REG_GROSS_MAH);

    p_out->vbat_mv     = pb_rd_u16(reg, PB_REG_VBAT_MV);
    p_out->vpv_mv      = pb_rd_u16(reg, PB_REG_VPV_MV);
    p_out->vdc_mv      = pb_rd_u16(reg, PB_REG_VDC_MV);
    p_out->rem_efc     = pb_rd_u16(reg, PB_REG_REM_EFC);
    p_out->rem_years_x10 = pb_rd_u16(reg, PB_REG_REM_YEARS_X10);
    p_out->soc_x10     = pb_rd_u16(reg, PB_REG_SOC_X10);

    p_out->ichg_ma     = pb_rd_u16(reg, PB_REG_ICHG_MA);
    p_out->ibat_ma     = pb_rd_i16(reg, PB_REG_IBAT_MA);
    p_out->ibus_ma     = pb_rd_u16(reg, PB_REG_IBUS_MA);

    p_out->board_temp_c  = (int8_t)reg[PB_REG_BOARD_TEMP];
    p_out->batt_temp_x10 = pb_rd_i16(reg, PB_REG_BATT_TEMP);
    p_out->batt_ts       = reg[PB_REG_BATT_TS];

    p_out->chg_stat      = reg[PB_REG_CHG_STAT];
    p_out->chg_phase     = reg[PB_REG_CHG_PHASE];
    p_out->ico_stat      = reg[PB_REG_ICO_STAT];
    p_out->heater_state  = reg[PB_REG_HEATER_STATE];
    p_out->batt_state    = reg[PB_REG_BATT_STATE];
    p_out->batt_cap_ah   = reg[PB_REG_BATT_CAP_AH];
    p_out->batt_crate    = reg[PB_REG_BATT_CRATE];
    p_out->ichg_target_ma = pb_rd_u16(reg, PB_REG_ICHG_TARGET);

    p_out->bq_fault0   = reg[PB_REG_BQ_FAULT0];
    p_out->bq_fault1   = reg[PB_REG_BQ_FAULT1];
    p_out->alarm_live  = reg[PB_REG_ALARM_LIVE];
    p_out->alarm_latch = reg[PB_REG_ALARM_LATCH];

    p_out->delta_uwh   = (int32_t)pb_rd_u32(reg, PB_REG_DELTA_UWH);
    p_out->total_mwh   = (int32_t)pb_rd_u32(reg, PB_REG_TOTAL_MWH);
    p_out->total_mah   = (int32_t)pb_rd_u32(reg, PB_REG_TOTAL_MAH);
    p_out->bms_present = reg[PB_REG_BMS_PRESENT];

    p_out->bq_reg1b    = reg[PB_REG_BQ_REG1B];
    p_out->bq_reg1d    = reg[PB_REG_BQ_REG1D];
    p_out->bq_reg1e    = reg[PB_REG_BQ_REG1E];
    p_out->bq_reg1f    = reg[PB_REG_BQ_REG1F];

    p_out->pwr_io      = reg[PB_REG_PWR_IO];
    p_out->hiz_trig    = reg[PB_REG_HIZ_TRIG];
    p_out->mppt_trig   = reg[PB_REG_MPPT_TRIG];
    p_out->iindpm_trig = reg[PB_REG_IINDPM_TRIG];
    p_out->acdrv_trig  = reg[PB_REG_ACDRV_TRIG];
    p_out->pwr_src     = reg[PB_REG_PWR_SRC];

    p_out->bq_vsys_mv  = pb_rd_u16(reg, PB_REG_BQ_VSYS_MV);
    p_out->bq_vbus_mv  = pb_rd_u16(reg, PB_REG_BQ_VBUS_MV);
    p_out->stm_vbat_mv = pb_rd_u16(reg, PB_REG_STM_VBAT_MV);
    p_out->bq_vac1_mv  = pb_rd_u16(reg, PB_REG_BQ_VAC1_MV);
    p_out->bq_vac2_mv  = pb_rd_u16(reg, PB_REG_BQ_VAC2_MV);
    p_out->bq_tdie_c   = (int8_t)reg[PB_REG_BQ_TDIE_C];

    return p_out->valid;
}

bool power_board_get_power(power_board_power_t *p_out)
{
    if (p_out == NULL)
    {
        return false;
    }

    uint8_t blk[POWER_BOARD_PWR_LEN];
    i2c_slave_snapshot(blk, POWER_BOARD_PWR_BASE, POWER_BOARD_PWR_LEN);

    p_out->valid = (pb_xsum(blk, (uint8_t)(POWER_BOARD_PWR_LEN - 1U))
                    == blk[POWER_BOARD_PWR_LEN - 1U]);

    p_out->ppv_mw  = (int32_t)pb_rd_u32(blk, PB_PWR_PPV);
    p_out->pdc_mw  = (int32_t)pb_rd_u32(blk, PB_PWR_PDC);
    p_out->psys_mw = (int32_t)pb_rd_u32(blk, PB_PWR_PSYS);
    p_out->pbat_mw = (int32_t)pb_rd_u32(blk, PB_PWR_PBAT);
    p_out->pin_mw  = (int32_t)pb_rd_u32(blk, PB_PWR_PIN);

    return p_out->valid;
}

bool power_board_get_lastgasp(power_board_lastgasp_t *p_out)
{
    if (p_out == NULL)
    {
        return false;
    }

    uint8_t blk[POWER_BOARD_LG_LEN];
    i2c_slave_snapshot(blk, POWER_BOARD_LG_BASE, POWER_BOARD_LG_LEN);

    bool marker_ok = (blk[0] == POWER_BOARD_LG_MARKER);
    bool xsum_ok   = (pb_xsum(blk, (uint8_t)(POWER_BOARD_LG_LEN - 1U))
                      == blk[POWER_BOARD_LG_LEN - 1U]);
    p_out->valid = (marker_ok && xsum_ok);

    p_out->reason      = blk[1];
    p_out->soh_x10     = pb_rd_u16(blk, 2U);
    p_out->efc         = pb_rd_u16(blk, 4U);
    p_out->equiv_hours = pb_rd_u32(blk, 6U);
    p_out->gross_mah   = pb_rd_u32(blk, 10U);
    p_out->total_mwh   = pb_rd_u32(blk, 14U);
    p_out->total_mah   = pb_rd_u32(blk, 18U);
    p_out->vbat_mv     = pb_rd_u16(blk, 22U);
    p_out->cap_ah      = blk[24];
    p_out->soc_pct     = blk[25];

    return p_out->valid;
}

/* ======================================================================
 *  Logging
 * ====================================================================== */

void power_board_log_telemetry(void)
{
    power_board_telemetry_t t;
    (void)power_board_get_telemetry(&t);

    CSLOG_NODT("[PWRB] === PowerBoard Telemetry ===\r\n");
    CSLOG_NODT("[PWRB] XSUM:%s  PROT_VER:0x%02X  SEQ:%u  REC:0x%02X\r\n",
               t.valid ? "OK" : "FAIL",
               (unsigned)t.prot_ver, (unsigned)t.seq, (unsigned)t.rec_flag);
    CSLOG_NODT("[PWRB] SYS_STATE:0x%02X  SYS_FAULT:0x%02X  SYS_FLAGS:0x%02X  CHG_RAW:0x%02X\r\n",
               (unsigned)t.sys_state, (unsigned)t.sys_fault,
               (unsigned)t.sys_flags, (unsigned)t.chg_stat_raw);
    CSLOG_NODT("[PWRB] SoH:%u.%u%%  SoC:%u.%u%%  EFC:%u  yrs_left:%u.%u\r\n",
               (unsigned)(t.soh_x10 / 10U), (unsigned)(t.soh_x10 % 10U),
               (unsigned)(t.soc_x10 / 10U), (unsigned)(t.soc_x10 % 10U),
               (unsigned)t.efc,
               (unsigned)(t.rem_years_x10 / 10U), (unsigned)(t.rem_years_x10 % 10U));
    CSLOG_NODT("[PWRB] VBAT:%u  VPV:%u  VDC:%u mV  (STM_VBAT:%u)\r\n",
               (unsigned)t.vbat_mv, (unsigned)t.vpv_mv, (unsigned)t.vdc_mv,
               (unsigned)t.stm_vbat_mv);
    CSLOG_NODT("[PWRB] ICHG:%u  IBAT:%d  IBUS:%u mA  (target:%u)\r\n",
               (unsigned)t.ichg_ma, (int)t.ibat_ma, (unsigned)t.ibus_ma,
               (unsigned)t.ichg_target_ma);

    if (t.board_temp_c == POWER_BOARD_BOARD_TEMP_ERROR)
    {
        CSLOG_NODT("[PWRB] BoardT:err  ");
    }
    else
    {
        CSLOG_NODT("[PWRB] BoardT:%d C  ", (int)t.board_temp_c);
    }

    if (t.batt_temp_x10 == POWER_BOARD_BATT_TEMP_INVALID)
    {
        CSLOG_NODT("BattT:n/a  TS:%u\r\n", (unsigned)t.batt_ts);
    }
    else
    {
        int16_t bt  = t.batt_temp_x10;
        bool    neg = (bt < 0);
        int     mag = neg ? -(int)bt : (int)bt;
        CSLOG_NODT("BattT:%s%d.%d C  TS:%u\r\n",
                   neg ? "-" : "", mag / 10, mag % 10,
                   (unsigned)t.batt_ts);
    }

    CSLOG_NODT("[PWRB] CHG:%u  PHASE:%u  ICO:%u  HEATER:%u  BATT:%u  CAP:%uAh  CRATE:%u%%\r\n",
               (unsigned)t.chg_stat, (unsigned)t.chg_phase, (unsigned)t.ico_stat,
               (unsigned)t.heater_state, (unsigned)t.batt_state,
               (unsigned)t.batt_cap_ah, (unsigned)t.batt_crate);
    CSLOG_NODT("[PWRB] energy: delta=%ld uWh  total=%ld mWh/%ld mAh  equiv=%luh  gross=%lumAh\r\n",
               (long)t.delta_uwh, (long)t.total_mwh, (long)t.total_mah,
               (unsigned long)t.equiv_hours, (unsigned long)t.gross_mah);
    CSLOG_NODT("[PWRB] ALARM live:0x%02X latch:0x%02X  PWR_IO:0x%02X  PWR_SRC:0x%02X\r\n",
               (unsigned)t.alarm_live, (unsigned)t.alarm_latch,
               (unsigned)t.pwr_io, (unsigned)t.pwr_src);
    CSLOG_NODT("[PWRB] BQ FAULT0:0x%02X FAULT1:0x%02X  R1B:0x%02X R1D:0x%02X R1E:0x%02X R1F:0x%02X\r\n",
               (unsigned)t.bq_fault0, (unsigned)t.bq_fault1,
               (unsigned)t.bq_reg1b, (unsigned)t.bq_reg1d,
               (unsigned)t.bq_reg1e, (unsigned)t.bq_reg1f);
    CSLOG_NODT("[PWRB] BQ VSYS:%u VBUS:%u VAC1/DC:%u VAC2/PV:%u mV  TDIE:%d C\r\n",
               (unsigned)t.bq_vsys_mv, (unsigned)t.bq_vbus_mv,
               (unsigned)t.bq_vac1_mv, (unsigned)t.bq_vac2_mv,
               (int)t.bq_tdie_c);
    CSLOG_NODT("[PWRB] watchdogs HIZ:%u MPPT:%u IINDPM:%u ACDRV:%u\r\n",
               (unsigned)t.hiz_trig, (unsigned)t.mppt_trig,
               (unsigned)t.iindpm_trig, (unsigned)t.acdrv_trig);
}

void power_board_log_power(void)
{
    power_board_power_t p;
    (void)power_board_get_power(&p);

    CSLOG_NODT("[PWRB] === Power Block (%s) ===\r\n", p.valid ? "OK" : "FAIL");
    CSLOG_NODT("[PWRB] PPV:%ld  PDC:%ld  PSYS:%ld  PBAT:%ld  PIN:%ld mW\r\n",
               (long)p.ppv_mw, (long)p.pdc_mw, (long)p.psys_mw,
               (long)p.pbat_mw, (long)p.pin_mw);
}

void power_board_log_lastgasp(void)
{
    power_board_lastgasp_t g;
    (void)power_board_get_lastgasp(&g);

    CSLOG_NODT("[PWRB] === LastGasp (%s) ===\r\n", g.valid ? "OK" : "FAIL");
    CSLOG_NODT("[PWRB] reason:0x%02X  SoC:%u%%  SoH:%u.%u%%  VBAT:%u mV  CAP:%uAh\r\n",
               (unsigned)g.reason, (unsigned)g.soc_pct,
               (unsigned)(g.soh_x10 / 10U), (unsigned)(g.soh_x10 % 10U),
               (unsigned)g.vbat_mv, (unsigned)g.cap_ah);
    CSLOG_NODT("[PWRB] EFC:%u  equiv:%luh  gross:%lumAh  total:%lumWh/%lumAh\r\n",
               (unsigned)g.efc, (unsigned long)g.equiv_hours,
               (unsigned long)g.gross_mah, (unsigned long)g.total_mwh,
               (unsigned long)g.total_mah);
}

void power_board_log_raw(void)
{
    uint8_t reg[I2C_SLAVE_REG_COUNT];
    i2c_slave_snapshot(reg, 0U, (uint8_t)I2C_SLAVE_REG_COUNT);

    CSLOG_NODT("[PWRB] === Register dump (%u bytes) ===\r\n",
               (unsigned)I2C_SLAVE_REG_COUNT);
    CSLOG_NODT("[PWRB]     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\r\n");

    for (uint16_t row = 0U; row < (uint16_t)I2C_SLAVE_REG_COUNT; row += 16U)
    {
        CSLOG_NODT("[PWRB] %02X:", (unsigned)row);
        for (uint8_t col = 0U; col < 16U; col++)
        {
            CSLOG_NODT(" %02X", (unsigned)reg[row + col]);
        }
        CSLOG_NODT("\r\n");
    }
}

void power_board_log_stats(void)
{
    i2c_slave_stats_t s;
    i2c_slave_get_stats(&s);

    CSLOG_NODT("[PWRB] === I2C slave stats ===\r\n");
    CSLOG_NODT("[PWRB] addr wr:%u  rd:%u  rx:%u  tx:%u  stop:%u\r\n",
               (unsigned)s.addr_write, (unsigned)s.addr_read,
               (unsigned)s.rx_bytes, (unsigned)s.tx_bytes,
               (unsigned)s.listen_cplt);
    CSLOG_NODT("[PWRB] err AF:%u  BERR:%u  ARLO:%u  OVR:%u  other:%u\r\n",
               (unsigned)s.err_af, (unsigned)s.err_berr,
               (unsigned)s.err_arlo, (unsigned)s.err_ovr,
               (unsigned)s.err_other);
    CSLOG_NODT("[PWRB] recover:%u  last_error:0x%08X\r\n",
               (unsigned)s.recover, (unsigned)s.last_error);
}

/* ======================================================================
 *  PULL blocks: RESTORE + STATBLK publish
 * ====================================================================== */

void power_board_set_restore(const power_board_restore_t *p_in)
{
    if (p_in == NULL)
    {
        return;
    }

    uint8_t blk[POWER_BOARD_RESTORE_LEN];
    for (uint8_t i = 0U; i < POWER_BOARD_RESTORE_LEN; i++)
    {
        blk[i] = 0U;
    }

    blk[0] = POWER_BOARD_RESTORE_MARKER;                 /* 0x60 = 0xA5 */
    /* 0x61-0x62 (16K RST_SOH_X10) left 0x0000; 32K ignores it. */
    uint16_t soc = (p_in->soc_x10 > 1000U) ? 1000U : p_in->soc_x10;
    pb_wr_u16(blk, 3U,  soc);                            /* 0x63 soc_x10 */
    pb_wr_u32(blk, 5U,  p_in->equiv_hours);             /* 0x65 */
    pb_wr_u32(blk, 9U,  p_in->gross_mah);               /* 0x69 */
    pb_wr_u32(blk, 13U, (uint32_t)p_in->total_mwh);     /* 0x6D */
    blk[17] = pb_xsum(blk, (uint8_t)(POWER_BOARD_RESTORE_LEN - 1U));  /* 0x71 */

    i2c_slave_write_block(POWER_BOARD_RESTORE_BASE, blk, POWER_BOARD_RESTORE_LEN);
}

void power_board_set_restore_empty(void)
{
    uint8_t blk[POWER_BOARD_RESTORE_LEN];
    for (uint8_t i = 0U; i < POWER_BOARD_RESTORE_LEN; i++)
    {
        blk[i] = 0U;
    }

    blk[0] = 0x00U;   /* marker != 0xA5 -> master rejects, uses clean defaults */
    blk[17] = pb_xsum(blk, (uint8_t)(POWER_BOARD_RESTORE_LEN - 1U));

    i2c_slave_write_block(POWER_BOARD_RESTORE_BASE, blk, POWER_BOARD_RESTORE_LEN);
}

/** @brief Rebuild s_statblk from s_cfg and publish it atomically. */
static void pb_statblk_publish(void)
{
    s_statblk[0] = s_cfg.rec_ack;
    s_statblk[1] = s_cfg.cfg_gen;
    s_statblk[2] = s_cfg.cap_ah;
    pb_wr_u16(s_statblk, 3U, s_cfg.tcal_q15);
    s_statblk[5] = s_cfg.crate_pct;
    s_statblk[6] = pb_xsum(s_statblk, (uint8_t)(POWER_BOARD_STATBLK_LEN - 1U));

    i2c_slave_write_block(POWER_BOARD_STATBLK_BASE, s_statblk, POWER_BOARD_STATBLK_LEN);
}

/** @brief Publish the default STATBLK (doc 9b.3 = PowerBoard's own defaults). */
static void pb_statblk_publish_default(void)
{
    s_cfg.rec_ack   = POWER_BOARD_CFG_DEF_REC_ACK;
    s_cfg.cfg_gen   = POWER_BOARD_CFG_DEF_GEN;
    s_cfg.cap_ah    = POWER_BOARD_CFG_DEF_CAP_AH;
    s_cfg.crate_pct = POWER_BOARD_CFG_DEF_CRATE;
    s_cfg.tcal_q15  = POWER_BOARD_CFG_DEF_TCAL_Q15;
    pb_statblk_publish();
}

void power_board_set_config(uint8_t cap_ah, uint8_t crate_pct, uint16_t tcal_q15)
{
    uint8_t cap = cap_ah;
    if ((cap < POWER_BOARD_CFG_CAP_AH_MIN) || (cap > POWER_BOARD_CFG_CAP_AH_MAX))
    {
        cap = POWER_BOARD_CFG_DEF_CAP_AH;
    }

    uint8_t crate = crate_pct;
    if ((crate == 0U) || (crate == 0xFFU))
    {
        crate = POWER_BOARD_CFG_DEF_CRATE;
    }
    else if (crate < POWER_BOARD_CFG_CRATE_MIN)
    {
        crate = POWER_BOARD_CFG_CRATE_MIN;
    }
    else if (crate > POWER_BOARD_CFG_CRATE_MAX)
    {
        crate = POWER_BOARD_CFG_CRATE_MAX;
    }
    else
    {
        /* in range */
    }

    uint16_t tcal = tcal_q15;
    if (tcal == 0xFFFFU)
    {
        tcal = POWER_BOARD_CFG_TCAL_UNSET;
    }

    s_cfg.cap_ah    = cap;
    s_cfg.crate_pct = crate;
    s_cfg.tcal_q15  = tcal;
    s_cfg.cfg_gen++;            /* bump pulse -> master re-reads full config */
    pb_statblk_publish();
}

void power_board_get_config(power_board_config_t *p_out)
{
    if (p_out == NULL)
    {
        return;
    }
    *p_out = s_cfg;
}

/* ======================================================================
 *  REC handshake + persistence stubs
 * ====================================================================== */

void power_board_persist_request(const power_board_telemetry_t *p_t,
                                 uint8_t seq, bool alarm, bool checkpoint)
{
    (void)p_t;
    /* Stub: NVM not wired yet. The call site is preserved so a future task
     * only fills this in. Logs the request for visibility. */
    CSLOG_WARN_NODT("[PWRB] PERSIST req seq=%u %s%s (stub - NVM yok)\r\n",
                    (unsigned)seq, alarm ? "ALARM " : "",
                    checkpoint ? "CKPT" : "");
    /* TODO(NVM): persist p_t->soc_x10 / total_mwh / equiv_hours / gross_mah. */
}

void power_board_persist_lastgasp(const power_board_lastgasp_t *p_g)
{
    if (p_g == NULL)
    {
        return;
    }
    /* Stub: NVM not wired yet. */
    CSLOG_WARN_NODT("[PWRB] LASTGASP persist reason=0x%02X soc=%u%% vbat=%u mV (stub)\r\n",
                    (unsigned)p_g->reason, (unsigned)p_g->soc_pct,
                    (unsigned)p_g->vbat_mv);
    /* TODO(NVM): persist the last-gasp snapshot immediately. */
}

/** @brief Handle a record request: persist stub + echo SEQ into STATBLK[0]. */
static void pb_handle_rec(const power_board_telemetry_t *p_t)
{
    uint8_t seq        = p_t->seq;
    bool    alarm      = ((p_t->rec_flag & 0x01U) != 0U);
    bool    checkpoint = ((p_t->rec_flag & 0x02U) != 0U);

    power_board_persist_request(p_t, seq, alarm, checkpoint);

    /* Echo the SEQ so the master's REC_ACK read (0x72) completes the handshake. */
    s_cfg.rec_ack = seq;
    pb_statblk_publish();
}

/* ======================================================================
 *  Periodic logging
 * ====================================================================== */

void power_board_periodic_log_start(uint32_t period_ms)
{
    uint32_t period = period_ms;
    if (period < POWER_BOARD_LOG_MIN_PERIOD_MS)
    {
        period = POWER_BOARD_LOG_MIN_PERIOD_MS;
    }

    uint32_t ticks = (period * (uint32_t)CLOCK_SECOND) / 1000U;
    if (ticks == 0U)
    {
        ticks = 1U;
    }

    s_log_period_ticks = ticks;
    s_log_active       = true;
    process_poll(&power_board_process);
}

void power_board_periodic_log_stop(void)
{
    s_log_active = false;
    process_poll(&power_board_process);
}

bool power_board_periodic_log_is_active(void)
{
    return s_log_active;
}

/* ======================================================================
 *  Contiki process - drain PUSH blocks + periodic logging
 * ====================================================================== */

/* Report telemetry transitions to elog: latched alarms (set/clear) and
 * battery presence state. Called on every drained telemetry block; the
 * payload packing and level policy live in elog.c. */
static void pb_log_transitions(const power_board_telemetry_t *t)
{
    static bool     have_prev  = false;
    static uint8_t  prev_latch = 0U;
    static uint8_t  prev_batt  = 0U;

    if (!t->valid)
    {
        return;
    }

    if (have_prev)
    {
        if (t->alarm_latch != prev_latch)
        {
            elog_power_alarm_t alarm;
            alarm.latch     = t->alarm_latch;
            alarm.live      = t->alarm_live;
            alarm.sys_fault = t->sys_fault;
            alarm.bq_fault0 = t->bq_fault0;
            alarm.bq_fault1 = t->bq_fault1;
            alarm.rising    = ((t->alarm_latch & (uint8_t)~prev_latch) != 0U);
            elog_log_power_alarm(&alarm);
        }

        if (t->batt_state != prev_batt)
        {
            elog_log_battery_state_change(ELOG_BAT_SRC_POWER_BOARD,
                                          prev_batt, t->batt_state,
                                          (uint8_t)(t->soc_x10 / 10U),
                                          (uint8_t)(t->soh_x10 / 10U));
        }
    }

    prev_latch = t->alarm_latch;
    prev_batt  = t->batt_state;
    have_prev  = true;
}

PROCESS_THREAD(power_board_process, ev, data)
{
    (void)data;

    PROCESS_BEGIN();

    /* Fast drain cadence: PUSH blocks arrive ~1 s (telemetry/power) or as
     * events (lastgasp); 50 ms polling drains them well within the REC
     * retry window (~2.5 s). */
    etimer_set(&s_drain_timer, 50);

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL((ev == PROCESS_EVENT_POLL)
                                 || (ev == PROCESS_EVENT_TIMER));

        /* Drain freshly written PUSH blocks (thread context, not ISR). */
        if (i2c_slave_take_block(I2C_SLAVE_BLK_TELEMETRY))
        {
            power_board_telemetry_t t;
            if (power_board_get_telemetry(&t))
            {
                pb_log_transitions(&t);
                if (t.rec_flag != 0U)
                {
                    pb_handle_rec(&t);
                }
            }
        }
        if (i2c_slave_take_block(I2C_SLAVE_BLK_LASTGASP))
        {
            power_board_lastgasp_t g;
            if (power_board_get_lastgasp(&g))
            {
                power_board_persist_lastgasp(&g);
            }
        }
        /* Power block is read on demand (power_board_get_power); just clear
         * its flag so the snapshot taken there reflects the latest write. */
        (void)i2c_slave_take_block(I2C_SLAVE_BLK_POWER);

        if (ev == PROCESS_EVENT_POLL)
        {
            /* (Re)arm the periodic-log timer from the owning process. */
            if (s_log_active)
            {
                etimer_set(&s_log_timer, (clock_time_t)s_log_period_ticks);
            }
            else
            {
                etimer_stop(&s_log_timer);
            }
        }

        if ((ev == PROCESS_EVENT_TIMER) && s_log_active
            && etimer_expired(&s_log_timer))
        {
            power_board_log_telemetry();
            etimer_set(&s_log_timer, (clock_time_t)s_log_period_ticks);
        }

        if (etimer_expired(&s_drain_timer))
        {
            etimer_restart(&s_drain_timer);
        }
    }

    PROCESS_END();
}

/* ======================================================================
 *  Shell command
 * ====================================================================== */

static int power_board_shell_handler(int argc, char *argv[])
{
    if ((argc < 2) || (strcmp(argv[1], "show") == 0))
    {
        power_board_log_telemetry();
        return 0;
    }

    if (strcmp(argv[1], "power") == 0)
    {
        power_board_log_power();
        return 0;
    }

    if (strcmp(argv[1], "lastgasp") == 0)
    {
        power_board_log_lastgasp();
        return 0;
    }

    if (strcmp(argv[1], "raw") == 0)
    {
        power_board_log_raw();
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0)
    {
        if ((argc >= 3) && (strcmp(argv[2], "clear") == 0))
        {
            i2c_slave_clear_stats();
            CSLOG("[PWRB] stats cleared\r\n");
            return 0;
        }
        power_board_log_stats();
        return 0;
    }

    if (strcmp(argv[1], "cfg") == 0)
    {
        power_board_config_t c;
        power_board_get_config(&c);
        if (argc >= 4)
        {
            int cap    = xstrtoi(argv[2]);
            int crate = xstrtoi(argv[3]);
            uint16_t tcal = c.tcal_q15;
            if (argc >= 5)
            {
                tcal = (uint16_t)xstrtoi(argv[4]);
            }
            power_board_set_config((uint8_t)cap, (uint8_t)crate, tcal);
            power_board_get_config(&c);
            CSLOG("[PWRB] cfg applied: cap=%uAh crate=%u%% tcal=0x%04X gen=%u\r\n",
                  (unsigned)c.cap_ah, (unsigned)c.crate_pct,
                  (unsigned)c.tcal_q15, (unsigned)c.cfg_gen);
        }
        else
        {
            CSLOG("[PWRB] cfg: rec_ack=%u gen=%u cap=%uAh crate=%u%% tcal=0x%04X\r\n",
                  (unsigned)c.rec_ack, (unsigned)c.cfg_gen,
                  (unsigned)c.cap_ah, (unsigned)c.crate_pct, (unsigned)c.tcal_q15);
        }
        return 0;
    }

    if (strcmp(argv[1], "mon") == 0)
    {
        uint32_t period = POWER_BOARD_LOG_DEF_PERIOD_MS;
        if (argc >= 3)
        {
            int val = xstrtoi(argv[2]);
            if (val > 0)
            {
                period = (uint32_t)val;
            }
        }
        power_board_periodic_log_start(period);
        CSLOG("[PWRB] periodic log ON (%u ms)\r\n", (unsigned)period);
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        power_board_periodic_log_stop();
        CSLOG("[PWRB] periodic log OFF\r\n");
        return 0;
    }

    CSLOG("Usage: pwrboard [show]      - print telemetry once\r\n");
    CSLOG("       pwrboard power      - print power block\r\n");
    CSLOG("       pwrboard lastgasp   - print last-gasp block\r\n");
    CSLOG("       pwrboard raw        - hex dump full register file\r\n");
    CSLOG("       pwrboard stats [clear] - I2C slave diagnostics\r\n");
    CSLOG("       pwrboard cfg [cap crate [tcal]] - show/apply config\r\n");
    CSLOG("       pwrboard mon [ms]    - start periodic log (default %u ms)\r\n",
          (unsigned)POWER_BOARD_LOG_DEF_PERIOD_MS);
    CSLOG("       pwrboard stop        - stop periodic log\r\n");
    return -1;
}

/* ======================================================================
 *  Init
 * ====================================================================== */

void power_board_init(void)
{
    /* RESTORE: no stored state yet -> empty marker (0x00). The master then
     * seeds clean-battery defaults (SoC 100 %). Replace with
     * power_board_set_restore() once NVM is wired. Must be valid before the
     * slave starts listening (master boot read, doc section 7). */
    power_board_set_restore_empty();

    /* STATBLK: publish defaults matching the PowerBoard's own config so
     * "unconfigured" and "configured the same" are indistinguishable. */
    pb_statblk_publish_default();

    i2c_slave_init();

    process_start(&power_board_process, NULL);

    shell_register_command(&(shell_cmd_t){
        .cmd   = "pwrboard",
        .desc  = "PowerBoard I2C telemetry\r\n"
                 "\tpwrboard [show]    - print telemetry once\r\n"
                 "\tpwrboard power     - print power block\r\n"
                 "\tpwrboard lastgasp  - print last-gasp block\r\n"
                 "\tpwrboard raw       - hex dump full register file\r\n"
                 "\tpwrboard stats     - I2C slave diagnostics\r\n"
                 "\tpwrboard cfg [cap crate [tcal]] - show/apply config\r\n"
                 "\tpwrboard mon [ms]  - periodic log via process\r\n"
                 "\tpwrboard stop      - stop periodic log",
        .level = SHELL_LVL_USER,
        .func  = power_board_shell_handler
    });
}
