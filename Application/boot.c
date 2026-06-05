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
#include "boot.h"
#include "w25qxx.h"
#include "w25qxx_memory_organization.h"
#include "crc32.h"
#include "console_logger.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Module state                                                      */
/* ------------------------------------------------------------------ */

/** Cached backup_section field from the superblock. */
static uint8_t s_backup_section = (uint8_t)BOOT_FW_SECTION_B;

/** Flag set when xmodem download completes successfully. */
static bool s_new_fw_downloaded = false;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

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

bool boot_is_new_firmware_downloaded(void)
{
    return s_new_fw_downloaded;
}

void boot_mark_new_firmware_downloaded(bool downloaded)
{
    s_new_fw_downloaded = downloaded;
}
