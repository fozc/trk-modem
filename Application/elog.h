/*
 * elog.h
 *
 *  Created on: 14 Eyl 2025
 *      Author: Fatih Özcan
 *              fatihozcan@gmail.com
 */

#ifndef ELOG_H_
#define ELOG_H_

#include <stdint.h>
#include <stddef.h>
#include "elog_codes.h"

typedef enum 
{
    ELOG_LEVEL_DEBUG,
    ELOG_LEVEL_INFO,
    ELOG_LEVEL_WARN,
    ELOG_LEVEL_ERROR,
    ELOG_LEVEL_FATAL
} elog_level_t;
 
typedef struct 
{
    uint32_t timestamp;
    uint16_t entry_id;
    uint8_t level;
    uint8_t code;
    uint8_t info[16];

}__attribute__((packed)) elog_entry_t;

typedef struct 
{
	uint32_t log_addr;        /* Logs start address in flash */
	uint32_t log_backup_addr; /* Logs start address in flash */
    uint32_t size;        	  /* Size of the log area in bytes */
    uint16_t total_entries;   /* Total number of log entries added */
    uint16_t max_entries;     /* Maximum number of log entries */
    uint32_t crc;
}__attribute__((packed)) elog_metadata_t; // Super block structure,

typedef struct {
    void (*read)(uint32_t addr, void *buf, uint32_t len);
    int (*write)(uint32_t addr, const void *buf, uint32_t len);
} elog_storage_if_t;

typedef struct 
{
    elog_storage_if_t storage_if;  
    uint32_t super_block_addr;        /* Address of the super block */
    uint32_t log_block_addr;          /* Start address of the log entries */
    uint32_t super_block_backup_paddr; /* Address of the super block */
    uint32_t log_block_backup_addr;   /* Start address of the log entries */
    uint32_t size;                    /* Size of the log area */
} elog_io_cfg_t;

void elog_init(const elog_io_cfg_t *io_cfg);
void elog_add(elog_code_t _code, elog_level_t level, const void *data, size_t data_len);
void elog_add_entry(const elog_entry_t *entry);
int elog_read_entry(uint32_t index, elog_entry_t *entry);
void elog_print(elog_level_t level, const char *message);
void elog_clear(void);
uint16_t elog_get_entry_count(void);
uint16_t elog_get_max_entries(void);
void elog_shell_init(void);

/**
 * @brief Source of a configuration change.
 */
typedef enum
{
    ELOG_SOURCE_WEB    = 0,
    ELOG_SOURCE_SERIAL = 1
} elog_config_source_t;

/**
 * @brief Log a configuration change event.
 *
 * info payload (16 bytes):
 *   [0..3]  client IP (big-endian, 0 when source is serial)
 *   [4]     source  (elog_config_source_t)
 *   [5..15] area label (null-terminated, truncated if needed)
 *
 * @param[in] code    Elog code identifying the config area.
 * @param[in] source  ELOG_SOURCE_WEB or ELOG_SOURCE_SERIAL.
 * @param[in] ip      Remote client IP (uint32_t, big-endian). Pass 0 for serial.
 * @param[in] p_area  Short area label (e.g. "device", "rf").
 */
void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area);

#endif /* ELOG_H_ */
