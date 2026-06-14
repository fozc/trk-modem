/*
 * breaker.c
 *
 *  Created on: 15 Agu 2025
 *      Author: fatih
 */
#include <stdio.h>
#include <string.h>
#include "breaker.h"
#include "nvram.h"
#include "bsp.h"
#include "shell.h"
#include "utils.h"
 
#define breaker (nvram_get_breaker_rw())

static bool breaker_initialized = false;
static breaker_data_t breaker_data = {0};

static inline bool is_ioa_equal(ioa_3byte_t ioa1, ioa_3byte_t ioa2)
{
    return (ioa1.ioa_high == ioa2.ioa_high &&
            ioa1.ioa_low == ioa2.ioa_low &&
            ioa1.ioa_mid == ioa2.ioa_mid);
}

/* ------------------------------------------------------------------ */
/*  Shell command: dump a line's L1/L2/L3 phase data                  */
/* ------------------------------------------------------------------ */

static void breaker_shell_dump_line(uint32_t line_index)
{
    const feeder_data_t *p_feeder = breaker_get_feeder_data(line_index);
    const power_line_t  *p_line   = breaker_get_power_line_by_idx(line_index);

    if ((p_feeder == NULL) || (p_line == NULL)) {
        CSLOG_WARN("[BREAKER] Line %u: no data\r\n", (unsigned)(line_index + 1U));
        return;
    }

    CCSLOG_NODTCSLOG("[BREAKER] === Line %u (index %u) ===\r\n",
          (unsigned)(line_index + 1U), (unsigned)line_index);
    CCSLOG_NODT("[BREAKER] In use: %s\r\n", p_line->iec104.in_use ? "yes" : "no");
    CCSLOG_NODT("[BREAKER] %-22s | %10s | %10s | %10s\r\n",
          "Field", "L1", "L2", "L3");

    CCSLOG_NODT("[BREAKER] %-22s | %10.3f | %10.3f | %10.3f\r\n", "ariza_akimi (A)",
          p_feeder->phase[PHASE_L1].ariza_akimi,
          p_feeder->phase[PHASE_L2].ariza_akimi,
          p_feeder->phase[PHASE_L3].ariza_akimi);
    CCSLOG_NODT("[BREAKER] %-22s | %10.3f | %10.3f | %10.3f\r\n", "anlik_akim (A)",
          p_feeder->phase[PHASE_L1].anlik_akim,
          p_feeder->phase[PHASE_L2].anlik_akim,
          p_feeder->phase[PHASE_L3].anlik_akim);
    CCSLOG_NODT("[BREAKER] %-22s | %10.0f | %10.0f | %10.0f\r\n", "ariza_suresi (ms)",
          p_feeder->phase[PHASE_L1].ariza_suresi,
          p_feeder->phase[PHASE_L2].ariza_suresi,
          p_feeder->phase[PHASE_L3].ariza_suresi);
    CCSLOG_NODT("[BREAKER] %-22s | %10u | %10u | %10u\r\n", "ariza_kalicimi",
          (unsigned)p_feeder->phase[PHASE_L1].ariza_kalicimi,
          (unsigned)p_feeder->phase[PHASE_L2].ariza_kalicimi,
          (unsigned)p_feeder->phase[PHASE_L3].ariza_kalicimi);
    CCSLOG_NODT("[BREAKER] %-22s | %10u | %10u | %10u\r\n", "enerji_varyok",
          (unsigned)p_feeder->phase[PHASE_L1].enerji_varyok,
          (unsigned)p_feeder->phase[PHASE_L2].enerji_varyok,
          (unsigned)p_feeder->phase[PHASE_L3].enerji_varyok);
    CCSLOG_NODT("[BREAKER] %-22s | %10u | %10u | %10u\r\n", "nominal_akim_varyok",
          (unsigned)p_feeder->phase[PHASE_L1].nominal_akim_varyok,
          (unsigned)p_feeder->phase[PHASE_L2].nominal_akim_varyok,
          (unsigned)p_feeder->phase[PHASE_L3].nominal_akim_varyok);
    CCSLOG_NODT("[BREAKER] %-22s | %10u | %10u | %10u\r\n", "rf_haberlesme_varyok",
          (unsigned)p_feeder->phase[PHASE_L1].rf_haberlesme_varyok,
          (unsigned)p_feeder->phase[PHASE_L2].rf_haberlesme_varyok,
          (unsigned)p_feeder->phase[PHASE_L3].rf_haberlesme_varyok);
}

