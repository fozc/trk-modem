/**
 * @file test_logs.c
 * @brief Test utilities for log system - dummy data generation and edge case testing
 * 
 * Created on: Feb 21, 2026
 * Author: Fatih Özcan
 *         fatihozcan@gmail.com
 */

#include "test_logs.h"
#include "elog.h"
#include "xprintf.h"
#include "shell.h"
#include "http_handlers.h"
#include "http_response.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * HTTP TEST HANDLER
 * ============================================================================ */

/**
 * @brief Test version - generates dummy system logs without using elog
 */
void test_handle_get_syslogs_json(void)
{
    xprintf("[HTTP] GET /syslogs (TEST MODE) - Generating dummy system logs\r\n");
    
    /* Get TX buffer from handlers module */
    int buf_size = 0;
    char *buf = http_handlers_get_tx_buffer(&buf_size);
    
    /* Get query string */
    const char *query_string = http_handlers_get_query_string();
    
    /* Parse query parameters */
    uint32_t offset = 0;
    uint32_t limit = 30;  /* Default: 30 records per page */
    
    if (query_string) {
        const char *offset_ptr = strstr(query_string, "offset=");
        if (offset_ptr) {
            offset = (uint32_t)strtoul(offset_ptr + 7, NULL, 10);
        }
        
        const char *limit_ptr = strstr(query_string, "limit=");
        if (limit_ptr) {
            limit = (uint32_t)strtoul(limit_ptr + 6, NULL, 10);
            if (limit > 100) limit = 100;  /* Cap at 100 */
        }
    }
    
    /* Dummy data: simulate 150 total entries */
    const uint16_t total_entries = 150;
    
    /* Dummy system messages */
    const char* messages[] = {
        "System started",
        "Config loaded",
        "Network init",
        "GSM connected",
        "Time synced",
        "IEC104 active",
        "Modbus ready",
        "RF init done",
        "Watchdog OK",
        "Battery low",
        "Temp warning",
        "Memory OK",
        "Flash mounted",
        "NTP sync OK",
        "User login",
        "Config saved"
    };
    const uint8_t msg_count = sizeof(messages) / sizeof(messages[0]);
    
    xprintf("[HTTP] System Logs (TEST) - offset=%lu, limit=%lu, total=%u\r\n", 
            offset, limit, total_entries);
    
    /* Sanitize offset */
    if (offset >= total_entries) {
        offset = 0;
    }
    
    /* Calculate actual records to return */
    uint32_t end_idx = offset + limit;
    if (end_idx > total_entries) {
        end_idx = total_entries;
    }
    uint32_t count = end_idx - offset;
    
    /* Build JSON response */
    int pos = 0;
    
    /* Start JSON: {"recs":"..." */
    pos += xsnprintf(buf + pos, buf_size - pos, "{\"recs\":\"");
    
    /* Generate dummy entries */
    for (uint32_t display_idx = offset + 1; display_idx <= end_idx; display_idx++) {
        /* Generate dummy timestamp (starts from 2026-02-21 00:00:00) */
        uint32_t timestamp = 1708506000 + (display_idx * 60);
        
        /* Generate dummy level (cycle through levels) */
        uint8_t level = display_idx % 5;  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL
        
        /* Generate dummy code */
        uint8_t code = display_idx % 10;
        
        /* Select message (cycle through messages) */
        const char* msg = messages[display_idx % msg_count];
        
        /* Format: "#INDEX TS:timestamp LVL:level CODE:code INFO:text\n" */
        pos += xsnprintf(buf + pos, buf_size - pos, 
                        "#%lu TS:%lu LVL:%u CODE:%u INFO:%s",
                        display_idx, timestamp, level, code, msg);
        
        /* Add newline separator (except for last entry) */
        if (display_idx < end_idx) {
            pos += xsnprintf(buf + pos, buf_size - pos, "\\n");
        }
    }
    
    /* Close "recs" field and add total count */
    pos += xsnprintf(buf + pos, buf_size - pos, "\",\"t\":%u", total_entries);
    
    pos += xsnprintf(buf + pos, buf_size - pos, "}");
    
    xprintf("[HTTP] System Logs (TEST) JSON size: %d bytes\r\n", pos);
    
    /* Buffer overflow check */
    if (pos >= buf_size - 1) {
        xcprintf(XCOLOR_RED, "[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n", pos, buf_size);
    }
    
    http_send_json(buf, pos);
}

/* ============================================================================
 * TEST DATA GENERATORS
 * ============================================================================ */

/**
 * @brief Populate logs with dummy fault records
 */
