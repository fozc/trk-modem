/**
 * @file  power_board_decode.c
 * @brief Pure PUSH-block decoders for the PowerBoard I2C protocol.
 *
 * Reference: doc/PowerBoard_I2C_Protocol.md (Rev 1.0, PROT_VER 0x09);
 * block-layout authority: power-card repo, upper_board_reference/pwr_i2c_packets.h.
 * All multi-byte fields are MSB-first (big-endian).
 */
#include <stddef.h>
#include "power_board_decode.h"

/* ======================================================================
 *  Telemetry register offsets (protocol section 3b)
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
#define PB_REG_SOC_X10       ((uint8_t)0x1AU)   /* int16: -1000..+1000 */
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
#define PB_REG_CHG_REAL      ((uint8_t)0x4AU)   /* PROT_VER 0x09: real-charge verdict */
#define PB_REG_BQ_YAS_DS     ((uint8_t)0x4BU)   /* telemetry age, 0.1 s units */
#define PB_REG_BQ_ERR_N      ((uint8_t)0x4CU)   /* BQ read errors, mod-256 */
#define PB_REG_PANIC_CAUSE   ((uint8_t)0x4DU)   /* panic cause bitmask */
#define PB_REG_PSYS_ST       ((uint8_t)0x4EU)   /* PSYS_MW validity */
#define PB_REG_CAL_VER       ((uint8_t)0x4FU)   /* calibration set */
#define PB_REG_BQ_VSYS_MV    ((uint8_t)0x50U)
#define PB_REG_BQ_VBUS_MV    ((uint8_t)0x52U)
#define PB_REG_STM_VBAT_MV   ((uint8_t)0x54U)
#define PB_REG_IBAT_MA       ((uint8_t)0x56U)
#define PB_REG_IBUS_MA       ((uint8_t)0x58U)   /* int16 per contract */
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
 *  Internal helpers (MSB-first readers / salted XSUM)
 * ====================================================================== */

static uint16_t pb_rd_u16(const uint8_t *p_reg, uint8_t off)
{
    return (uint16_t)(((uint16_t)p_reg[off] << 8) | (uint16_t)p_reg[off + 1U]);
}

static int16_t pb_rd_i16(const uint8_t *p_reg, uint8_t off)
{
    return (int16_t)pb_rd_u16(p_reg, off);
}

static uint32_t pb_rd_u32(const uint8_t *p_reg, uint8_t off)
{
    return ((uint32_t)p_reg[off] << 24) |
           ((uint32_t)p_reg[off + 1U] << 16) |
           ((uint32_t)p_reg[off + 2U] << 8) |
           (uint32_t)p_reg[off + 3U];
}

uint8_t power_board_xsum(const uint8_t *p_buf, uint8_t len)
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

