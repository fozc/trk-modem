/*
 * fault_log.c
 *
 *  Created on: Feb 21, 2026
 *      Author: fatih
 */
#include "fault_log.h"
#include "w25qxx.h"
#include "w25qxx_memory_organization.h"
#include "bsp.h"
#include "crc32.h"
#include "datetime.h"
#include "shell.h"
#include "cp56time2a.h"

#define FAULT_LOG_COUNT 15

#define FAULT_LOG_FEEDER_1_ADDRESS          (FAULT_LOG_ADDRESS)
#define FAULT_LOG_FEEDER_2_ADDRESS          (FAULT_LOG_ADDRESS + 4096U)
#define FAULT_LOG_FEEDER_3_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*2)
#define FAULT_LOG_FEEDER_4_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*3)
#define FAULT_LOG_FEEDER_5_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*4)
#define FAULT_LOG_FEEDER_6_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*5)
#define FAULT_LOG_FEEDER_7_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*6)
#define FAULT_LOG_FEEDER_8_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*7)

#define INVALID_FAULT_LOG_ADDRESS 0xFFFFFFFF

const uint32_t fault_log_feeder_addresses[MAX_POWER_LINE_COUNT] = {
	FAULT_LOG_FEEDER_1_ADDRESS,
	FAULT_LOG_FEEDER_2_ADDRESS,
	FAULT_LOG_FEEDER_3_ADDRESS,
	FAULT_LOG_FEEDER_4_ADDRESS,
	FAULT_LOG_FEEDER_5_ADDRESS,
	FAULT_LOG_FEEDER_6_ADDRESS,
	FAULT_LOG_FEEDER_7_ADDRESS,
	FAULT_LOG_FEEDER_8_ADDRESS
};

typedef struct
{
	fault_log_t temporary_fault_log[PHASE_MAX][FAULT_LOG_COUNT];
	fault_log_t permanent_fault_log[PHASE_MAX][FAULT_LOG_COUNT];
	uint8_t temporary_fault_log_index[PHASE_MAX];
	uint8_t permanent_fault_log_index[PHASE_MAX];
	uint32_t total_temporary_faults[PHASE_MAX];
	uint32_t total_permanent_faults[PHASE_MAX];
	crc32_t crc; 
}fault_log_feeder_history_t;


static fault_log_feeder_history_t g_feeder_log;
static int8_t g_current_feeder = -1;    /* index of the feeder in g_feeder_log, -1 = none */
static bool   g_feeder_dirty   = false; /* true when g_feeder_log has unsaved changes */

/* Forward declarations */
static crc32_t calculate_crc(const void *data, uint32_t len);

/* Flush the in-RAM feeder to flash (primary + backup). */
static int fault_log_sync_current(void)
{
	if(g_current_feeder < 0) {
		return 0;
	}

	crc32_t crc = calculate_crc(&g_feeder_log, sizeof(g_feeder_log));

	if(crc == g_feeder_log.crc && !g_feeder_dirty)
	{
		CCSLOG(XCOLOR_YELLOW, "Feeder %d: already up to date, skipping.\r\n", g_current_feeder + 1);
		return 0;
	}

	g_feeder_log.crc = crc;

	uint32_t primary_addr = fault_log_feeder_addresses[g_current_feeder];
	uint32_t backup_addr  = FAULT_LOG_BACKUP_ADDRESS + (uint32_t)g_current_feeder * 4096U;

	int res1 = w25qxx_write_buff(primary_addr, &g_feeder_log, sizeof(g_feeder_log));
	int res2 = w25qxx_write_buff(backup_addr,  &g_feeder_log, sizeof(g_feeder_log));

	if(res1 != 0 || res2 != 0)
	{
		CCSLOG(XCOLOR_RED, "Feeder %d: sync error. Res1=%d Res2=%d\r\n", g_current_feeder + 1, res1, res2);
	}
	else
	{
		CCSLOG(XCOLOR_GREEN, "Feeder %d: fault log synced.\r\n", g_current_feeder + 1);
		g_feeder_dirty = false;
	}

	return res1 | res2;
}

/*
 * Load feeder_id into g_feeder_log.
 * If a different dirty feeder is in RAM it is flushed first.
 * On CRC failure the backup is tried; if that also fails the log is cleared.
 */