void test_populate_logs(uint16_t count)
{
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("   GENERATING DUMMY FAULT RECORDS\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Target count: %u\r\n", count);
    
    if (count == 0) {
        test_clear_logs();
        return;
    }
    
    uint16_t initial_count = elog_get_entry_count();
    SHELL_LOG("Initial entry count: %u\r\n", initial_count);
    SHELL_LOG("Max entries: %u\r\n", elog_get_max_entries());
    SHELL_LOG("\r\nGenerating %u fault records...\r\n\r\n", count);
    
    for (uint16_t i = 0; i < count; i++) {
        elog_entry_t entry = {0};
        
        /* Timestamp: simulate time progression */
        entry.timestamp = 1708506000 + (i * 60);  // +1 minute per log
        
        /* Entry ID: auto-assigned by elog system */
        
        /* Level: cycle through all levels */
        entry.level = i % 5;  // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL
        
        /* Code: various fault codes */
        entry.code = (i % 10) + 1;  // Codes 1-10
        
        /* Info: hex data representing fault details */
        for (int j = 0; j < 16; j++) {
            entry.info[j] = (uint8_t)((i + j) & 0xFF);
        }
        
        /* Add entry using elog API */
        elog_add_entry(&entry);
        
        /* Progress indicator every 10 entries */
        if ((i + 1) % 10 == 0) {
            SHELL_LOG("  Generated %u/%u...\r\n", i + 1, count);
        }
    }
    
    uint16_t final_count = elog_get_entry_count();
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("Final entry count: %u\r\n", final_count);
    SHELL_LOG("Entries added: %u\r\n", final_count - initial_count);
    
    if (final_count > elog_get_max_entries()) {
        SHELL_LOG("NOTE: Circular buffer wrapped around!\r\n");
        SHELL_LOG("      Oldest %u entries overwritten.\r\n", 
                final_count - elog_get_max_entries());
    }
    
    SHELL_LOG("========================================\r\n\r\n");
}

/**
 * @brief Populate logs with dummy system logs (text-based)
 */
void test_populate_syslogs(uint16_t count)
{
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("    GENERATING DUMMY SYSTEM LOGS\r\n");
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Target count: %u\r\n", count);
    
    if (count == 0) {
        test_clear_logs();
        return;
    }
    
    const char* system_messages[] = {
        "System started",
        "Config loaded",
        "Network init OK",
        "GSM connected",
        "Time synced",
        "IEC104 active",
        "Modbus ready",
        "RF init done",
        "Watchdog reset",
        "Battery low",
        "Temp warning",
        "Memory OK",
        "Flash mounted",
        "NTP sync OK",
        "User login",
        "Config saved"
    };
    const uint8_t msg_count = sizeof(system_messages) / sizeof(system_messages[0]);
    
    uint16_t initial_count = elog_get_entry_count();
    SHELL_LOG("Initial entry count: %u\r\n", initial_count);
    SHELL_LOG("\r\nGenerating %u system logs...\r\n\r\n", count);
    
    for (uint16_t i = 0; i < count; i++) {
        elog_entry_t entry = {0};
        
        /* Timestamp: simulate time progression */
        entry.timestamp = 1708506000 + (i * 30);  // +30 seconds per log
        
        /* Level: mostly INFO with occasional WARN/ERROR */
        if (i % 20 == 0) {
            entry.level = ELOG_LEVEL_ERROR;
        } else if (i % 10 == 0) {
            entry.level = ELOG_LEVEL_WARN;
        } else {
            entry.level = ELOG_LEVEL_INFO;
        }
        
        /* Code: system event codes */
        entry.code = i % 5;
        
        /* Info: text message (16 bytes max, null-terminated) */
        const char* msg = system_messages[i % msg_count];
        strncpy((char*)entry.info, msg, sizeof(entry.info) - 1);
        entry.info[sizeof(entry.info) - 1] = '\0';  // Ensure null termination
        
        /* Add entry */
        elog_add_entry(&entry);
        
        /* Progress indicator */
        if ((i + 1) % 10 == 0) {
            SHELL_LOG("  Generated %u/%u...\r\n", i + 1, count);
        }
    }
    
    uint16_t final_count = elog_get_entry_count();
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("Final entry count: %u\r\n", final_count);
    SHELL_LOG("========================================\r\n\r\n");
}

/**
 * @brief Test edge cases for log system
 */