bool power_board_decode_telemetry(const uint8_t *p_reg,
                                  power_board_telemetry_t *p_out)
{
    if ((p_reg == NULL) || (p_out == NULL))
    {
        return false;
    }

    /* Single contract: a frame from another protocol revision is rejected
     * even when its XSUM happens to match - a field-shifted layout would
     * decode into wrong values. XSUM catches corruption, not version
     * drift (protocol Rev 1.0, single-contract rule). */
    bool xsum_ok   = (power_board_xsum(p_reg, PB_REG_TLM_XSUM)
                      == p_reg[PB_REG_TLM_XSUM]);
    bool version_ok = (p_reg[PB_REG_PROT_VER] == POWER_BOARD_PROT_VER);
    p_out->valid = (xsum_ok && version_ok);

    p_out->prot_ver    = p_reg[PB_REG_PROT_VER];
    p_out->seq         = p_reg[PB_REG_SEQ];
    p_out->rec_flag    = p_reg[PB_REG_REC_FLAG];
    p_out->blk_xsum    = p_reg[PB_REG_TLM_XSUM];

    p_out->sys_state   = p_reg[PB_REG_SYS_STATE];
    p_out->sys_fault   = p_reg[PB_REG_SYS_FAULT];
    p_out->sys_flags   = p_reg[PB_REG_SYS_FLAGS];
    p_out->chg_stat_raw = p_reg[PB_REG_CHG_STAT_RAW];

    p_out->soh_x10     = pb_rd_u16(p_reg, PB_REG_SOH_X10);
    p_out->efc         = pb_rd_u16(p_reg, PB_REG_EFC);
    p_out->equiv_hours = pb_rd_u32(p_reg, PB_REG_EQUIV_HOURS);
    p_out->gross_mah   = pb_rd_u32(p_reg, PB_REG_GROSS_MAH);

    p_out->vbat_mv     = pb_rd_u16(p_reg, PB_REG_VBAT_MV);
    p_out->vpv_mv      = pb_rd_u16(p_reg, PB_REG_VPV_MV);
    p_out->vdc_mv      = pb_rd_u16(p_reg, PB_REG_VDC_MV);
    p_out->rem_efc     = pb_rd_u16(p_reg, PB_REG_REM_EFC);
    p_out->rem_years_x10 = pb_rd_u16(p_reg, PB_REG_REM_YEARS_X10);
    /* SIGNED since the 2026-08-27 SoC model: negative SoC is normal;
     * reading it as u16 turns -1 % into 6552 % and feeds fake values
     * back through the restore path. */
    p_out->soc_x10     = pb_rd_i16(p_reg, PB_REG_SOC_X10);

    p_out->ichg_ma     = pb_rd_u16(p_reg, PB_REG_ICHG_MA);
    p_out->ibat_ma     = pb_rd_i16(p_reg, PB_REG_IBAT_MA);
    p_out->ibus_ma     = pb_rd_i16(p_reg, PB_REG_IBUS_MA);

    p_out->board_temp_c  = (int8_t)p_reg[PB_REG_BOARD_TEMP];
    p_out->batt_temp_x10 = pb_rd_i16(p_reg, PB_REG_BATT_TEMP);
    p_out->batt_ts       = p_reg[PB_REG_BATT_TS];

    p_out->chg_stat      = p_reg[PB_REG_CHG_STAT];
    p_out->chg_phase     = p_reg[PB_REG_CHG_PHASE];
    p_out->ico_stat      = p_reg[PB_REG_ICO_STAT];
    p_out->heater_state  = p_reg[PB_REG_HEATER_STATE];
    p_out->batt_state    = p_reg[PB_REG_BATT_STATE];
    p_out->batt_cap_ah   = p_reg[PB_REG_BATT_CAP_AH];
    p_out->batt_crate    = p_reg[PB_REG_BATT_CRATE];
    p_out->ichg_target_ma = pb_rd_u16(p_reg, PB_REG_ICHG_TARGET);

    p_out->bq_fault0   = p_reg[PB_REG_BQ_FAULT0];
    p_out->bq_fault1   = p_reg[PB_REG_BQ_FAULT1];
    p_out->alarm_live  = p_reg[PB_REG_ALARM_LIVE];
    p_out->alarm_latch = p_reg[PB_REG_ALARM_LATCH];

    p_out->delta_uwh   = (int32_t)pb_rd_u32(p_reg, PB_REG_DELTA_UWH);
    p_out->total_mwh   = (int32_t)pb_rd_u32(p_reg, PB_REG_TOTAL_MWH);
    p_out->total_mah   = (int32_t)pb_rd_u32(p_reg, PB_REG_TOTAL_MAH);
    p_out->bms_present = p_reg[PB_REG_BMS_PRESENT];

    p_out->bq_reg1b    = p_reg[PB_REG_BQ_REG1B];
    p_out->bq_reg1d    = p_reg[PB_REG_BQ_REG1D];
    p_out->bq_reg1e    = p_reg[PB_REG_BQ_REG1E];
    p_out->bq_reg1f    = p_reg[PB_REG_BQ_REG1F];

    p_out->pwr_io      = p_reg[PB_REG_PWR_IO];
    p_out->hiz_trig    = p_reg[PB_REG_HIZ_TRIG];
    p_out->mppt_trig   = p_reg[PB_REG_MPPT_TRIG];
    p_out->iindpm_trig = p_reg[PB_REG_IINDPM_TRIG];
    p_out->acdrv_trig  = p_reg[PB_REG_ACDRV_TRIG];
    p_out->pwr_src     = p_reg[PB_REG_PWR_SRC];

    p_out->chg_real    = p_reg[PB_REG_CHG_REAL];
    p_out->bq_yas_ds   = p_reg[PB_REG_BQ_YAS_DS];
    p_out->bq_err_n    = p_reg[PB_REG_BQ_ERR_N];
    p_out->panic_cause = p_reg[PB_REG_PANIC_CAUSE];
    p_out->psys_st     = p_reg[PB_REG_PSYS_ST];
    p_out->cal_ver     = p_reg[PB_REG_CAL_VER];

    p_out->bq_vsys_mv  = pb_rd_u16(p_reg, PB_REG_BQ_VSYS_MV);
    p_out->bq_vbus_mv  = pb_rd_u16(p_reg, PB_REG_BQ_VBUS_MV);
    p_out->stm_vbat_mv = pb_rd_u16(p_reg, PB_REG_STM_VBAT_MV);
    p_out->bq_vac1_mv  = pb_rd_u16(p_reg, PB_REG_BQ_VAC1_MV);
    p_out->bq_vac2_mv  = pb_rd_u16(p_reg, PB_REG_BQ_VAC2_MV);
    p_out->bq_tdie_c   = (int8_t)p_reg[PB_REG_BQ_TDIE_C];

    return p_out->valid;
}

