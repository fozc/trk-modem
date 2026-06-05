/*
 * w25qxx_memory_organization.h
 *
 *  Created on: Sep 20, 2025
 *      Author: fatih.ozcan
 */

#ifndef LIBS_W25QXX_MEMORY_ORGANIZATION_H_
#define LIBS_W25QXX_MEMORY_ORGANIZATION_H_

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Flash boundaries                                                  */
/* ------------------------------------------------------------------ */
#define SPI_FLASH_START_ADDR    0x000000UL
#define SPI_FLASH_TOTAL_SIZE    0x200000UL   /* 2 MB */
#define SPI_FLASH_END_ADDR      (SPI_FLASH_START_ADDR + SPI_FLASH_TOTAL_SIZE - 1UL)

/* ------------------------------------------------------------------ */
/*  Layout struct — never instantiated, used only for offsetof/sizeof */
/*                                                                    */
/*  Region                                           Size    Address  */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t SPIFLASH_SECTION_BOOTLOADER_SB                   [0x001000];  /*    4 KB  0x000000 */
    uint8_t SPIFLASH_SECTION_BOOTLOADER_SB_BACKUP            [0x001000];  /*    4 KB  0x001000 */
    uint8_t SPIFLASH_SECTION_BOOT_LOG                        [0x002000];  /*    8 KB  0x002000 */
    uint8_t SPIFLASH_SECTION_IPC                             [0x001000];  /*    4 KB  0x002000 */
    _Alignas(0x10000)
    uint8_t SPIFLASH_SECTION_FIRMWARE_A                      [0x080000];  /*  512 KB  0x010000 */
    _Alignas(0x10000)
    uint8_t SPIFLASH_SECTION_FIRMWARE_B                      [0x080000];  /*  512 KB  0x090000 */
    /* -- Data sections after Firmware B -- START ADDRESS 0x110000 --  */
    uint8_t SPIFLASH_SECTION_NVRAM                           [0x002000];  /*    8 KB  0x110000 */
    uint8_t SPIFLASH_SECTION_NVRAM_BACKUP                    [0x002000];  /*    8 KB  0x112000 */
    uint8_t SPIFLASH_SECTION_ELOG_SUPERBLOCK                 [0x001000];  /*    4 KB  0x114000 */
    uint8_t SPIFLASH_SECTION_ELOG_SUPERBLOCK_BACKUP          [0x001000];  /*    4 KB  0x115000 */
    uint8_t SPIFLASH_SECTION_ELOG_LOGAREA                    [0x001000];  /*    4 KB  0x116000 */
    uint8_t SPIFLASH_SECTION_ELOG_LOGAREA_BACKUP             [0x001000];  /*    4 KB  0x117000 */
    uint8_t SPIFLASH_SECTION_FAULT_LOG_SUPERBLOCK            [0x001000];  /*    4 KB  0x118000 */
    uint8_t SPIFLASH_SECTION_FAULT_LOG_SUPERBLOCK_BACKUP     [0x001000];  /*    4 KB  0x119000 */
    uint8_t SPIFLASH_SECTION_FAULT_LOG                       [0x008000];  /*   32 KB  0x11A000 */
    uint8_t SPIFLASH_SECTION_FAULT_LOG_BACKUP                [0x008000];  /*   32 KB  0x122000 */
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK        [0x001000];  /*    4 KB  0x12A000 */
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK_BACKUP [0x001000];  /*    4 KB  0x12B000 */
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_DATA              [0x002000];  /*    8 KB  0x12C000 */
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_DATA_BACKUP       [0x002000];  /*    8 KB  0x12E000 */
} spi_flash_layout_t;

/* ------------------------------------------------------------------ */
/*  Access macros                                                     */
/*  Usage: SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A)         */
/*         SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FIRMWARE_A)         */
/* ------------------------------------------------------------------ */
#define SPIFLASH_SECTION_ADDR(member)  ((uint32_t)offsetof(spi_flash_layout_t, member))
#define SPIFLASH_SECTION_SIZE(member)  ((uint32_t)sizeof(((spi_flash_layout_t *)0)->member))