void test_log_edge_cases(void)
{
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("       TESTING LOG EDGE CASES\r\n");
    SHELL_LOG("========================================\r\n\r\n");
    
    elog_entry_t entry = {0};
    
    /* Test 1: Empty string */
    SHELL_LOG("Test 1: Empty string in info field\r\n");
    entry.timestamp = 1708506000;
    entry.level = ELOG_LEVEL_INFO;
    entry.code = 0;
    memset(entry.info, 0, sizeof(entry.info));
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added empty string entry\r\n\r\n");
    
    /* Test 2: Special characters (JSON escape test) */
    SHELL_LOG("Test 2: Special characters\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_WARN;
    entry.code = 1;
    const char special[] = "Quote\"Slash\\";
    strncpy((char*)entry.info, special, sizeof(entry.info) - 1);
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added: '%s'\r\n\r\n", special);
    
    /* Test 3: Newline character */
    SHELL_LOG("Test 3: Newline character\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_ERROR;
    entry.code = 2;
    const char newline[] = "Line1\nLine2";
    strncpy((char*)entry.info, newline, sizeof(entry.info) - 1);
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added: 'Line1\\nLine2'\r\n\r\n");
    
    /* Test 4: Maximum length (no null termination) */
    SHELL_LOG("Test 4: Maximum length (16 bytes, no null)\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_FATAL;
    entry.code = 3;
    const char maxlen[] = "1234567890123456";  // Exactly 16 bytes
    memcpy(entry.info, maxlen, sizeof(entry.info));
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added: '1234567890123456'\r\n\r\n");
    
    /* Test 5: Non-printable characters */
    SHELL_LOG("Test 5: Non-printable characters\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_DEBUG;
    entry.code = 4;
    for (int i = 0; i < 16; i++) {
        entry.info[i] = i;  // 0x00-0x0F (mostly non-printable)
    }
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added non-printable bytes (0x00-0x0F)\r\n\r\n");
    
    /* Test 6: All 0xFF (uninitialized pattern) */
    SHELL_LOG("Test 6: All 0xFF bytes\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_INFO;
    entry.code = 5;
    memset(entry.info, 0xFF, sizeof(entry.info));
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added all 0xFF pattern\r\n\r\n");
    
    /* Test 7: Unicode-like sequence (UTF-8 simulation) */
    SHELL_LOG("Test 7: High-value bytes (UTF-8 sim)\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_WARN;
    entry.code = 6;
    const uint8_t utf8[] = {0xC3, 0xA7, 0xC4, 0xB1, 0xC5, 0x9F, 'T', 'e', 's', 't', 0, 0, 0, 0, 0, 0};
    memcpy(entry.info, utf8, sizeof(entry.info));
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added UTF-8-like bytes\r\n\r\n");
    
    /* Test 8: Tab and carriage return */
    SHELL_LOG("Test 8: Tab and CR characters\r\n");
    entry.timestamp++;
    entry.level = ELOG_LEVEL_ERROR;
    entry.code = 7;
    const char control[] = "Tab\tCR\rTest";
    strncpy((char*)entry.info, control, sizeof(entry.info) - 1);
    elog_add_entry(&entry);
    SHELL_LOG("  â†’ Added: 'Tab\\tCR\\rTest'\r\n\r\n");
    
    SHELL_LOG("========================================\r\n");
    SHELL_LOG("Edge case testing completed!\r\n");
    SHELL_LOG("Total test entries added: 8\r\n");
    SHELL_LOG("========================================\r\n\r\n");
}

/**
 * @brief Clear all logs for testing
 */
void test_clear_logs(void)
{
    SHELL_LOG("\r\n========================================\r\n");
    SHELL_LOG("         CLEARING ALL LOGS\r\n");
    SHELL_LOG("========================================\r\n");
    
    uint16_t count_before = elog_get_entry_count();
    SHELL_LOG("Entries before clear: %u\r\n", count_before);
    
    elog_clear();
    
    uint16_t count_after = elog_get_entry_count();
    SHELL_LOG("Entries after clear: %u\r\n", count_after);
    SHELL_LOG("========================================\r\n\r\n");
}

/* ============================================================================
 * SHELL COMMANDS
 * ============================================================================ */

static int test_logs_shell_handler(int argc, char **argv)
{
    if (argc < 2) {
        SHELL_LOG("\r\nTest Log Commands:\r\n");
        SHELL_LOG("  testlog fault <count>   - Generate <count> fault records\r\n");
        SHELL_LOG("  testlog sys <count>     - Generate <count> system logs\r\n");
        SHELL_LOG("  testlog edge            - Test edge cases\r\n");
        SHELL_LOG("  testlog clear           - Clear all logs\r\n");
        SHELL_LOG("  testlog scenario        - Run full test scenario\r\n");
        SHELL_LOG("\r\nExample scenarios:\r\n");
        SHELL_LOG("  testlog fault 0         - Clear logs\r\n");
        SHELL_LOG("  testlog fault 50        - Partial buffer (50/100)\r\n");
        SHELL_LOG("  testlog fault 100       - Full buffer (100/100)\r\n");
        SHELL_LOG("  testlog fault 200       - Circular wrap (overwrite)\r\n");
        SHELL_LOG("\r\n");
        return 1;
    }
    
    const char* cmd = argv[1];
    
    if (strcmp(cmd, "fault") == 0) {
        if (argc < 3) {
            SHELL_LOG("Error: Missing count parameter\r\n");
            SHELL_LOG("Usage: testlog fault <count>\r\n");
            return 1;
        }
        uint16_t count = (uint16_t)strtoul(argv[2], NULL, 10);
        test_populate_logs(count);
    }
    else if (strcmp(cmd, "sys") == 0) {
        if (argc < 3) {
            SHELL_LOG("Error: Missing count parameter\r\n");
            SHELL_LOG("Usage: testlog sys <count>\r\n");
            return 1;
        }
        uint16_t count = (uint16_t)strtoul(argv[2], NULL, 10);
        test_populate_syslogs(count);
    }
    else if (strcmp(cmd, "edge") == 0) {
        test_log_edge_cases();
    }
    else if (strcmp(cmd, "clear") == 0) {
        test_clear_logs();
    }
    else if (strcmp(cmd, "scenario") == 0) {
        SHELL_LOG("\r\n");
        SHELL_LOG("â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—\r\n");
        SHELL_LOG("â•‘   FULL TEST SCENARIO EXECUTION         â•‘\r\n");
        SHELL_LOG("â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•\r\n");
        SHELL_LOG("\r\n");
        
        SHELL_LOG("Step 1: Clear all logs\r\n");
        test_clear_logs();
        
        SHELL_LOG("Step 2: Test empty buffer request\r\n");
        SHELL_LOG("  â†’ Web UI should show 0 records\r\n\r\n");
        
        SHELL_LOG("Step 3: Generate 50 fault records (partial)\r\n");
        test_populate_logs(50);
        
        SHELL_LOG("Step 4: Test partial buffer\r\n");
        SHELL_LOG("  â†’ Web UI should paginate 50 records\r\n");
        SHELL_LOG("  â†’ Page 1: #1-30, Page 2: #31-50\r\n\r\n");
        
        SHELL_LOG("Step 5: Generate 50 more (total 100)\r\n");
        test_populate_logs(50);
        
        SHELL_LOG("Step 6: Test full buffer\r\n");
        SHELL_LOG("  â†’ Web UI should show 100 records\r\n");
        SHELL_LOG("  â†’ Page 1-3: Full, Page 4: #91-100\r\n\r\n");
        
        SHELL_LOG("Step 7: Generate 50 more (circular wrap)\r\n");
        test_populate_logs(50);
        
        SHELL_LOG("Step 8: Test circular buffer\r\n");
        SHELL_LOG("  â†’ Oldest 50 entries overwritten\r\n");
        SHELL_LOG("  â†’ Web UI shows entries #51-150 (as #1-100)\r\n\r\n");
        
        SHELL_LOG("Step 9: Test edge cases\r\n");
        test_log_edge_cases();
        
        SHELL_LOG("Step 10: Generate system logs\r\n");
        test_populate_syslogs(30);
        
        SHELL_LOG("\r\n");
        SHELL_LOG("â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—\r\n");
        SHELL_LOG("â•‘   TEST SCENARIO COMPLETE!              â•‘\r\n");
        SHELL_LOG("â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•\r\n");
        SHELL_LOG("\r\n");
        SHELL_LOG("Now test Web UI:\r\n");
        SHELL_LOG("1. Open 'Fault Records' tab â†’ verify pagination\r\n");
        SHELL_LOG("2. Click 'Next Records' â†’ verify append behavior\r\n");
        SHELL_LOG("3. Keep clicking â†’ verify circular reset at end\r\n");
        SHELL_LOG("4. Open 'System Logs' tab â†’ verify text format\r\n");
        SHELL_LOG("5. Check edge cases â†’ verify special char handling\r\n");
        SHELL_LOG("\r\n");
    }
    else {
        SHELL_LOG("Unknown command: %s\r\n", cmd);
        SHELL_LOG("Type 'testlog' for help\r\n");
    }
    
    return 1;
}

/**
 * @brief Register test commands with shell
 */
void test_logs_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "testlog",
        .desc = "Log system testing (fault/sys/edge/clear/scenario)",
        .level = 0,
        .func = test_logs_shell_handler
    });
    
    SHELL_LOG("[TEST] Log test commands registered\r\n");
}
