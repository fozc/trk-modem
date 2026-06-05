/*
 * app_boot_log.c
 *
 * Application-side read-only access to the bootloader audit log.
 * Stateless — each call scans SPI flash directly without requiring
 * boot_log_init() or any bootloader-side state.
 *
 *  Created on: May 09, 2026
 *      Author: fatih.ozcan
 */

#include "app_boot_log.h"

#include "w25qxx.h"
#include "w25qxx_memory_organization.h"
#include "console_logger.h"
#include "utils.h"
#include "shell.h"

/* ------------------------------------------------------------------ */
/*  Local aliases for shared geometry constants                       */
/* ------------------------------------------------------------------ */
#define LOG_SECTOR_SIZE         BOOT_LOG_SECTOR_SIZE
#define LOG_ENTRY_SIZE          BOOT_LOG_ENTRY_SIZE
#define LOG_ENTRIES_PER_SECTOR  BOOT_LOG_ENTRIES_PER_SECTOR
#define LOG_NUM_SECTORS         BOOT_LOG_NUM_SECTORS
#define LOG_TOTAL_ENTRIES       BOOT_LOG_TOTAL_ENTRIES

#define LOG_BASE_ADDR  SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOT_LOG)
#define LOG_END_ADDR   (LOG_BASE_ADDR + (LOG_NUM_SECTORS * LOG_SECTOR_SIZE))

/* ------------------------------------------------------------------ */
/*  Internal: find the write head by scanning for highest sequence    */
/* ------------------------------------------------------------------ */

/**
 * @brief Scan both sectors, find highest-sequence entry.
 *
 * @param[out] p_max_addr   Flash address of the slot AFTER the newest
 *                          entry (i.e. the write head).  Set to
 *                          LOG_BASE_ADDR when no valid entries exist.
 * @param[out] p_count      Total number of valid entries found.
 */
