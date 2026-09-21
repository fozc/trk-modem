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
#include "spi_flash_organization.h"
#include "utils.h"
#include "xprintf.h"
#include "shell.h"
#include "boot.h"

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
/*  Event decoding (meanings taken from the bootloader write sites)   */
/* ------------------------------------------------------------------ */

/**
 * @brief Stable short name for an event id (boot_log_defs.h order).
 *
 * @param[in] event_id  Raw event_id byte of a log entry.
 *
 * @return NUL-terminated static string, "UNKNOWN" for undefined ids.
 */
static const char *boot_log_event_name(uint8_t event_id)
{
    switch (event_id)
    {
        case BOOT_LOG_FW_INSTALL_OK:        return "FW_INSTALL_OK";
        case BOOT_LOG_FW_INSTALL_FAIL:      return "FW_INSTALL_FAIL";
        case BOOT_LOG_FW_VERIFY_CRC_FAIL:   return "FW_VERIFY_CRC_FAIL";
        case BOOT_LOG_FW_VERIFY_ECDSA_FAIL: return "FW_VERIFY_ECDSA_FAIL";
        case BOOT_LOG_BOOT_ERROR:           return "BOOT_ERROR";
        case BOOT_LOG_RECOVERY_ATTEMPT:     return "RECOVERY_ATTEMPT";
        case BOOT_LOG_RECOVERY_OK:          return "RECOVERY_OK";
        case BOOT_LOG_RECOVERY_FAIL:        return "RECOVERY_FAIL";
        case BOOT_LOG_AUTH_FAIL:            return "AUTH_FAIL";
        case BOOT_LOG_SYSTEM_RESET:         return "SYSTEM_RESET";
        case BOOT_LOG_DOWNGRADE_REJECTED:   return "DOWNGRADE_REJECTED";
        case BOOT_LOG_FW_APPROVED:          return "FW_APPROVED";
        case BOOT_LOG_HARDFAULT:            return "HARDFAULT";
        case BOOT_LOG_INSTALL_INTERRUPTED:  return "INSTALL_INTERRUPTED";
        case BOOT_LOG_TRIAL_NO_START:       return "TRIAL_NO_START";
        case BOOT_LOG_IPC_UPDATE_BIND_FAIL: return "IPC_UPDATE_BIND_FAIL";
        case BOOT_LOG_IPC_APPROVE_BIND_FAIL: return "IPC_APPROVE_BIND_FAIL";
        case BOOT_LOG_FW_VERIFY_REJECT:    return "FW_VERIFY_REJECT";
        case BOOT_LOG_FW_DECODE_FAIL:      return "FW_DECODE_FAIL";
        default:                            return "UNKNOWN";
    }
}

/**
 * @brief One-line human description of an entry (Turkish, ASCII).
 *
 * Payload/detail semantics mirror the bootloader write sites:
 * boot_fw.c (install/verify), boot_main.c (boot error counter, recovery,
 * interrupted install), boot.c (approval -> backup section), and
 * hardfault_handler.c (detail = CFSR low byte, payload = stacked PC).
 * AUTH_FAIL / SYSTEM_RESET / DOWNGRADE_REJECTED are defined but have no
 * writer today; their detail/payload are shown raw.
 *
 * @param[in]  p_e      Entry to describe.
 * @param[out] buf      Destination buffer.
 * @param[in]  buf_len  Size of buf.
 */
