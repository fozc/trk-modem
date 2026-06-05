/*
 * fault_log.h
 *
 *  Created on: Feb 21, 2026
 *      Author: fatih
 */

#ifndef FAULT_LOG_H_
#define FAULT_LOG_H_

#include <stdint.h>
#include "types.h"

typedef enum
{
	FAULT_LOG_TYPE_TEMPORARY = 0,
	FAULT_LOG_TYPE_PERMANENT = 1
}fault_log_type_t;


typedef struct
{
    void (*read)(uint32_t addr, void *buf, uint32_t len);
    int (*write)(uint32_t addr, const void *buf, uint32_t len);
}fault_storage_if_t;

typedef struct
{
	fault_storage_if_t storage_if;
    uint32_t super_block_addr;         /* Address of the super block */
    uint32_t log_block_addr;           /* Start address of the log entries */
    uint32_t super_block_backup_paddr; /* Address of the super block */
    uint32_t log_block_backup_addr;    /* Start address of the log entries */
    uint32_t size;                     /* Size of the log area */
}fault_log_io_cfg_t;

typedef struct
{
	cp56time2a_t  tm;           // Timestamp of the fault event
	float    fault_current;
	uint16_t fault_duration_ms;
	struct
	{
		uint8_t feeder : 3;              // 0: Feeder 1, 7: Feeder 8
		uint8_t phase : 2;               // 0: L1, 1: L2, 2: L3
		uint8_t nominal_current_status : 1; // 0: Normal, 1: Below nominal
		uint8_t power_status : 1;           // 0: Off, 1: On
		uint8_t type : 1;                   // 0: Temporary, 1: Permanent
	} __attribute__((packed)) info;
	uint32_t crc;
}__attribute__((packed)) fault_log_t;

void fault_log_init(void);
int fault_log_sync(void);
bool fault_log_add_log(fault_log_t *log);
bool fault_log_add(float fault_current, uint16_t fault_duration_ms, uint8_t nominal_current_status,
		uint8_t power_status, uint8_t type, uint8_t feeder_id, uint8_t phase_id);
bool fault_log_read_permanent(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log);
bool fault_log_read_temporary(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log);

/* Returns the number of stored entries (capped at FAULT_LOG_COUNT, i.e. max 15). */
uint8_t fault_log_get_temp_count(uint8_t feeder_id, uint8_t phase_id);
uint8_t fault_log_get_perm_count(uint8_t feeder_id, uint8_t phase_id);

/* Read the n-th newest entry (n=0 -> newest, n=count-1 -> oldest).
 * Returns false if n is out of range or the entry has a CRC error. */
bool fault_log_read_nth(uint8_t feeder_id, uint8_t phase_id, fault_log_type_t type, uint8_t n, fault_log_t *log);

#endif /* FAULT_LOG_H_ */
