/*
 * w25qxx_memory_organization.h
 *
 * Mock flash memory layout definitions for host-side testing.
 * Provides the same macros as the production header using a
 * minimal struct layout with fake addresses.
 */

#ifndef MOCK_W25QXX_MEMORY_ORGANIZATION_H_
#define MOCK_W25QXX_MEMORY_ORGANIZATION_H_

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Mock flash boundaries                                             */
/* ------------------------------------------------------------------ */
#define SPI_FLASH_START_ADDR    0x000000UL
#define SPI_FLASH_TOTAL_SIZE    0x200000UL   /* 2 MB */

/* ------------------------------------------------------------------ */
/*  Minimal mock layout — sizes match production header                */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK        [0x001000];
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK_BACKUP [0x001000];
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_DATA              [0x002000];
    uint8_t SPIFLASH_SECTION_IEC104_EVTLOG_DATA_BACKUP       [0x002000];
} spi_flash_layout_t;

#define SPIFLASH_SECTION_ADDR(member)  ((uint32_t)offsetof(spi_flash_layout_t, member))
#define SPIFLASH_SECTION_SIZE(member)  ((uint32_t)sizeof(((spi_flash_layout_t *)0)->member))

#define IEC104_EVTLOG_SUPERBLOCK_SIZE       SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK)
#define IEC104_EVTLOG_SUPERBLOCK_ADDR       SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK)
#define IEC104_EVTLOG_SUPERBLOCK_BACKUP_ADDR SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_SUPERBLOCK_BACKUP)
#define IEC104_EVTLOG_DATA_SECTOR_COUNT     2U
#define IEC104_EVTLOG_DATA_SECTOR_SIZE      4096U
#define IEC104_EVTLOG_DATA_SIZE             SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_IEC104_EVTLOG_DATA)
#define IEC104_EVTLOG_DATA_ADDR             SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_DATA)
#define IEC104_EVTLOG_DATA_BACKUP_ADDR      SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IEC104_EVTLOG_DATA_BACKUP)

#endif /* MOCK_W25QXX_MEMORY_ORGANIZATION_H_ */