static void boot_log_event_desc(const boot_log_entry_t *p_e,
                                char *buf, uint32_t buf_len)
{
    switch (p_e->event_id)
    {
        case BOOT_LOG_FW_INSTALL_OK:
            (void)xsnprintf(buf, buf_len,
                            "firmware kurulumu tamamlandi (CRC+imza dogrulandi)");
            break;
        case BOOT_LOG_FW_INSTALL_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "kurulum basarisiz - tum denemeler tukendi");
            break;
        case BOOT_LOG_FW_VERIFY_CRC_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "SPI imaj CRC dogrulamasi basarisiz");
            break;
        case BOOT_LOG_FW_VERIFY_ECDSA_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "SPI imaj ECDSA imza dogrulamasi basarisiz");
            break;
        case BOOT_LOG_BOOT_ERROR:
            (void)xsnprintf(buf, buf_len,
                            "acilis hatasi (hata sayaci=%u)", p_e->payload);
            break;
        case BOOT_LOG_RECOVERY_ATTEMPT:
            (void)xsnprintf(buf, buf_len,
                            "kurtarma moduna gecis (hata sayisi=%u)", p_e->payload);
            break;
        case BOOT_LOG_RECOVERY_OK:
            (void)xsnprintf(buf, buf_len, "kurtarma basarili");
            break;
        case BOOT_LOG_RECOVERY_FAIL:
            (void)xsnprintf(buf, buf_len, "kurtarma basarisiz");
            break;
        case BOOT_LOG_AUTH_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "yetkilendirme reddedildi (dtl=0x%02X pay=0x%04X)",
                            p_e->detail, p_e->payload);
            break;
        case BOOT_LOG_SYSTEM_RESET:
            (void)xsnprintf(buf, buf_len,
                            "sistem reseti (dtl=0x%02X pay=0x%04X)",
                            p_e->detail, p_e->payload);
            break;
        case BOOT_LOG_DOWNGRADE_REJECTED:
            (void)xsnprintf(buf, buf_len,
                            "surum geriletme reddedildi (dtl=0x%02X pay=0x%04X)",
                            p_e->detail, p_e->payload);
            break;
        case BOOT_LOG_FW_APPROVED:
            (void)xsnprintf(buf, buf_len, "firmware onaylandi, yedek bolum %c",
                            (p_e->payload == (uint16_t)BOOT_FW_SECTION_A) ? 'A' : 'B');
            break;
        case BOOT_LOG_HARDFAULT:
            (void)xsnprintf(buf, buf_len,
                            "hardfault (CFSR_lo=0x%02X PC=0x%04X)",
                            p_e->detail, p_e->payload);
            break;
        case BOOT_LOG_INSTALL_INTERRUPTED:
            (void)xsnprintf(buf, buf_len,
                            "yarida kalan kurulum algilandi, devam ediliyor");
            break;
        case BOOT_LOG_TRIAL_NO_START:
            (void)xsnprintf(buf, buf_len,
                            "trial acilista baslamadi (bolumge=%u)", p_e->payload);
            break;
        case BOOT_LOG_IPC_UPDATE_BIND_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "update IPC baglamasi uyusmadi - kurulum reddedildi (beklenen CRC lo=0x%04X)",
                            p_e->payload);
            break;
        case BOOT_LOG_IPC_APPROVE_BIND_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "onay IPC baglamasi uyusmadi - onay yok sayildi (beklenen CRC lo=0x%04X)",
                            p_e->payload);
            break;
        case BOOT_LOG_FW_VERIFY_REJECT:
            (void)xsnprintf(buf, buf_len,
                            "v2 paket dogrulamada reddedildi (neden=%u)",
                            p_e->detail);
            break;
        case BOOT_LOG_FW_DECODE_FAIL:
            (void)xsnprintf(buf, buf_len,
                            "cozme/on-kontrol basarisiz (neden=%u)",
                            p_e->detail);
            break;
        default:
            (void)xsnprintf(buf, buf_len,
                            "bilinmeyen olay (dtl=0x%02X pay=0x%04X)",
                            p_e->detail, p_e->payload);
            break;
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