static void fault_log_load_feeder(uint8_t feeder_id)
{
	if(g_current_feeder == (int8_t)feeder_id){ 
		return;
	}

	if(g_feeder_dirty){
		fault_log_sync_current();
	}

	uint32_t primary_addr = fault_log_feeder_addresses[feeder_id];
	uint32_t backup_addr  = FAULT_LOG_BACKUP_ADDRESS + (uint32_t)feeder_id * 4096U;

	w25qxx_read_buff(primary_addr, &g_feeder_log, sizeof(g_feeder_log));
	crc32_t crc = calculate_crc(&g_feeder_log, sizeof(g_feeder_log));

	if(crc == g_feeder_log.crc)
	{
		g_current_feeder = (int8_t)feeder_id;
		g_feeder_dirty   = false;
		return;
	}

	CCSLOG(XCOLOR_RED, "Feeder %d: CRC mismatch, trying backup.\r\n", feeder_id + 1);

	w25qxx_read_buff(backup_addr, &g_feeder_log, sizeof(g_feeder_log));
	crc = calculate_crc(&g_feeder_log, sizeof(g_feeder_log));

	if(crc == g_feeder_log.crc)
	{
		CCSLOG(XCOLOR_CYAN, "Feeder %d: recovered from backup.\r\n", feeder_id + 1);
	}
	else
	{
		CCSLOG(XCOLOR_RED, "Feeder %d: backup CRC mismatch \xe2\x80\x94 clearing log.\r\n", feeder_id + 1);
		memset(&g_feeder_log, 0, sizeof(g_feeder_log));
	}

	g_current_feeder = (int8_t)feeder_id;
	g_feeder_dirty   = true; /* must be written back */
}

static void print_log(int entry_num, const fault_log_t *log)
{
	CCSLOG_NODT(XCOLOR_CYAN, "  [%2d] %02u-%02u-%04u %02u:%02u:%02u  I=%.1fA  T=%ums  Nominal=%s  Power=%s\r\n",
			entry_num,
			log->tm.day, log->tm.month, (uint32_t)(log->tm.year + 2000),
			log->tm.hour, log->tm.minute, cp56time2a_get_second(&log->tm),
			log->fault_current / 10.0f,
			log->fault_duration_ms,
			log->info.nominal_current_status ? "Below" : "Normal",
			log->info.power_status ? "On" : "Off");
}

void fault_log_dump(void)
{
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		fault_log_load_feeder((uint8_t)feeder);
		const fault_log_feeder_history_t *fh = &g_feeder_log;

		CSLOG("\r\n=== Feeder %d ===\r\n", feeder + 1);

		for(int phase = 0; phase < PHASE_MAX; phase++)
		{
			uint32_t total_temp = fh->total_temporary_faults[phase];
			uint32_t temp_count = total_temp > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total_temp;

			CSLOG("  Phase %d — TEMPORARY FAULTS (total: %u, showing: %u, newest first):\r\n",
					phase + 1, total_temp, temp_count);

			if(temp_count == 0)
			{
				CSLOG("  (none)\r\n");
			}
			else
			{
				uint8_t write_idx = fh->temporary_fault_log_index[phase];
				for(uint32_t i = 0; i < temp_count; i++)
				{
					uint8_t idx = (uint8_t)((write_idx - 1u - i + FAULT_LOG_COUNT) % FAULT_LOG_COUNT); // Show newest first
					print_log((int)(i + 1), &fh->temporary_fault_log[phase][idx]);
				}
			}

			uint32_t total_perm = fh->total_permanent_faults[phase];
			uint32_t perm_count = total_perm > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total_perm;

			CSLOG("  Phase %d — PERMANENT FAULTS (total: %u, showing: %u, newest first):\r\n",
					phase + 1, total_perm, perm_count);

			if(perm_count == 0)
			{
				CSLOG("  (none)\r\n");
			}
			else
			{
				uint8_t perm_write_idx = fh->permanent_fault_log_index[phase];
				for(uint32_t i = 0; i < perm_count; i++)
				{
					uint8_t idx = (uint8_t)((perm_write_idx - 1u - i + FAULT_LOG_COUNT) % FAULT_LOG_COUNT);
					print_log((int)(i + 1), &fh->permanent_fault_log[phase][idx]);
				}
			}
		}
		CSLOG("\r\n");
	}
}