/* ------------------------------------------------------------------ */
/*  Backward-compatible aliases                                       */
/* ------------------------------------------------------------------ */

/* Bootloader */
#define BOOTLOADER_SUPERBLOCK_SIZE          SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_BOOTLOADER_SB)
#define BOOTLOADER_SUPERBLOCK_ADDR          SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOTLOADER_SB)
#define BOOTLOADER_SUPERBLOCK_BACKUP_ADDR   SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOTLOADER_SB_BACKUP)

/* NVRAM */
#define NVRAM_FLASH_AREA_SIZE               SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_NVRAM)
#define NVRAM_ADDRESS                       SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_NVRAM)
#define NVRAM_BACKUP_ADDRESS                SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_NVRAM_BACKUP)

/* Event log */
#define ELOG_SUPERBLOCK_SIZE                SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_ELOG_SUPERBLOCK)
#define ELOG_SUPERBLOCK_ADDRESS             SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_ELOG_SUPERBLOCK)
#define ELOG_SUPERBLOCK_BACKUP_ADDRESS      SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_ELOG_SUPERBLOCK_BACKUP)
#define ELOG_LOG_AREA_SIZE                  SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_ELOG_LOGAREA)
#define ELOG_LOGAREA_ADDRESS                SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_ELOG_LOGAREA)
#define ELOG_LOGAREA_BACKUP_ADDRESS         SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_ELOG_LOGAREA_BACKUP)

/* Fault log */
#define FAULT_LOG_SUPERBLOCK_AREA_SIZE      SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FAULT_LOG_SUPERBLOCK)
#define FAULT_LOG_SUPERBLOCK_ADDRESS        SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FAULT_LOG_SUPERBLOCK)
#define FAULT_LOG_SUPERBLOCK_BACKUP_ADDRESS SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FAULT_LOG_SUPERBLOCK_BACKUP)
#define FAULT_LOG_AREA_SIZE                 SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FAULT_LOG)
#define FAULT_LOG_ADDRESS                   SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FAULT_LOG)
#define FAULT_LOG_BACKUP_ADDRESS            SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FAULT_LOG_BACKUP)

/* IEC104 event log */
#define IEC104_EVTLOG_SUPERBLOCK_SIZE       SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK)
#define IEC104_EVTLOG_SUPERBLOCK_ADDR       SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK)
#define IEC104_EVTLOG_SUPERBLOCK_BACKUP_ADDR SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK_BACKUP)
#define IEC104_EVTLOG_DATA_SECTOR_COUNT     2U
#define IEC104_EVTLOG_DATA_SECTOR_SIZE      4096U
#define IEC104_EVTLOG_DATA_SIZE             SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_IEC104_EVTLOG_DATA)
#define IEC104_EVTLOG_DATA_ADDR             SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_DATA)
#define IEC104_EVTLOG_DATA_BACKUP_ADDR      SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_DATA_BACKUP)

/* Firmware */
#define FIRMWARE_FLASH_AREA_SIZE            SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FIRMWARE_A)
#define FIRMWARE_A_ADDRESS                  SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A)
#define FIRMWARE_B_ADDRESS                  SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_B)
#define FIRMWARE_SECTION_END_ADDRESS        (FIRMWARE_B_ADDRESS + FIRMWARE_FLASH_AREA_SIZE)

/* ------------------------------------------------------------------ */
/*  Compile-time layout checks                                        */
/* ------------------------------------------------------------------ */
_Static_assert(sizeof(spi_flash_layout_t) <= SPI_FLASH_TOTAL_SIZE,
               "Flash layout exceeds flash boundary");

_Static_assert((SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A) % 0x10000UL) == 0U,
               "Firmware A must be 64KB block-aligned");

_Static_assert((SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_B) % 0x10000UL) == 0U,
               "Firmware B must be 64KB block-aligned");

#endif /* LIBS_W25QXX_MEMORY_ORGANIZATION_H_ */
