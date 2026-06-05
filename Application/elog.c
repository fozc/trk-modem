/*
 * elog.c
 *
 *  Created on: 14 Eyl 2025
 *      Author: fatih
 */
#include "elog.h"
#include <stdio.h>
#include <string.h>
#include "time_service.h"
#include "crc32.h"
#include "bsp.h"
#include "shell.h"
#include "xprintf.h"
#include "rtc.h"
#include "datetime.h"

typedef enum
{
	ELOG_MAIN_AREA = 0,
	ELOG_BACKUP_AREA
}elog_memory_area_t;

static elog_io_cfg_t log_io_cfg = {0};
static elog_metadata_t log_metadata = {0};
static bool is_initialized = false;

static int elog_read(uint32_t addr, void *buf, uint32_t len)
{
    log_io_cfg.storage_if.read(addr, buf, len);

    return 0;
}

static int elog_write(uint32_t addr, const void *buf, uint32_t len)
{
    if (addr + log_io_cfg.size <= addr + len) {
        return -1; // Out of bounds
    }
    return log_io_cfg.storage_if.write(addr, buf, len);
}

static crc32_t elog_calculate_metadata_crc(void)
{
    crc32_t crc = crc32_init();
    crc = crc32_update(crc, &log_metadata, (sizeof(log_metadata) - sizeof(log_metadata.crc)));
    return crc32_finalize(crc);
}

static bool is_elog_empty_or_uninitialized(void)
{
	const uint8_t* elog_ptr = (const uint8_t*)&log_metadata;
	for(int i = 0; i < sizeof(elog_metadata_t); i++){
		if(elog_ptr[i] != 0xFF){
			return 0;
		}
	}
	return 1; // Empty
}

static void elog_write_metadata(elog_memory_area_t area)
{
    log_metadata.crc = elog_calculate_metadata_crc();

    uint32_t addr = area == ELOG_MAIN_AREA ? log_io_cfg.super_block_addr : log_io_cfg.super_block_backup_paddr;
    elog_write(addr, &log_metadata, sizeof(log_metadata));
}

static void elog_write_log_entry(const elog_entry_t *entry, bool backup)
{
	if(entry == NULL){
		return;
	}

	uint32_t log_index = log_metadata.total_entries % log_metadata.max_entries;
	uint32_t write_offset = (log_index * sizeof(elog_entry_t));

	log_metadata.total_entries++;

	elog_write((log_io_cfg.log_block_addr + write_offset), entry, sizeof(elog_entry_t));
	elog_write_metadata(ELOG_MAIN_AREA);

	if(backup){
		elog_write((log_io_cfg.log_block_backup_addr + write_offset), entry, sizeof(elog_entry_t));
		elog_write_metadata(ELOG_BACKUP_AREA);
	}
}

void elog_init(const elog_io_cfg_t *io_cfg)
{
    if(io_cfg)
    {
        log_io_cfg = *io_cfg;
        
        if(elog_read(io_cfg->super_block_addr, &log_metadata, sizeof(log_metadata))){
        	CSLOG("Error reading error log metadata from storage.\r\n");
        }
    	crc32_t calculated_crc = elog_calculate_metadata_crc();
        if(calculated_crc != log_metadata.crc)
        {
        	bool is_main_elog_empty_or_uninitialized = is_elog_empty_or_uninitialized();
            CSLOG("Error log metadata CRC mismatch. Recovering from backup...\r\n");

            if(elog_read(io_cfg->super_block_backup_paddr, &log_metadata, sizeof(log_metadata))){
            	CSLOG("Error reading error log backup metadata from storage.\r\n");
            }

            calculated_crc = elog_calculate_metadata_crc();

            if(calculated_crc != log_metadata.crc)
            {
            	bool is_backup_elog_empty_or_uninitialized = is_elog_empty_or_uninitialized();
            	CSLOG("Error log backup metadata CRC mismatch. Reinitializing log storage...\r\n");
            	if(is_main_elog_empty_or_uninitialized && is_backup_elog_empty_or_uninitialized){
					CSLOG("Error log storage uninitialized.\r\n");
				}
				else{
					CSLOG("Both main and backup error log metadata are corrupted.\r\n");
				}

    			memset(&log_metadata, 0, sizeof(log_metadata));

    			log_metadata.total_entries = 0;
    			log_metadata.max_entries = log_io_cfg.size / sizeof(elog_entry_t);
    			log_metadata.size = log_io_cfg.size;
    			log_metadata.log_addr = log_io_cfg.log_block_addr;
    			log_metadata.log_backup_addr = log_io_cfg.log_block_backup_addr;

    			elog_write_metadata(ELOG_MAIN_AREA);
    			elog_write_metadata(ELOG_BACKUP_AREA);

    			CSLOG("Error log metadata reinitialized.\r\n");
            }
            else
            {
				CSLOG("Error log metadata recovered from backup.\r\n");
				elog_write_metadata(ELOG_MAIN_AREA); // Restore main from backup
            }
        }

        is_initialized = true;

        CSLOG("Error log system initialized.\r\n");
        CSLOG("Max entries: %u, Current entries: %u\r\n", log_metadata.max_entries, log_metadata.total_entries);
    } else {
        CSLOG("Error log system initialization failed - NULL config.\r\n");
    }
}