static void scan_log(uint32_t *p_max_addr, uint32_t *p_count)
{
    uint32_t max_seq    = 0U;
    uint32_t max_sector = 0U;
    uint32_t max_slot   = 0U;
    bool     found_any  = false;
    uint32_t valid      = 0U;

    for (uint32_t sec = 0U; sec < LOG_NUM_SECTORS; sec++)
    {
        for (uint32_t slot = 0U; slot < LOG_ENTRIES_PER_SECTOR; slot++)
        {
            boot_log_entry_t entry;
            uint32_t addr = LOG_BASE_ADDR
                          + (sec  * LOG_SECTOR_SIZE)
                          + (slot * LOG_ENTRY_SIZE);

            w25qxx_read_buff(addr, &entry, LOG_ENTRY_SIZE);

            if (boot_log_entry_is_valid(&entry))
            {
                valid++;

                if (entry.sequence >= max_seq)
                {
                    max_seq    = entry.sequence;
                    max_sector = sec;
                    max_slot   = slot;
                    found_any  = true;
                }
            }
        }
    }

    *p_count = valid;

    if (!found_any)
    {
        *p_max_addr = LOG_BASE_ADDR;
    }
    else
    {
        uint32_t next_slot = max_slot + 1U;

        if (next_slot < LOG_ENTRIES_PER_SECTOR)
        {
            *p_max_addr = LOG_BASE_ADDR
                        + (max_sector * LOG_SECTOR_SIZE)
                        + (next_slot  * LOG_ENTRY_SIZE);
        }
        else
        {
            uint32_t other = (max_sector + 1U) % LOG_NUM_SECTORS;
            *p_max_addr = LOG_BASE_ADDR + (other * LOG_SECTOR_SIZE);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

uint32_t app_boot_log_read_last(boot_log_entry_t *p_entries, uint32_t count)
{
    if ((p_entries == NULL) || (count == 0U))
    {
        return 0U;
    }

    uint32_t write_head = 0U;
    uint32_t total      = 0U;
    scan_log(&write_head, &total);

    if (total == 0U)
    {
        return 0U;
    }

    if (count > total)
    {
        count = total;
    }

    uint32_t found      = 0U;
    uint32_t check_addr = write_head;

    for (uint32_t idx = 0U; idx < LOG_TOTAL_ENTRIES; idx++)
    {
        if (found >= count)
        {
            break;
        }

        /* Step backwards by one entry, wrapping around the two sectors. */
        if (check_addr == LOG_BASE_ADDR)
        {
            check_addr = LOG_END_ADDR - LOG_ENTRY_SIZE;
        }
        else
        {
            check_addr -= LOG_ENTRY_SIZE;
        }

        boot_log_entry_t entry;
        w25qxx_read_buff(check_addr, &entry, LOG_ENTRY_SIZE);

        if (boot_log_entry_is_valid(&entry))
        {
            p_entries[found] = entry;
            found++;
        }
    }

    return found;
}

uint32_t app_boot_log_count(void)
{
    uint32_t write_head = 0U;
    uint32_t total      = 0U;
    scan_log(&write_head, &total);

    return total;
}

void app_boot_log_dump(uint32_t count)
{
    uint32_t write_head = 0U;
    uint32_t total      = 0U;
    scan_log(&write_head, &total);

    if (total == 0U)
    {
        CSLOG("Boot log: empty\r\n");
        return;
    }

    if ((count == 0U) || (count > total))
    {
        count = total;
    }

    if (count > 32U)
    {
        count = 32U;
    }

    boot_log_entry_t entries[32];
    uint32_t num = 0U;

    /* Read backwards from write head. */
    uint32_t check_addr = write_head;

    for (uint32_t idx = 0U; idx < LOG_TOTAL_ENTRIES; idx++)
    {
        if (num >= count)
        {
            break;
        }

        if (check_addr == LOG_BASE_ADDR)
        {
            check_addr = LOG_END_ADDR - LOG_ENTRY_SIZE;
        }
        else
        {
            check_addr -= LOG_ENTRY_SIZE;
        }

        boot_log_entry_t entry;
        w25qxx_read_buff(check_addr, &entry, LOG_ENTRY_SIZE);

        if (boot_log_entry_is_valid(&entry))
        {
            entries[num] = entry;
            num++;
        }
    }

    CSLOG("Boot log: %u entries (showing last %u)\r\n", total, num);
    CSLOG("  SEQ    TIMESTAMP     EVENT  DTL  PAYLOAD\r\n");

    for (uint32_t idx = 0U; idx < num; idx++)
    {
        const boot_log_entry_t *p_e = &entries[idx];
        uint32_t ts = p_e->timestamp;

        /* Unpack: YY(6) MM(4) DD(5) hh(5) mm(6) ss(6) */
        uint8_t year   = (uint8_t)((ts >> 26U) & 0x3FU);
        uint8_t month  = (uint8_t)((ts >> 22U) & 0x0FU);
        uint8_t day    = (uint8_t)((ts >> 17U) & 0x1FU);
        uint8_t hour   = (uint8_t)((ts >> 12U) & 0x1FU);
        uint8_t minute = (uint8_t)((ts >> 6U)  & 0x3FU);
        uint8_t second = (uint8_t)(ts & 0x3FU);

        CSLOG("  %-5u  %02u/%02u/%02u %02u:%02u:%02u  0x%02X   0x%02X  0x%04X\r\n",
              p_e->sequence,
              year, month, day, hour, minute, second,
              p_e->event_id, p_e->detail, p_e->payload);
    }
}

/* ------------------------------------------------------------------ */
/*  Shell command                                                     */
/* ------------------------------------------------------------------ */

static int bootlog_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        CSLOG("Usage: bootlog <status|dump [n]>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        uint32_t total = app_boot_log_count();
        CSLOG("[BOOTLOG] === Boot Log Status ===\r\n");
        CSLOG("[BOOTLOG] Flash base : 0x%08X\r\n", LOG_BASE_ADDR);
        CSLOG("[BOOTLOG] Flash end  : 0x%08X\r\n", LOG_END_ADDR);
        CSLOG("[BOOTLOG] Sectors    : %u\r\n", LOG_NUM_SECTORS);
        CSLOG("[BOOTLOG] Entry size : %u bytes\r\n", LOG_ENTRY_SIZE);
        CSLOG("[BOOTLOG] Capacity   : %u entries\r\n", LOG_TOTAL_ENTRIES);
        CSLOG("[BOOTLOG] Used       : %u entries\r\n", total);
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0)
    {
        uint32_t n = 0U;

        if (argc >= 3)
        {
            int val = xstrtoi(argv[2]);
            n = (val > 0) ? (uint32_t)val : 0U;
        }

        app_boot_log_dump(n);
        return 0;
    }

    CSLOG("Unknown argument: %s\r\n", argv[1]);
    return -1;
}

void app_boot_log_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "bootlog",
        .desc  = "Boot log diagnostics\r\n"
                 "\tbootlog status   - show log status & flash info\r\n"
                 "\tbootlog dump [n] - dump last n entries (default: all)",
        .level = SHELL_LVL_USER,
        .func  = bootlog_shell_handler
    });
}

/*** end of file ***/
