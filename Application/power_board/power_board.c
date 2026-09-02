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
 * Reference: doc/PowerBoard_I2C_Protocol.md (Rev 1.0, PROT_VER 0x09);
 * block-layout authority: power-card repo, upper_board_reference/pwr_i2c_packets.h.
 * Wire-format decode lives in power_board_decode.c (host-testable).
 */

#include <string.h>
#include "power_board.h"
#include "power_board_decode.h"
#include "i2c_slave.h"
#include "bsp.h"
#include "contiki.h"
#include "shell.h"
#include "utils.h"
#include "console_logger.h"
#include "elog.h"

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
 *  Internal helpers (MSB-first writers for the PULL blocks)
 * ====================================================================== */

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

/* ======================================================================
 *  PUSH blocks: decode
 * ====================================================================== */

bool power_board_get_telemetry(power_board_telemetry_t *p_out)
{
    static uint8_t s_last_prot_ver = 0xFFU;

    if (p_out == NULL)
    {
        return false;
    }

    uint8_t reg[POWER_BOARD_TLM_SIZE];
    i2c_slave_snapshot(reg, POWER_BOARD_TLM_BASE, POWER_BOARD_TLM_SIZE);

    (void)power_board_decode_telemetry(reg, p_out);

    /* Log a protocol-version change once, not on every ~1 s frame. */
    if (p_out->prot_ver != s_last_prot_ver)
    {
        s_last_prot_ver = p_out->prot_ver;
        if (p_out->prot_ver != POWER_BOARD_PROT_VER)
        {
            CSLOG_WARN("[PWRB] PROT_VER 0x%02X != expected 0x%02X - frames rejected\r\n",
                       (unsigned)p_out->prot_ver,
                       (unsigned)POWER_BOARD_PROT_VER);
        }
    }

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

    (void)power_board_decode_power(blk, p_out);
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

    (void)power_board_decode_lastgasp(blk, p_out);
    return p_out->valid;
}

/* ======================================================================
 *  Logging
 * ====================================================================== */