static void test_fault_log_add_random(void)
{
	/* Values are encoded so they are self-identifying when read back:
	 *   fault_current (A)  = (feeder+1)*100 + (phase+1)*10 + (i+1)
	 *     e.g. feeder=0, phase=0, i=0  →  111.0 A
	 *          feeder=1, phase=2, i=3  →  234.0 A
	 *   fault_duration_ms  = (feeder+1)*1000 + (phase+1)*100 + (i+1)*10
	 *     e.g. feeder=0, phase=0, i=0  →  1110 ms
	 *          feeder=1, phase=2, i=3  →  2340 ms
	 *   nominal_current_status / power_status:
	 *     temporary  →  0 / 0
	 *     permanent  →  1 / 1
	 */
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		for(int phase = 0; phase < PHASE_MAX; phase++)
		{
			/* 5 temporary */
			for(int i = 0; i < 5; i++)
			{
				float    fault_current     = (feeder + 1) * 100.0f + (phase + 1) * 10.0f + (i + 1) * 1.0f;
				uint16_t fault_duration_ms = (uint16_t)((feeder + 1) * 1000U + (phase + 1) * 100U + (i + 1) * 10U);

				fault_log_add(fault_current, fault_duration_ms,
						0 /* nominal_current_status */, 0 /* power_status */, 0 /* temporary */,
						(uint8_t)feeder, (uint8_t)phase);
			}

			/* 5 permanent */
			for(int i = 0; i < 5; i++)
			{
				float    fault_current     = (feeder + 1) * 100.0f + (phase + 1) * 10.0f + (i + 1) * 1.0f;
				uint16_t fault_duration_ms = (uint16_t)((feeder + 1) * 1000U + (phase + 1) * 100U + (i + 1) * 10U);

				fault_log_add(fault_current, fault_duration_ms,
						1 /* nominal_current_status */, 1 /* power_status */, 1 /* permanent */,
						(uint8_t)feeder, (uint8_t)phase);
			}
		}
	}

	fault_log_sync();
	CSLOG("Test logs added: %d feeders x %d phases x 5 temp + 5 perm.\r\n",
			MAX_POWER_LINE_COUNT, PHASE_MAX);
}

void fault_log_clear(void)
{
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		g_current_feeder = (int8_t)feeder;
		g_feeder_dirty   = true;
		memset(&g_feeder_log, 0, sizeof(g_feeder_log));
		fault_log_sync_current();
	}
	CSLOG("Fault log cleared.\r\n");
}

static int shell_fltlog_dump(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	if(argc > 1 && strcmp(argv[1], "test") == 0)
	{
		test_fault_log_add_random();
	}
	else if(argc > 1 && strcmp(argv[1], "clear") == 0)
	{
		fault_log_clear();
	}
	else if(argc == 1){
		fault_log_dump();
	}

	return 0;
}

static crc32_t calculate_crc(const void *data, uint32_t len)
{
	if(len < sizeof(crc32_t)){
		CCSLOG(XCOLOR_RED, "Data length too small for CRC calculation: %u bytes\r\n", len);
		return 0;
	}

    crc32_t crc = crc32_init();
    crc = crc32_update(crc, data, (len - sizeof(crc32_t)));
    return crc32_finalize(crc);
}

int fault_log_sync(void)
{
	return fault_log_sync_current();
}

void fault_log_init(void)
{
	CSLOG("sizeof(fault_log_t) = %u bytes\r\n", (unsigned)sizeof(fault_log_t));
	CSLOG("sizeof(fault_log_feeder_history_t) = %u bytes\r\n", (unsigned)sizeof(fault_log_feeder_history_t));

	/* Validate every feeder via fault_log_load_feeder which repairs/clears on
	 * CRC error and marks g_feeder_dirty. Flush immediately so flash is consistent. */
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		g_current_feeder = -1; /* force reload */
		fault_log_load_feeder((uint8_t)feeder);
		if(g_feeder_dirty){
			fault_log_sync_current();
		}
		else{
			CSLOG("Feeder %d: fault log loaded OK.\r\n", feeder + 1);
		}
	}

	//fault_log_dump();

	shell_register_command( &(shell_cmd_t){
		.cmd = "fltlog",
		.desc = "Dump fault logs",
		.func = shell_fltlog_dump}
	);
}

static bool fault_log_add_temporary(uint8_t feeder_id, uint8_t phase_id, const fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	} 

	fault_log_load_feeder(feeder_id);

	uint8_t idx = g_feeder_log.temporary_fault_log_index[phase_id];
	fault_log_t *slot = &g_feeder_log.temporary_fault_log[phase_id][idx];
	*slot = *log;
	slot->crc = calculate_crc(slot, sizeof(fault_log_t));
	g_feeder_log.temporary_fault_log_index[phase_id] = (idx + 1u) % FAULT_LOG_COUNT;
	g_feeder_log.total_temporary_faults[phase_id]++;
	g_feeder_dirty = true;

	return true;
}

