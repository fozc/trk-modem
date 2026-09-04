/*
 * boot.c
 *
 * Application-side boot superblock reader.
 *
 * Reads the bootloader superblock (primary or backup) to determine which
 * SPI flash section the new firmware should be downloaded to.  If both
 * copies are invalid, defaults to section A.
 *
 * The application only downloads the file and sends an IPC request to the
 * bootloader.  All verification, installation, and recovery logic resides
 * in the bootloader.
 *
 *  Created on: Apr 29, 2026
 *      Author: fatih.ozcan
 */
#include <spi_flash_organization.h>
#include "boot.h"
#include "w25qxx.h"
#include "crc32.h"
#include "console_logger.h"
#include "datetime.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Module state                                                      */
/* ------------------------------------------------------------------ */

/** Cached backup_section field from the superblock. */
static uint8_t s_backup_section = (uint8_t)BOOT_FW_SECTION_B;

/** Flag set when xmodem download completes successfully. */
static bool s_new_fw_downloaded = false;

/** Cached installed_fw from the last valid superblock read. */
static fw_info_t s_installed_fw = {0};

/** True after boot_init() found a valid (primary or backup) superblock. */
static bool s_installed_fw_valid = false;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Cache installed_fw from a valid superblock.
 *
 * short_commit_hash 8 bayttir ve NUL sonlandirmasi garanti degildir;
 * burada zorlanir (7 karakter hash + NUL), boylece cagiranlar alani
 * dogrudan dizgi olarak kullanabilir.
 */
static void cache_installed_fw(const boot_superblock_t *p_sb)
{
    s_installed_fw = p_sb->installed_fw;
    s_installed_fw.short_commit_hash[7] = 0U;
    s_installed_fw_valid = true;
}

/**
 * @brief Validate a superblock read from SPI flash.
 *
 * @param[in] p_sb  Pointer to the superblock data.
 * @return true if magic and CRC are valid.
 */
static bool is_superblock_valid(const boot_superblock_t *p_sb)
{
    if (p_sb->magic != BOOTLOADER_SUPERBLOCK_MAGIC)
    {
        return false;
    }

    crc32_t crc = crc32_init();
    crc = crc32_update(crc, p_sb, sizeof(*p_sb) - sizeof(p_sb->crc));
    crc = crc32_finalize(crc);

    return (crc == p_sb->crc);
}

/**
 * @brief Extract and validate the backup_section field.
 *
 * @param[in] section  Raw value from the superblock.
 * @return Validated section value (A or B).  Defaults to B if invalid
 *         (so that download target becomes A).
 */
static uint8_t validated_backup_section(uint8_t section)
{
    if ((section == (uint8_t)BOOT_FW_SECTION_A) ||
        (section == (uint8_t)BOOT_FW_SECTION_B))
    {
        return section;
    }

    /* Invalid value -- default so download goes to section A */
    return (uint8_t)BOOT_FW_SECTION_B;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void boot_init(void)
{
    boot_superblock_t sb;

    /* Try primary superblock */
    w25qxx_read_buff(SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOTLOADER_SB),
                     &sb, sizeof(sb));

    if (is_superblock_valid(&sb))
    {
        s_backup_section = validated_backup_section(sb.backup_section);
        cache_installed_fw(&sb);
        CSLOG("BOOT: Primary superblock OK, backup_section=%c\n",
              (s_backup_section == (uint8_t)BOOT_FW_SECTION_A) ? 'A' : 'B');
        return;
    }

    CSLOG("BOOT: Primary superblock invalid, trying backup...\n");

    /* Try backup superblock */
    w25qxx_read_buff(SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOTLOADER_SB_BACKUP),
                     &sb, sizeof(sb));

    if (is_superblock_valid(&sb))
    {
        s_backup_section = validated_backup_section(sb.backup_section);
        cache_installed_fw(&sb);
        CSLOG("BOOT: Backup superblock OK, backup_section=%c\n",
              (s_backup_section == (uint8_t)BOOT_FW_SECTION_A) ? 'A' : 'B');
        return;
    }

    /* Both invalid -- default: download to section A */
    s_backup_section = (uint8_t)BOOT_FW_SECTION_B;
    CSLOG_ERR("BOOT: Both superblocks invalid, defaulting download to section A\n");
}

boot_fw_section_t boot_get_download_section(void)
{
    if (s_backup_section == (uint8_t)BOOT_FW_SECTION_A)
    {
        return BOOT_FW_SECTION_B;
    }

    return BOOT_FW_SECTION_A;
}

uint32_t boot_get_download_address(void)
{
    if (s_backup_section == (uint8_t)BOOT_FW_SECTION_A)
    {
        return SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_B);
    }

    return SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A);
}

const fw_info_t *boot_get_installed_fw_info(void)
{
    if (!s_installed_fw_valid)
    {
    	s_installed_fw =  (fw_info_t){0};   /* Gecerli superblock yok */
    }

    s_installed_fw.short_commit_hash[7] = 0U;

    return &s_installed_fw;
}

void boot_log_installed_fw(void)
{
    const fw_info_t *fw = boot_get_installed_fw_info();

    if (fw == NULL)
    {
        CSLOG("Git Commit: [-----]\n");
        return;
    }

    /* Hash alani cache'e alinirken NUL ile sonlandirilir */
    CSLOG("Git Commit: [%s]\n", (const char *)fw->short_commit_hash);
    CSLOG("Image Build: [%04u-%02u-%02u %02u:%02u:%02u]\n",
          (unsigned)fw->year, (unsigned)fw->month, (unsigned)fw->day,
          (unsigned)fw->hour, (unsigned)fw->minute, (unsigned)fw->second);

    if (fw->installation_date != 0U)
    {
        datetime_t dt = {0};

        dt_conv_from_epoch((time32_t)fw->installation_date, &dt);
        CSLOG("Kurulum: [%04u-%02u-%02u %02u:%02u:%02u]\n",
              (unsigned)dt.date.year, (unsigned)dt.date.month, (unsigned)dt.date.day,
              (unsigned)dt.time.hour, (unsigned)dt.time.minute, (unsigned)dt.time.second);
    }
    else
    {
        /* Bootloader henuz RTC damgasi yazmiyor */
        CSLOG("Kurulum: [-----]\n");
    }
}

bool boot_is_new_firmware_downloaded(void)
{
    return s_new_fw_downloaded;
}

void boot_mark_new_firmware_downloaded(bool downloaded)
{
    s_new_fw_downloaded = downloaded;
}
