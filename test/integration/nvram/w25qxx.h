/*
 * Stub of libs/w25qxx.h for the nvram host tests: only the result
 * codes and the two entry points nvram.c uses are declared; the
 * implementation is the RAM slot double in mock_platform.c.
 */
#ifndef NVRAM_TEST_STUB_W25QXX_H_
#define NVRAM_TEST_STUB_W25QXX_H_

#include <stdint.h>
#include <spi_flash_organization.h>

#define W25QXX_RES_OK             0
#define W25QXX_RES_ERROR         -1
#define W25QXX_RES_INVALID_PARAM -3
#define W25QXX_RES_WRITE_FAIL    -8

void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len);
int  w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t buff_len);
int  w25qxx_erase_sector(uint32_t addr);
int  w25qxx_page_write(uint32_t addr, const void *buff, uint32_t len);

#endif /* NVRAM_TEST_STUB_W25QXX_H_ */