void elog_add(elog_code_t _code, elog_level_t level, const void *data, size_t data_len)
{
    // Check if storage is properly initialized
    if (!is_initialized || log_metadata.max_entries == 0) {
        return; // Cannot add entries to zero-size storage
    }

    elog_entry_t entry = {};

    rtc_t dt = rtc_now();
    datetime_t     dt2 = {
		.date.day = dt.day,
		.date.month = dt.month,
		.date.year = 2000U + (uint16_t)dt.year,
		.time.hour = dt.hour,
		.time.minute = dt.minute,
		.time.second = dt.second
	};

    entry.timestamp = dt_conv_to_epoch(&dt2);

    entry.entry_id = log_metadata.total_entries;
    entry.level = (uint8_t)level;
    entry.code = (uint8_t)_code;
    memcpy(entry.info, data, data_len < sizeof(entry.info) ? data_len : sizeof(entry.info));

    elog_write_log_entry(&entry, true);
}

void elog_add_entry(const elog_entry_t *entry)
{
	if (!is_initialized || entry == NULL || log_metadata.max_entries == 0) {
		return; // Invalid entry pointer or uninitialized storage
	}

	elog_write_log_entry(entry, true);
}


int elog_read_entry(uint32_t index, elog_entry_t *entry)
{
    if (!is_initialized || entry == NULL || index >= log_metadata.max_entries) {
        return 1; // Invalid entry pointer or ID
    }

    uint32_t read_addr = log_io_cfg.log_block_addr + (index * sizeof(elog_entry_t));

    elog_read(read_addr, entry, sizeof(elog_entry_t));

    return 0;
}

void elog_print(elog_level_t level, const char *message)
{
    const char *level_str = "";
    switch(level) {
        case ELOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case ELOG_LEVEL_INFO:  level_str = "INFO";  break;
        case ELOG_LEVEL_WARN:  level_str = "WARN";  break;
        case ELOG_LEVEL_ERROR: level_str = "ERROR"; break;
        case ELOG_LEVEL_FATAL: level_str = "FATAL"; break;
        default: level_str = "UNKNOWN"; break;
    }
    CSLOG("[%s] %s\r\n", level_str, message);
}


void elog_test()
{
	elog_entry_t entry = {0};

	uint32_t start_time = millis();
	for(int i = 0; i < log_metadata.max_entries; i++)
	{
		char test_data[sizeof(entry.info)];
		xsprintf(test_data, "%04d - Test Log", i + 1);

		uint32_t t1 = millis();
		elog_add(ELOG_CODE_OK, ELOG_LEVEL_INFO, test_data, strlen(test_data)+1);
		uint32_t t2 = millis();
		CSLOG("Log entry %d added in %u ms\r\n", i + 1, t2 - t1);
	}
	uint32_t end_time = millis();

	CSLOG("Added %d log entries in %u ms (avg %u ms/entry)\r\n", log_metadata.max_entries, end_time - start_time, (end_time - start_time) / log_metadata.max_entries);

	for(int i = 0; i < log_metadata.max_entries; i++)
	{
		char test_data[sizeof(entry.info)];
		xsprintf(test_data, "%04d - Test Log", i + 1);

		memset(&entry, 0, sizeof(entry));
		elog_read_entry(i, &entry);

		//test_data[sizeof(entry.info) - 1] = '\0'; // Ensure null-termination
		//entry.info[sizeof(entry.info) - 1] = '\0'; // Ensure null-termination

		CSLOG("Read log entry [%d] ID=[%d] TimeStamp[%d] Info=[%s]\r\n", i + 1, entry.entry_id, entry.timestamp, entry.info);
		if(entry.entry_id != i){
			CSLOG("Log entry ID mismatch at index %d: expected %d, got %d\r\n", i, i, entry.entry_id);
		}

		if(memcmp(test_data, entry.info, strlen(test_data))){
			CSLOG("Log entry mismatch at index %d: expected '%s', got '%s'\r\n", i, test_data, entry.info);
		}
	}

	for(int i = 0; i < log_metadata.max_entries; i++)
	{
		char test_data[sizeof(entry.info)];
		xsprintf(test_data, "%04d - Test Log", log_metadata.max_entries + i + 1);
		elog_add(ELOG_CODE_OK, ELOG_LEVEL_INFO, test_data, strlen(test_data)+1);
	}


	for(int i = 0; i < log_metadata.max_entries; i++)
	{
		char test_data[sizeof(entry.info)];
		xsprintf(test_data, "%04d - Test Log", log_metadata.max_entries + i + 1);

		memset(&entry, 0, sizeof(entry));
		elog_read_entry(i, &entry);

		CSLOG("Read log entry [%d] ID=[%d] TimeStamp[%d] Info=[%s]\r\n", log_metadata.max_entries + i + 1, entry.entry_id, entry.timestamp, entry.info);
		if(entry.entry_id != log_metadata.max_entries + i){
			CSLOG("Log entry ID mismatch at index %d: expected %d, got %d\r\n", i, i, entry.entry_id);
		}

		if(memcmp(test_data, entry.info, strlen(test_data))){
			CSLOG("Log entry mismatch at index %d: expected '%s', got '%s'\r\n", i, test_data, entry.info);
		}
	}

}

