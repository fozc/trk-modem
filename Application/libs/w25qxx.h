/*
 * w25qxx.h
 *
 *  Created on: Dec 6, 2023
 *      Author: fatih
 */

#ifndef LIBS_W25QXX_H_
#define LIBS_W25QXX_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stdbool.h>
#include <spi_flash_organization.h>

#define	W25QXX_RES_OK               0
#define	W25QXX_RES_ERROR           -1
#define	W25QXX_RES_TIMEOUT         -2
#define	W25QXX_RES_INVALID_PARAM   -3
#define W25QXX_RES_BUSY            -4
#define W25QXX_RES_VERIFY_FAILED   -5
#define W25QXX_RES_PROTECT_ENABLED -6
#define W25QXX_RES_WEL_NOT_SET     -7
#define W25QXX_RES_WRITE_FAIL      -8

typedef struct 
{
    uint32_t jedec_id;          // Beklenen JEDEC ID (şu an 0x001540EF)
    uint32_t size_bytes;        // Flash boyutu (şu an 2MB)
    uint32_t block_size;        // Block boyutu (şu an 64KB)
    uint32_t sector_size;       // Sector boyutu (şu an 4KB)
    uint32_t page_size;         // Page boyutu (şu an 256 bytes)
} w25qxx_chip_config_t;

#define W25QXX_SIZE        (2048 * 1024) /* Bytes */
#define W25QXX_BLOCK_SIZE  (64 * 1024)
#define W25QXX_SECTOR_SIZE (4 * 1024)
#define W25QXX_PAGE_SIZE   256

#define W25QXX_START_ADDR 0x000000
#define W25QXX_END_ADDR   (W25QXX_START_ADDR + W25QXX_SIZE - 1)

#define W25QXX_BLOCK_COUNT  (W25QXX_SIZE / W25QXX_BLOCK_SIZE)
#define W25QXX_SECTOR_COUNT (W25QXX_SIZE / W25QXX_SECTOR_SIZE)

int w25qxx_init(void);
uint16_t w25qxx_read_manu_deviceid(void);
uint32_t w25qxx_read_jedecid(void);

int w25qxx_erase_block64(uint32_t addr);
int w25qxx_erase_block32(uint32_t addr);
int w25qxx_erase_sector(uint32_t addr);
uint8_t w25qxx_read_byte(uint32_t addr);
void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len);
int w25qxx_write_byte(uint32_t addr, uint8_t data);
int w25qxx_write_sector(uint32_t addr, const void *buff, uint32_t lenght);
int w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t buff_len);
int w25qxx_verify(uint32_t addr, const void *data, uint32_t len);

void w25qxx_test(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBS_W25QXX_H_ */