void power_board_log_telemetry(void)
{
    power_board_telemetry_t t;
    (void)power_board_get_telemetry(&t);

    /* Signed SoC: negative is normal since the 2026-08-27 model. */
    int soc_abs = (t.soc_x10 < 0) ? -t.soc_x10 : t.soc_x10;

    SHELL_LOG("[PWRB] === PowerBoard Telemetry ===\r\n");
    SHELL_LOG("       XSUM:%s  PROT_VER:0x%02X  SEQ:%u  REC:0x%02X\r\n",
               t.valid ? "OK" : "FAIL",
               (unsigned)t.prot_ver, (unsigned)t.seq, (unsigned)t.rec_flag);
    SHELL_LOG("       SYS_STATE:0x%02X  SYS_FAULT:0x%02X  SYS_FLAGS:0x%02X  CHG_RAW:0x%02X\r\n",
               (unsigned)t.sys_state, (unsigned)t.sys_fault,
               (unsigned)t.sys_flags, (unsigned)t.chg_stat_raw);
    SHELL_LOG("       SoH:%u.%u%%  SoC:%s%d.%d%%  EFC:%u cyc  yrs_left:%u.%u yr\r\n",
               (unsigned)(t.soh_x10 / 10U), (unsigned)(t.soh_x10 % 10U),
               (t.soc_x10 < 0) ? "-" : "",
               soc_abs / 10, soc_abs % 10,
               (unsigned)t.efc,
               (unsigned)(t.rem_years_x10 / 10U), (unsigned)(t.rem_years_x10 % 10U));
    SHELL_LOG("       VBAT:%umV  VPV:%umV  VDC:%umV  (STM_VBAT:%umV)\r\n",
               (unsigned)t.vbat_mv, (unsigned)t.vpv_mv, (unsigned)t.vdc_mv,
               (unsigned)t.stm_vbat_mv);
    SHELL_LOG("       ICHG:%umA  IBAT:%dmA  IBUS:%dmA  (target:%umA)\r\n",
               (unsigned)t.ichg_ma, (int)t.ibat_ma, (int)t.ibus_ma,
               (unsigned)t.ichg_target_ma);
    SHELL_LOG("       CHG_REAL:%u  BQ_YAS:%u ds  BQ_ERR:%u  PSYS_ST:%u  PANIC:0x%02X  CAL:%u\r\n",
               (unsigned)t.chg_real, (unsigned)t.bq_yas_ds,
               (unsigned)t.bq_err_n, (unsigned)t.psys_st,
               (unsigned)t.panic_cause, (unsigned)t.cal_ver);

    if (t.board_temp_c == POWER_BOARD_BOARD_TEMP_ERROR)
    {
        SHELL_LOG("       BoardT:err  ");
    }
    else
    {
        SHELL_LOG("       BoardT:%d C  ", (int)t.board_temp_c);
    }

    if (t.batt_temp_x10 == POWER_BOARD_BATT_TEMP_INVALID)
    {
        SHELL_LOG("BattT:n/a  TS:%u\r\n", (unsigned)t.batt_ts);
    }
    else
    {
        int16_t bt  = t.batt_temp_x10;
        bool    neg = (bt < 0);
        int     mag = neg ? -(int)bt : (int)bt;
        SHELL_LOG("BattT:%s%d.%d C  TS:%u\r\n",
                   neg ? "-" : "", mag / 10, mag % 10,
                   (unsigned)t.batt_ts);
    }

    SHELL_LOG("       CHG:%u  PHASE:%u  ICO:%u  HEATER:%u  BATT:%u  CAP:%uAh  CRATE:%u%%\r\n",
               (unsigned)t.chg_stat, (unsigned)t.chg_phase, (unsigned)t.ico_stat,
               (unsigned)t.heater_state, (unsigned)t.batt_state,
               (unsigned)t.batt_cap_ah, (unsigned)t.batt_crate);
    SHELL_LOG("       energy: delta=%ld uWh  total=%ld mWh/%ld mAh  equiv=%luh  gross=%lumAh\r\n",
               (long)t.delta_uwh, (long)t.total_mwh, (long)t.total_mah,
               (unsigned long)t.equiv_hours, (unsigned long)t.gross_mah);
    SHELL_LOG("       ALARM live:0x%02X latch:0x%02X  PWR_IO:0x%02X  PWR_SRC:0x%02X\r\n",
               (unsigned)t.alarm_live, (unsigned)t.alarm_latch,
               (unsigned)t.pwr_io, (unsigned)t.pwr_src);
    SHELL_LOG("       BQ FAULT0:0x%02X FAULT1:0x%02X  R1B:0x%02X R1D:0x%02X R1E:0x%02X R1F:0x%02X\r\n",
               (unsigned)t.bq_fault0, (unsigned)t.bq_fault1,
               (unsigned)t.bq_reg1b, (unsigned)t.bq_reg1d,
               (unsigned)t.bq_reg1e, (unsigned)t.bq_reg1f);
    SHELL_LOG("       BQ VSYS:%umV VBUS:%umV VAC1/DC:%umV VAC2/PV:%umV  TDIE:%d C\r\n",
               (unsigned)t.bq_vsys_mv, (unsigned)t.bq_vbus_mv,
               (unsigned)t.bq_vac1_mv, (unsigned)t.bq_vac2_mv,
               (int)t.bq_tdie_c);
    SHELL_LOG("       watchdogs HIZ:%u MPPT:%u IINDPM:%u ACDRV:%u\r\n",
               (unsigned)t.hiz_trig, (unsigned)t.mppt_trig,
               (unsigned)t.iindpm_trig, (unsigned)t.acdrv_trig);
}

void power_board_log_power(void)
{
    power_board_power_t p;
    (void)power_board_get_power(&p);

    SHELL_LOG("[PWRB] === Power Block (%s) ===\r\n", p.valid ? "OK" : "FAIL");
    SHELL_LOG("       PPV:%ldmW  PDC:%ldmW  PSYS:%ldmW  PBAT:%ldmW  PIN:%ldmW\r\n",
               (long)p.ppv_mw, (long)p.pdc_mw, (long)p.psys_mw,
               (long)p.pbat_mw, (long)p.pin_mw);
}

void power_board_log_lastgasp(void)
{
    power_board_lastgasp_t g;
    (void)power_board_get_lastgasp(&g);

    SHELL_LOG("[PWRB] === LastGasp (%s) ===\r\n", g.valid ? "OK" : "FAIL");
    SHELL_LOG("       reason:0x%02X  SoC:%u%%  SoH:%u.%u%%  VBAT:%umV  CAP:%uAh\r\n",
               (unsigned)g.reason, (unsigned)g.soc_pct,
               (unsigned)(g.soh_x10 / 10U), (unsigned)(g.soh_x10 % 10U),
               (unsigned)g.vbat_mv, (unsigned)g.cap_ah);
    SHELL_LOG("       EFC:%u cyc  equiv:%luh  gross:%lumAh  total:%lumWh/%lumAh\r\n",
               (unsigned)g.efc, (unsigned long)g.equiv_hours,
               (unsigned long)g.gross_mah, (unsigned long)g.total_mwh,
               (unsigned long)g.total_mah);
}