static bool fault_log_add_permanent(uint8_t feeder_id, uint8_t phase_id, const fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	} 

	fault_log_load_feeder(feeder_id);

	uint8_t idx = g_feeder_log.permanent_fault_log_index[phase_id];
	fault_log_t *slot = &g_feeder_log.permanent_fault_log[phase_id][idx];
	*slot = *log;
	slot->crc = calculate_crc(slot, sizeof(fault_log_t));
	g_feeder_log.permanent_fault_log_index[phase_id] = (idx + 1u) % FAULT_LOG_COUNT;
	g_feeder_log.total_permanent_faults[phase_id]++;
	g_feeder_dirty = true;

	return true;
}

bool fault_log_add(float fault_current, uint16_t fault_duration_ms, uint8_t nominal_current_status,
		uint8_t power_status, uint8_t type, uint8_t feeder_id, uint8_t phase_id)
{
	cp56time2a_t timestamp = cp56time2a_now();

	fault_log_t new_log = 
	{
		.tm = timestamp,
		.fault_current = fault_current * 10.0,
		.fault_duration_ms = fault_duration_ms,
		.info = {
			.feeder = feeder_id & 0x07,
			.phase = phase_id & 0x03,
			.nominal_current_status = nominal_current_status ? 1 : 0,
			.power_status = power_status ? 1 : 0,
			.type = type ? 1 : 0
		}
	};
	
	if(type){
		return fault_log_add_permanent(feeder_id, phase_id, &new_log);
	}
	else{
		return fault_log_add_temporary(feeder_id, phase_id, &new_log);
	}
}

bool fault_log_add_log(fault_log_t *log)
{
	if(log == NULL){
		return false;
	}

	log->tm = cp56time2a_now();
	if(log->info.type){
		return fault_log_add_permanent(log->info.feeder, log->info.phase, log);
	}
	else{
		return fault_log_add_temporary(log->info.feeder, log->info.phase, log);
	}
}


bool fault_log_read_permanent(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || index >= FAULT_LOG_COUNT || log == NULL){
		return false;
	}

	fault_log_load_feeder(feeder_id);
	const fault_log_t *entry = &g_feeder_log.permanent_fault_log[phase_id][index];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc)
	{
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u Perm[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u, index, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}

bool fault_log_read_temporary(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || index >= FAULT_LOG_COUNT || log == NULL){
		return false;
	}

	fault_log_load_feeder(feeder_id);
	const fault_log_t *entry = &g_feeder_log.temporary_fault_log[phase_id][index];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc)
	{
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u Temp[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u, index, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}

uint8_t fault_log_get_temp_count(uint8_t feeder_id, uint8_t phase_id)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX){
		return 0;
	}
	fault_log_load_feeder(feeder_id);
	uint32_t total = g_feeder_log.total_temporary_faults[phase_id];
	return (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
}

uint8_t fault_log_get_perm_count(uint8_t feeder_id, uint8_t phase_id)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX){
		return 0;
	}
	fault_log_load_feeder(feeder_id);
	uint32_t total = g_feeder_log.total_permanent_faults[phase_id];
	return (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
}

// Bu fonksiyon dongusel kayitlarin icinden index'e gore okuma yapar. 
// 0. index son/guncel kayit, 14. kayit en eski kayit
bool fault_log_read_nth(uint8_t feeder_id, uint8_t phase_id, fault_log_type_t type, uint8_t n, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	}

	fault_log_load_feeder(feeder_id);

	uint32_t total;
	uint8_t write_idx;
	const fault_log_t *array;

	if(type == FAULT_LOG_TYPE_PERMANENT)
	{
		total     = g_feeder_log.total_permanent_faults[phase_id];
		write_idx = g_feeder_log.permanent_fault_log_index[phase_id];
		array     = g_feeder_log.permanent_fault_log[phase_id];
	} else {
		total     = g_feeder_log.total_temporary_faults[phase_id];
		write_idx = g_feeder_log.temporary_fault_log_index[phase_id];
		array     = g_feeder_log.temporary_fault_log[phase_id];
	}

	uint8_t count = (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
	if(n >= count){
		return false;
	}

	uint8_t slot = (uint8_t)((write_idx - 1u - n + FAULT_LOG_COUNT) % FAULT_LOG_COUNT);
	const fault_log_t *entry = &array[slot];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc){
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u %s[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u,
				type == FAULT_LOG_TYPE_PERMANENT ? "Perm" : "Temp",
				n, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}