void app_boot_log_dump(uint32_t count, bool raw)
{
    uint32_t total = app_boot_log_count();

    if (total == 0U)
    {
        SHELL_LOG("Boot log: empty\r\n");
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
    uint32_t num = app_boot_log_read_last(entries, count);

    SHELL_LOG("Boot log: %u entries (showing last %u)\r\n", total, num);

    if (raw)
    {
        SHELL_LOG("  SEQ    TIMESTAMP     EVENT  DTL  PAYLOAD\r\n");
    }
    else
    {
        SHELL_LOG("  SEQ    TIMESTAMP     EVENT                 DESCRIPTION\r\n");
    }

    for (uint32_t idx = 0U; idx < num; idx++)
    {
        const boot_log_entry_t *p_e = &entries[idx];
        uint32_t ts = p_e->timestamp;

        /* Unpack: YY(6) MM(4) DD(5) hh(5) mm(6) ss(6). YY is printed as
         * stored - the writer packs (full_year & 0x3F), no century info
         * exists in the entry. */
        uint8_t year   = (uint8_t)((ts >> 26U) & 0x3FU);
        uint8_t month  = (uint8_t)((ts >> 22U) & 0x0FU);
        uint8_t day    = (uint8_t)((ts >> 17U) & 0x1FU);
        uint8_t hour   = (uint8_t)((ts >> 12U) & 0x1FU);
        uint8_t minute = (uint8_t)((ts >> 6U)  & 0x3FU);
        uint8_t second = (uint8_t)(ts & 0x3FU);

        if (raw)
        {
            SHELL_LOG("  %-5u  %02u/%02u/%02u %02u:%02u:%02u  0x%02X   0x%02X  0x%04X\r\n",
                      p_e->sequence,
                      year, month, day, hour, minute, second,
                      p_e->event_id, p_e->detail, p_e->payload);
        }
        else
        {
            char desc[64];
            boot_log_event_desc(p_e, desc, (uint32_t)sizeof(desc));

            SHELL_LOG("  %-5u  %02u/%02u/%02u %02u:%02u:%02u  %-20s  %s\r\n",
                      p_e->sequence,
                      year, month, day, hour, minute, second,
                      boot_log_event_name(p_e->event_id), desc);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Shell command                                                     */
/* ------------------------------------------------------------------ */

static int bootlog_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        SHELL_LOG("Usage: bootlog <status|dump [raw] [n]>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        uint32_t total = app_boot_log_count();
        SHELL_LOG("=== Boot Log Status ===\r\n");
        SHELL_LOG("Flash base : 0x%08X\r\n", LOG_BASE_ADDR);
        SHELL_LOG("Flash end  : 0x%08X\r\n", LOG_END_ADDR);
        SHELL_LOG("Sectors    : %u\r\n", LOG_NUM_SECTORS);
        SHELL_LOG("Entry size : %u bytes\r\n", LOG_ENTRY_SIZE);
        SHELL_LOG("Capacity   : %u entries\r\n", LOG_TOTAL_ENTRIES);
        SHELL_LOG("Used       : %u entries\r\n", total);
        return 0;
    }

    if (strcmp(argv[1], "dump") == 0)
    {
        /* bootlog dump [raw] [n] */
        bool raw = ((argc >= 3) && (strcmp(argv[2], "raw") == 0));
        uint32_t n = 0U;

        if (raw && (argc >= 4))
        {
            int val = xstrtoi(argv[3]);
            n = (val > 0) ? (uint32_t)val : 0U;
        }
        else if (!raw && (argc >= 3))
        {
            int val = xstrtoi(argv[2]);
            n = (val > 0) ? (uint32_t)val : 0U;
        }
        else
        {
            /* no count argument */
        }

        app_boot_log_dump(n, raw);
        return 0;
    }

    SHELL_LOG("Unknown argument: %s\r\n", argv[1]);
    return -1;
}

void app_boot_log_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "bootlog",
        .desc  = "Boot log diagnostics\r\n"
                 "\tbootlog status      - show log status & flash info\r\n"
                 "\tbootlog dump [n]    - dump last n entries, decoded (default: all)\r\n"
                 "\tbootlog dump raw [n]- dump last n entries, raw hex values",
        .level = SHELL_LVL_USER,
        .func  = bootlog_shell_handler
    });
}

/*** end of file ***/