void power_board_log_raw(void)
{
    uint8_t reg[I2C_SLAVE_REG_COUNT];
    i2c_slave_snapshot(reg, 0U, (uint8_t)I2C_SLAVE_REG_COUNT);

    SHELL_LOG("[PWRB] === Register dump (%u bytes) ===\r\n",
               (unsigned)I2C_SLAVE_REG_COUNT);
    SHELL_LOG("           00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\r\n");

    for (uint16_t row = 0U; row < (uint16_t)I2C_SLAVE_REG_COUNT; row += 16U)
    {
        SHELL_LOG("       %02X:", (unsigned)row);
        for (uint8_t col = 0U; col < 16U; col++)
        {
            SHELL_LOG(" %02X", (unsigned)reg[row + col]);
        }
        SHELL_LOG("\r\n");
    }
}

void power_board_log_stats(void)
{
    i2c_slave_stats_t s;
    i2c_slave_get_stats(&s);

    SHELL_LOG("[PWRB] === I2C slave stats ===\r\n");
    SHELL_LOG("       addr wr:%u  rd:%u  rx:%u  tx:%u  stop:%u\r\n",
               (unsigned)s.addr_write, (unsigned)s.addr_read,
               (unsigned)s.rx_bytes, (unsigned)s.tx_bytes,
               (unsigned)s.listen_cplt);
    SHELL_LOG("       err AF:%u  BERR:%u  ARLO:%u  OVR:%u  other:%u\r\n",
               (unsigned)s.err_af, (unsigned)s.err_berr,
               (unsigned)s.err_arlo, (unsigned)s.err_ovr,
               (unsigned)s.err_other);
    SHELL_LOG("       recover:%u  last_error:0x%08X\r\n",
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
    /* 0x63 is a signed contract field, but restore only ever publishes
     * a valid SoC: clamp into 0..1000 (never seed a negative value). */
    int16_t soc = p_in->soc_x10;
    if (soc < 0)
    {
        soc = 0;
    }
    if (soc > 1000)
    {
        soc = 1000;
    }
    pb_wr_u16(blk, 3U,  (uint16_t)soc);                  /* 0x63 soc_x10 */
    pb_wr_u32(blk, 5U,  p_in->equiv_hours);             /* 0x65 */
    pb_wr_u32(blk, 9U,  p_in->gross_mah);               /* 0x69 */
    pb_wr_u32(blk, 13U, (uint32_t)p_in->total_mwh);     /* 0x6D */
    blk[17] = power_board_xsum(blk, (uint8_t)(POWER_BOARD_RESTORE_LEN - 1U));  /* 0x71 */

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
    blk[17] = power_board_xsum(blk, (uint8_t)(POWER_BOARD_RESTORE_LEN - 1U));

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
    s_statblk[6] = power_board_xsum(s_statblk, (uint8_t)(POWER_BOARD_STATBLK_LEN - 1U));

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
            SHELL_LOG("[PWRB] stats cleared\r\n");
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
            SHELL_LOG("[PWRB] cfg applied: cap=%uAh crate=%u%% tcal=0x%04X gen=%u\r\n",
                  (unsigned)c.cap_ah, (unsigned)c.crate_pct,
                  (unsigned)c.tcal_q15, (unsigned)c.cfg_gen);
        }
        else
        {
            SHELL_LOG("[PWRB] cfg: rec_ack=%u gen=%u cap=%uAh crate=%u%% tcal=0x%04X\r\n",
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
        SHELL_LOG("[PWRB] periodic log ON (%u ms)\r\n", (unsigned)period);
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        power_board_periodic_log_stop();
        SHELL_LOG("[PWRB] periodic log OFF\r\n");
        return 0;
    }

    SHELL_LOG("Usage: pwrboard [show]      - print telemetry once\r\n");
    SHELL_LOG("       pwrboard power      - print power block\r\n");
    SHELL_LOG("       pwrboard lastgasp   - print last-gasp block\r\n");
    SHELL_LOG("       pwrboard raw        - hex dump full register file\r\n");
    SHELL_LOG("       pwrboard stats [clear] - I2C slave diagnostics\r\n");
    SHELL_LOG("       pwrboard cfg [cap crate [tcal]] - show/apply config\r\n");
    SHELL_LOG("       pwrboard mon [ms]    - start periodic log (default %u ms)\r\n",
          (unsigned)POWER_BOARD_LOG_DEF_PERIOD_MS);
    SHELL_LOG("       pwrboard stop        - stop periodic log\r\n");
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