bool power_board_decode_power(const uint8_t *p_reg,
                              power_board_power_t *p_out)
{
    if ((p_reg == NULL) || (p_out == NULL))
    {
        return false;
    }

    p_out->valid = (power_board_xsum(p_reg, (uint8_t)(POWER_BOARD_PWR_LEN - 1U))
                    == p_reg[POWER_BOARD_PWR_LEN - 1U]);

    p_out->ppv_mw  = (int32_t)pb_rd_u32(p_reg, PB_PWR_PPV);
    p_out->pdc_mw  = (int32_t)pb_rd_u32(p_reg, PB_PWR_PDC);
    p_out->psys_mw = (int32_t)pb_rd_u32(p_reg, PB_PWR_PSYS);
    p_out->pbat_mw = (int32_t)pb_rd_u32(p_reg, PB_PWR_PBAT);
    p_out->pin_mw  = (int32_t)pb_rd_u32(p_reg, PB_PWR_PIN);

    return p_out->valid;
}

bool power_board_decode_lastgasp(const uint8_t *p_reg,
                                 power_board_lastgasp_t *p_out)
{
    if ((p_reg == NULL) || (p_out == NULL))
    {
        return false;
    }

    bool marker_ok = (p_reg[0] == POWER_BOARD_LG_MARKER);
    bool xsum_ok   = (power_board_xsum(p_reg, (uint8_t)(POWER_BOARD_LG_LEN - 1U))
                      == p_reg[POWER_BOARD_LG_LEN - 1U]);
    p_out->valid = (marker_ok && xsum_ok);

    p_out->reason      = p_reg[1];
    p_out->soh_x10     = pb_rd_u16(p_reg, 2U);
    p_out->efc         = pb_rd_u16(p_reg, 4U);
    p_out->equiv_hours = pb_rd_u32(p_reg, 6U);
    p_out->gross_mah   = pb_rd_u32(p_reg, 10U);
    p_out->total_mwh   = pb_rd_u32(p_reg, 14U);
    p_out->total_mah   = pb_rd_u32(p_reg, 18U);
    p_out->vbat_mv     = pb_rd_u16(p_reg, 22U);
    p_out->cap_ah      = p_reg[24];
    p_out->soc_pct     = p_reg[25];

    return p_out->valid;
}
