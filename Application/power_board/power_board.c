/**
 * @file  power_board.c
 * @brief PowerBoard I2C-slave telemetry access, logging and shell.
 *
 * The upper board acts as the I2C slave (0x48); the PowerBoard master
 * pushes a 96-byte telemetry block into the slave register file. This
 * module snapshots that block, verifies its checksum, decodes the fields
 * (MSB-first) and offers one-shot and periodic logging via the shell.
 *
 * Reference: doc/I2C_SLAVE_ENTEGRASYON_16K.md (BLTX-PWRB-I2C-SLV-16K).
 */

#include <string.h>
#include "power_board.h"
#include "i2c_slave.h"
#include "bsp.h"
#include "contiki.h"
#include "shell.h"
#include "utils.h"
#include "console_logger.h"

/* ======================================================================
 *  Telemetry register offsets (doc section 4)
 * ====================================================================== */

#define PB_REG_SYS_STATE     ((uint8_t)0x00U)
#define PB_REG_SYS_FAULT     ((uint8_t)0x01U)
#define PB_REG_SYS_FLAGS     ((uint8_t)0x02U)
#define PB_REG_SOH_X10       ((uint8_t)0x04U)
#define PB_REG_VBAT_MV       ((uint8_t)0x10U)
#define PB_REG_VPV_MV        ((uint8_t)0x12U)
#define PB_REG_VDC_MV        ((uint8_t)0x14U)
#define PB_REG_SOC_X10       ((uint8_t)0x1AU)
#define PB_REG_REC_FLAG      ((uint8_t)0x1CU)
#define PB_REG_SEQ           ((uint8_t)0x1DU)
#define PB_REG_PROT_VER      ((uint8_t)0x1EU)
#define PB_REG_BLK_XSUM      ((uint8_t)0x1FU)
#define PB_REG_ICHG_MA       ((uint8_t)0x20U)
#define PB_REG_BOARD_TEMP    ((uint8_t)0x22U)
#define PB_REG_BATT_TS       ((uint8_t)0x23U)
#define PB_REG_BATT_TEMP     ((uint8_t)0x24U)
#define PB_REG_BQ_FAULT0     ((uint8_t)0x26U)
#define PB_REG_BQ_FAULT1     ((uint8_t)0x27U)
#define PB_REG_CHG_STAT      ((uint8_t)0x28U)
#define PB_REG_BATT_CAP_AH   ((uint8_t)0x2CU)
#define PB_REG_ALARM_LIVE    ((uint8_t)0x30U)
#define PB_REG_ALARM_LATCH   ((uint8_t)0x31U)
#define PB_REG_CHG_PHASE     ((uint8_t)0x32U)
#define PB_REG_PWR_IO        ((uint8_t)0x44U)
#define PB_REG_BQ_VSYS_MV    ((uint8_t)0x50U)
#define PB_REG_BQ_VBUS_MV    ((uint8_t)0x52U)
#define PB_REG_IBAT_MA       ((uint8_t)0x56U)
#define PB_REG_IBUS_MA       ((uint8_t)0x58U)
#define PB_REG_BQ_VAC1_MV    ((uint8_t)0x5AU)
#define PB_REG_BQ_VAC2_MV    ((uint8_t)0x5CU)
#define PB_REG_BQ_TDIE_C     ((uint8_t)0x5EU)

/* ======================================================================
 *  Module state
 * ====================================================================== */

/** Periodic logging control (written by any context, read by process). */
static volatile bool     s_log_active;
static volatile uint32_t s_log_period_ticks;
static struct etimer     s_log_timer;

PROCESS(power_board_process, "pwrboard");

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

/**
 * @brief Read a big-endian (MSB-first) unsigned 16-bit field (doc K3).
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
 * @brief Verify the telemetry checksum BLK_XSUM (doc section 5.1).
 *
 * XSUM = XOR of bytes 0x00..0x5F, excluding 0x1F itself.
 */