static int breaker_shell_handler(int argc, char *argv[])
{
    if (argc < 2) {
        CSLOG("Usage: breaker <line 1..%u | all>\r\n", (unsigned)MAX_POWER_LINE_COUNT);
        return -1;
    }

    if (strcmp(argv[1], "all") == 0) {
        for (uint32_t i = 0U; i < MAX_POWER_LINE_COUNT; i++) {
            breaker_shell_dump_line(i);
        }
        return 0;
    }

    int line_no = xstrtoi(argv[1]);

    if ((line_no < 1) || (line_no > (int)MAX_POWER_LINE_COUNT)) {
        CSLOG("[BREAKER] Invalid line %d (valid: 1..%u or 'all')\r\n",
              line_no, (unsigned)MAX_POWER_LINE_COUNT);
        return -1;
    }

    breaker_shell_dump_line((uint32_t)(line_no - 1));
    return 0;
}

void breaker_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "breaker",
        .desc  = "Dump a power line's L1/L2/L3 phase data\r\n"
                 "\tbreaker <line 1..8> - show feeder values for one line\r\n"
                 "\tbreaker all         - show feeder values for all lines",
        .level = SHELL_LVL_USER,
        .func  = breaker_shell_handler
    });
}

uint8_t breaker_get_active_powerline_count(void)
{
    uint8_t count = 0;
    for(size_t i = 0; i < MAX_POWER_LINE_COUNT; i++){
        if(breaker->line[i].iec104.in_use){
            count++;
        }
    }
    return count;
}

const power_line_t * breaker_get_power_line(uint32_t idx)
{
	if(idx >= MAX_POWER_LINE_COUNT){
		return NULL;
	}

	return &breaker->line[idx];
}

const power_line_t * breaker_get_power_line_by_idx(uint32_t line_index)
{
    if (line_index >= MAX_POWER_LINE_COUNT) {
        CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
        return NULL;
    }

    return &breaker->line[line_index];
}

bool breaker_set_feeder_data(uint32_t line_index, const feeder_data_t *feeder_data)
{
	if (line_index >= MAX_POWER_LINE_COUNT) {
		CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
		return false;
	}

	memcpy(&breaker_data.feeder[line_index], feeder_data, sizeof(feeder_data_t));
	return true;
}

const feeder_data_t * breaker_get_feeder_data(uint32_t line_index)
{
	if (line_index >= MAX_POWER_LINE_COUNT) {
		CCSLOG(XCOLOR_RED, "Invalid parameters for power line phase set\r\n");
		return NULL;
	}

	return &breaker_data.feeder[line_index];
}



#ifdef C_SC_NA_1_ENABLED
int breaker_set_power_line_sbo_state(ioa_3byte_t ioa, sbo_state_t state)
{
    if (!breaker_initialized) {
        return -1;
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /* && is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            breaker->line[i].sbo_state = state;
            //TODO: match the ioa
            return 0;
        }
    }

    return -2;
}

sbo_state_t breaker_get_power_line_sbo_state(ioa_3byte_t ioa)
{
    if (!breaker_initialized) {
        return (sbo_state_t){0};
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /*&& is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            return breaker->line[i].sbo_state;
            //TODO: match the ioa
        }
    }

    return (sbo_state_t){0};
}

bool breaker_is_ioa_valid(ioa_3byte_t ioa)
{
    if (!breaker_initialized) {
        return false;
    }

    for (size_t i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        if (breaker->line[i].iec104.in_use /*&& is_ioa_equal(breaker->line[i].iec104.c_sc_na_1_ioa, ioa)*/) {
            return true;
        }
    }

    return false;
}
#endif
