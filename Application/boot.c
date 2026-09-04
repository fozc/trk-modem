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
        s_installed_fw = sb.installed_fw;
        s_installed_fw_valid = true;
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
        s_installed_fw = sb.installed_fw;
        s_installed_fw_valid = true;
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

bool boot_get_installed_fw_info(fw_info_t *out)
{
    if (out == NULL)
    {
        return false;
    }

    if (!s_installed_fw_valid)
    {
        memset(out, 0, sizeof(*out));
        return false;
    }

    *out = s_installed_fw;
    return true;
}

void boot_installed_hash_to_str(char *out, uint32_t out_size)
{
    uint32_t i = 0U;

    if ((out == NULL) || (out_size == 0U))
    {
        return;
    }

    if (s_installed_fw_valid)
    {
        /* short_commit_hash: 8 bayt ASCII, NUL garantisi yok (bin2efw
         * sifir ile doldurur) -> ilk NUL'e kadar kopyala. */
        while ((i < 8U) && (i < (out_size - 1U)) &&
               (s_installed_fw.short_commit_hash[i] != 0U))
        {
            out[i] = (char)s_installed_fw.short_commit_hash[i];
            i++;
        }
    }

    if (i == 0U)
    {
        /* Okunamadi ya da bos: gorunur bilinmez isareti. */
        const char unknown[] = "-----";
        uint32_t n = sizeof(unknown) - 1U;

        if (n > (out_size - 1U))
        {
            n = out_size - 1U;
        }
        for (uint32_t j = 0U; j < n; j++)
        {
            out[j] = unknown[j];
        }
        i = n;
    }

    out[i] = '\0';
}

void boot_log_installed_fw(void)
{
    char hash_str[9];

    boot_installed_hash_to_str(hash_str, sizeof(hash_str));
    CSLOG("Git Commit: [%s]\n", hash_str);

    if (!s_installed_fw_valid)
    {
        return;    /* Superblock yok: hash ----- ile basildi, gerisi yok */
    }

    CSLOG("Image Build: [%04u-%02u-%02u %02u:%02u:%02u]\n",
          (unsigned)s_installed_fw.year, (unsigned)s_installed_fw.month,
          (unsigned)s_installed_fw.day, (unsigned)s_installed_fw.hour,
          (unsigned)s_installed_fw.minute, (unsigned)s_installed_fw.second);

    if (s_installed_fw.installation_date != 0U)
    {
        datetime_t dt = {0};

        dt_conv_from_epoch((time32_t)s_installed_fw.installation_date, &dt);
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