/* ========== Public API Functions ========== */

void elog_clear(void)
{
    if (!is_initialized) {
        CSLOG("Error: elog not initialized\r\n");
        return;
    }
    
    log_metadata.total_entries = 0;
    
    elog_write_metadata(ELOG_MAIN_AREA);
    elog_write_metadata(ELOG_BACKUP_AREA);
    
    CSLOG("Error log cleared. All entries removed.\r\n");
}

uint16_t elog_get_entry_count(void)
{
    return is_initialized ? log_metadata.total_entries : 0;
}

uint16_t elog_get_max_entries(void)
{
    return is_initialized ? log_metadata.max_entries : 0;
}

/* ========== Shell Command Handlers ========== */

static const char* elog_level_to_string(elog_level_t level)
{
    switch(level) {
        case ELOG_LEVEL_DEBUG: return "DEBUG";
        case ELOG_LEVEL_INFO:  return "INFO ";
        case ELOG_LEVEL_WARN:  return "WARN ";
        case ELOG_LEVEL_ERROR: return "ERROR";
        case ELOG_LEVEL_FATAL: return "FATAL";
        default: return "?????";
    }
}

static void elog_shell_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    if (!is_initialized) {
        SHELL_LOG("Error Log: NOT INITIALIZED\r\n");
        return;
    }
    
    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("       ERROR LOG SYSTEM STATUS\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Status           : %s\r\n", is_initialized ? "INITIALIZED" : "NOT INITIALIZED");
    SHELL_LOG("Total Entries    : %u / %u\r\n", log_metadata.total_entries, log_metadata.max_entries);
    SHELL_LOG("Storage Size     : %u bytes\r\n", log_metadata.size);
    SHELL_LOG("Entry Size       : %u bytes\r\n", (uint32_t)sizeof(elog_entry_t));
    SHELL_LOG("Usage            : %.1f%%\r\n", 
            (float)(log_metadata.total_entries % log_metadata.max_entries) * 100.0f / log_metadata.max_entries);
    
    uint16_t displayed_entries = log_metadata.total_entries;
    if (displayed_entries > log_metadata.max_entries) {
        displayed_entries = log_metadata.max_entries;
    }
    SHELL_LOG("Displayed Entries: %u (oldest entries overwritten)\r\n", displayed_entries);
    
    SHELL_LOG("\r\nMemory Addresses:\r\n");
    SHELL_LOG("  Superblock     : 0x%08X\r\n", log_io_cfg.super_block_addr);
    SHELL_LOG("  Superblock BKP : 0x%08X\r\n", log_io_cfg.super_block_backup_paddr);
    SHELL_LOG("  Log Area       : 0x%08X\r\n", log_io_cfg.log_block_addr);
    SHELL_LOG("  Log Area BKP   : 0x%08X\r\n", log_io_cfg.log_block_backup_addr);
    
    SHELL_LOG("\r\nMetadata CRC     : 0x%08X\r\n", log_metadata.crc);
    SHELL_LOG("Calculated CRC   : 0x%08X\r\n", elog_calculate_metadata_crc());
    
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("\r\nCommands:\r\n");
    SHELL_LOG("  elog          - Show this information\r\n");
    SHELL_LOG("  elog dump     - Dump all log entries\r\n");
    SHELL_LOG("  elog clear    - Clear all log entries\r\n");
    SHELL_LOG("========================================\r\n\r\n");
}

