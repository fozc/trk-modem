/*
 * mock_flash.h
 *
 * Mock flash memory interface for host-based testing
 */

#ifndef MOCK_FLASH_H_
#define MOCK_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

void mock_flash_init(void);
void mock_flash_read(uint32_t address, void *buffer, uint32_t length);
int mock_flash_write(uint32_t address, const void *buffer, uint32_t length);
void mock_flash_erase_sector(uint32_t address);
void mock_flash_reset(void);

#endif /* MOCK_FLASH_H_ */
