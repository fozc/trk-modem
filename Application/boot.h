/*
 * boot.h
 *
 * Application-side boot superblock reader.
 *
 * Provides the download target address/section by reading the bootloader
 * superblock from SPI flash.  All other boot management (verification,
 * installation, recovery, approval) is handled by the bootloader.
 *
 *  Created on: Apr 29, 2026
 *      Author: fatih.ozcan
 */
#ifndef BOOT_H_
#define BOOT_H_

#include <stdint.h>
#include <stdbool.h>
#include "w25qxx_memory_organization.h"

#define BOOTLOADER_SUPERBLOCK_MAGIC  0xB007000B

/* ------------------------------------------------------------------ */
/*  Types                                                             */
/* ------------------------------------------------------------------ */

typedef enum
{
    BOOT_FW_SECTION_A    = 0,
    BOOT_FW_SECTION_B    = 1,
    BOOT_FW_SECTION_NONE = 0xFF
} boot_fw_section_t;

typedef struct
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t extra;
} fw_version_t;

typedef struct
{
    fw_version_t version;
    uint32_t size;
    uint32_t fw_crc;
    uint32_t installation_date;
    uint8_t  short_commit_hash[8];
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  _reserved;
} __attribute__((packed)) fw_info_t;

typedef struct
{
    uint32_t  magic;
    uint8_t   boot_mode;
    uint8_t   boot_fail_count;
    uint8_t   is_recovery_attempted;
    uint8_t   is_approved;
    uint8_t   backup_section;         /* boot_fw_section_t: A=0, B=1 */
    uint8_t   _reserved[3];
    fw_info_t installed_fw;
    fw_info_t backup_fw;
    uint32_t  crc;
} __attribute__((packed)) boot_superblock_t;

_Static_assert(sizeof(fw_version_t) == 4U, "fw_version_t size mismatch");
_Static_assert(sizeof(fw_info_t) == 32U, "fw_info_t size mismatch");
_Static_assert(sizeof(boot_superblock_t) == 80U, "boot_superblock_t size mismatch");

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize boot module -- read superblock and resolve download target.
 *
 * Reads primary superblock, falls back to backup.  If both are invalid,
 * defaults download target to section A.
 */
void boot_init(void);

/**
 * @brief Get the SPI flash section where new firmware should be downloaded.
 *
 * @return BOOT_FW_SECTION_A or BOOT_FW_SECTION_B.
 */
boot_fw_section_t boot_get_download_section(void);

/**
 * @brief Get the SPI flash base address for firmware download.
 *
 * @return Absolute SPI flash address of the target firmware section.
 */
uint32_t boot_get_download_address(void);

/**
 * @brief Check if a new firmware file has been downloaded.
 */
bool boot_is_new_firmware_downloaded(void);

/**
 * @brief Mark whether a new firmware download is complete.
 */
void boot_mark_new_firmware_downloaded(bool downloaded);

#endif /* BOOT_H_ */