static void elog_shell_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    if (!is_initialized) {
        SHELL_LOG("Error: elog not initialized\r\n");
        return;
    }
    
    uint16_t total_to_display = log_metadata.total_entries;
    uint16_t start_index = 0;
    
    /* If we've wrapped around, only show the last max_entries */
    if (total_to_display > log_metadata.max_entries) {
        start_index = total_to_display % log_metadata.max_entries;
        total_to_display = log_metadata.max_entries;
    }
    
    SHELL_LOG("\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("        ERROR LOG DUMP\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Showing %u entries (ID %u-%u)\r\n\r\n", 
            total_to_display,
            log_metadata.total_entries - total_to_display,
            log_metadata.total_entries - 1);
    
    SHELL_LOG("%-4s %-5s %-5s %-19s %-24s %-48s\r\n",
              "ID", "Idx", "Level", "Timestamp", "Code", "Info (Hex)");
    SHELL_LOG("---- ----- ----- ------------------- ------------------------ ------------------------------------------------\r\n");
    
    for (uint16_t i = 0; i < total_to_display; i++) {
        uint16_t actual_index = (start_index + i) % log_metadata.max_entries;
        elog_entry_t entry;
        
        if (elog_read_entry(actual_index, &entry) == 0) {
            /* Convert epoch timestamp to human-readable */
            datetime_t dt;
            dt_conv_from_epoch(entry.timestamp, &dt);

            /* Print info as hex bytes */
            char hex_str[64];
            int pos = 0;
            for (uint8_t j = 0; j < sizeof(entry.info) && pos < (int)sizeof(hex_str) - 3; j++) {
                pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X ", entry.info[j]);
            }
            if (pos > 0) hex_str[pos - 1] = '\0'; /* Remove trailing space */
            
            SHELL_LOG("%-4u %-5u %-5s %04u-%02u-%02u %02u:%02u:%02u %-24s %s\r\n",
                    entry.entry_id,
                    actual_index,
                    elog_level_to_string(entry.level),
                    dt.date.year, dt.date.month, dt.date.day,
                    dt.time.hour, dt.time.minute, dt.time.second,
                    elog_code_to_string((elog_code_t)entry.code),
                    hex_str);
        }
    }
    
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("Total: %u entries\r\n", total_to_display);
    SHELL_LOG("========================================\r\n\r\n");
}

static void elog_shell_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    if (!is_initialized) {
        SHELL_LOG("Error: elog not initialized\r\n");
        return;
    }
    
    SHELL_LOG("Are you sure you want to clear ALL log entries? (y/n): ");
    /* Note: In a real implementation, you'd wait for user confirmation here.
     * For now, we'll just clear directly. Add confirmation logic if needed. */
    
    uint16_t count = log_metadata.total_entries;
    elog_clear();
    SHELL_LOG("\r\nCleared %u log entries.\r\n", count);
}

static int elog_shell_command(int argc, char **argv)
{
    if (argc < 2) {
        /* No arguments - show info */
        elog_shell_info(argc, argv);
        return 1;
    }
    
    /* Parse subcommand */
    if (strcmp(argv[1], "dump") == 0) {
        elog_shell_dump(argc, argv);
    }
    else if (strcmp(argv[1], "clear") == 0) {
        elog_shell_clear(argc, argv);
    }
    else if (strcmp(argv[1], "info") == 0) {
        elog_shell_info(argc, argv);
    }
    else {
        SHELL_LOG("Unknown elog command: %s\r\n", argv[1]);
        SHELL_LOG("Usage: elog [dump|clear|info]\r\n");
    }

    return 1;
}

void elog_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "elog",
        .desc = "Error log management (dump/clear/info)",
        .level = 0,
        .func = elog_shell_command
    });
}

void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area)
{
    uint8_t info[16] = {0};

    info[0] = (uint8_t)((ip >> 24U) & 0xFFU);
    info[1] = (uint8_t)((ip >> 16U) & 0xFFU);
    info[2] = (uint8_t)((ip >>  8U) & 0xFFU);
    info[3] = (uint8_t)( ip         & 0xFFU);

    info[4] = (uint8_t)source;

    if (p_area != NULL)
    {
        size_t len = strlen(p_area);
        if (len > 11U)
        {
            len = 11U;
        }
        memcpy(&info[5], p_area, len);
    }

    elog_add(code, ELOG_LEVEL_INFO, info, sizeof(info));

    const char *src_str = (source == ELOG_SOURCE_WEB) ? "web" : "serial";
    CSLOG_WARN("[ELOG] Config changed: %s src:%s ip:%u.%u.%u.%u\r\n",
            p_area ? p_area : "?", src_str,
            (unsigned int)info[0], (unsigned int)info[1],
            (unsigned int)info[2], (unsigned int)info[3]);
}