static bool pb_xsum_ok(const uint8_t *p_reg)
{
    uint8_t x = 0U;
    for (uint8_t i = 0U; i <= PB_REG_BQ_TDIE_C; i++)
    {
        if (i != PB_REG_BLK_XSUM)
        {
            x = (uint8_t)(x ^ p_reg[i]);
        }
    }
    return (x == p_reg[PB_REG_BLK_XSUM]);
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

bool power_board_get_telemetry(power_board_telemetry_t *p_out)
{
    if (p_out == NULL)
    {
        return false;
    }

    uint8_t reg[POWER_BOARD_TLM_SIZE];
    i2c_slave_snapshot(reg, 0U, POWER_BOARD_TLM_SIZE);

    p_out->valid        = pb_xsum_ok(reg);
    p_out->prot_ver     = reg[PB_REG_PROT_VER];
    p_out->seq          = reg[PB_REG_SEQ];
    p_out->rec_flag     = reg[PB_REG_REC_FLAG];
    p_out->blk_xsum     = reg[PB_REG_BLK_XSUM];

    p_out->sys_state    = reg[PB_REG_SYS_STATE];
    p_out->sys_fault    = reg[PB_REG_SYS_FAULT];
    p_out->sys_flags    = reg[PB_REG_SYS_FLAGS];

    p_out->soh_x10      = pb_rd_u16(reg, PB_REG_SOH_X10);
    p_out->soc_x10      = pb_rd_u16(reg, PB_REG_SOC_X10);

    p_out->vbat_mv      = pb_rd_u16(reg, PB_REG_VBAT_MV);
    p_out->vpv_mv       = pb_rd_u16(reg, PB_REG_VPV_MV);
    p_out->vdc_mv       = pb_rd_u16(reg, PB_REG_VDC_MV);

    p_out->ichg_ma      = pb_rd_u16(reg, PB_REG_ICHG_MA);
    p_out->ibat_ma      = pb_rd_i16(reg, PB_REG_IBAT_MA);
    p_out->ibus_ma      = pb_rd_u16(reg, PB_REG_IBUS_MA);

    p_out->board_temp_c = (int8_t)reg[PB_REG_BOARD_TEMP];
    p_out->batt_temp_x10 = pb_rd_i16(reg, PB_REG_BATT_TEMP);
    p_out->batt_ts      = reg[PB_REG_BATT_TS];

    p_out->chg_stat     = reg[PB_REG_CHG_STAT];
    p_out->chg_phase    = reg[PB_REG_CHG_PHASE];
    p_out->batt_cap_ah  = reg[PB_REG_BATT_CAP_AH];

    p_out->bq_fault0    = reg[PB_REG_BQ_FAULT0];
    p_out->bq_fault1    = reg[PB_REG_BQ_FAULT1];
    p_out->alarm_live   = reg[PB_REG_ALARM_LIVE];
    p_out->alarm_latch  = reg[PB_REG_ALARM_LATCH];
    p_out->pwr_io       = reg[PB_REG_PWR_IO];

    p_out->bq_vsys_mv   = pb_rd_u16(reg, PB_REG_BQ_VSYS_MV);
    p_out->bq_vbus_mv   = pb_rd_u16(reg, PB_REG_BQ_VBUS_MV);
    p_out->bq_vac1_mv   = pb_rd_u16(reg, PB_REG_BQ_VAC1_MV);
    p_out->bq_vac2_mv   = pb_rd_u16(reg, PB_REG_BQ_VAC2_MV);
    p_out->bq_tdie_c    = (int8_t)reg[PB_REG_BQ_TDIE_C];

    return p_out->valid;
}

void power_board_log_telemetry(void)
{
    power_board_telemetry_t t;
    (void)power_board_get_telemetry(&t);

    CSLOG_NODT("[PWRB] === PowerBoard Telemetry ===\r\n");
    CSLOG_NODT("[PWRB] XSUM:%s  PROT_VER:0x%02X  SEQ:%u  REC:0x%02X\r\n",
               t.valid ? "OK" : "FAIL",
               (unsigned)t.prot_ver, (unsigned)t.seq, (unsigned)t.rec_flag);
    CSLOG_NODT("[PWRB] SYS_STATE:0x%02X  SYS_FAULT:0x%02X  SYS_FLAGS:0x%02X\r\n",
               (unsigned)t.sys_state, (unsigned)t.sys_fault,
               (unsigned)t.sys_flags);
    CSLOG_NODT("[PWRB] SoH:%u.%u %%  SoC:%u.%u %%\r\n",
               (unsigned)(t.soh_x10 / 10U), (unsigned)(t.soh_x10 % 10U),
               (unsigned)(t.soc_x10 / 10U), (unsigned)(t.soc_x10 % 10U));
    CSLOG_NODT("[PWRB] VBAT:%u mV  VPV:%u mV  VDC:%u mV\r\n",
               (unsigned)t.vbat_mv, (unsigned)t.vpv_mv, (unsigned)t.vdc_mv);
    CSLOG_NODT("[PWRB] ICHG:%u mA  IBAT:%d mA  IBUS:%u mA\r\n",
               (unsigned)t.ichg_ma, (int)t.ibat_ma, (unsigned)t.ibus_ma);

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

    CSLOG_NODT("[PWRB] CHG_STAT:%u  PHASE:%u  CAP:%u Ah\r\n",
               (unsigned)t.chg_stat, (unsigned)t.chg_phase,
               (unsigned)t.batt_cap_ah);
    CSLOG_NODT("[PWRB] BQ_FAULT0:0x%02X  BQ_FAULT1:0x%02X\r\n",
               (unsigned)t.bq_fault0, (unsigned)t.bq_fault1);
    CSLOG_NODT("[PWRB] ALARM live:0x%02X  latch:0x%02X  PWR_IO:0x%02X\r\n",
               (unsigned)t.alarm_live, (unsigned)t.alarm_latch,
               (unsigned)t.pwr_io);
    CSLOG_NODT("[PWRB] BQ VSYS:%u  VBUS:%u  VAC1/DC:%u  VAC2/PV:%u mV  TDIE:%d C\r\n",
               (unsigned)t.bq_vsys_mv, (unsigned)t.bq_vbus_mv,
               (unsigned)t.bq_vac1_mv, (unsigned)t.bq_vac2_mv,
               (int)t.bq_tdie_c);
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

void power_board_set_restore(uint16_t soh_x10)
{
    /* Restore block layout (doc section 7), 0x60..0x71 = 18 bytes:
     *   0x60 MARKER = 0xA5
     *   0x61 SoH x10 MSB-first (2 B)
     *   0x63..0x70 other fields (16K: zero; 32K: real values)
     *   0x71 XSUM = XOR of 0x60..0x70 (17 bytes, MARKER included) */
    uint8_t blk[POWER_BOARD_RESTORE_LEN];

    blk[0] = POWER_BOARD_RESTORE_MARKER;
    blk[1] = (uint8_t)(soh_x10 >> 8);
    blk[2] = (uint8_t)(soh_x10 & 0xFFU);
    for (uint8_t i = 3U; i <= 16U; i++)   /* 0x63..0x70 */
    {
        blk[i] = 0U;
    }

    uint8_t x = 0U;
    for (uint8_t i = 0U; i <= 16U; i++)   /* 17 bytes, marker included */
    {
        x = (uint8_t)(x ^ blk[i]);
    }
    blk[17] = x;                          /* 0x71 XSUM */

    i2c_slave_write_block(POWER_BOARD_RESTORE_BASE, blk, POWER_BOARD_RESTORE_LEN);
}

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
 *  Contiki process - periodic telemetry logging
 * ====================================================================== */

PROCESS_THREAD(power_board_process, ev, data)
{
    (void)data;

    PROCESS_BEGIN();

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL((ev == PROCESS_EVENT_POLL)
                                 || (ev == PROCESS_EVENT_TIMER));

        if (ev == PROCESS_EVENT_POLL)
        {
            /* Start/stop the periodic timer from the owning process. */
            if (s_log_active)
            {
                etimer_set(&s_log_timer, (clock_time_t)s_log_period_ticks);
            }
            else
            {
                etimer_stop(&s_log_timer);
            }
        }
        else /* PROCESS_EVENT_TIMER */
        {
            if (s_log_active && etimer_expired(&s_log_timer))
            {
                power_board_log_telemetry();
                etimer_set(&s_log_timer, (clock_time_t)s_log_period_ticks);
            }
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
    CSLOG("       pwrboard raw         - hex dump full register file\r\n");
    CSLOG("       pwrboard stats [clear] - I2C slave diagnostics\r\n");
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
    /* Publish a valid restore block before the slave starts listening, so
     * the master's boot read (doc section 7) gets correct data. The SoH
     * source is a default for now; wire it to NVRAM/flash later. */
    power_board_set_restore(POWER_BOARD_SOH_DEFAULT_X10);

    i2c_slave_init();

    process_start(&power_board_process, NULL);

    shell_register_command(&(shell_cmd_t){
        .cmd   = "pwrboard",
        .desc  = "PowerBoard I2C telemetry\r\n"
                 "\tpwrboard [show]   - print telemetry once\r\n"
                 "\tpwrboard raw      - hex dump full register file\r\n"
                 "\tpwrboard stats    - I2C slave diagnostics\r\n"
                 "\tpwrboard mon [ms] - periodic log via process\r\n"
                 "\tpwrboard stop     - stop periodic log",
        .level = SHELL_LVL_USER,
        .func  = power_board_shell_handler
    });
}
