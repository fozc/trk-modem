/*
 * mock_w25qxx.c
 *
 * Mock implementation of W25Qxx flash for host-based testing
 */

#include "w25qxx.h"
#include <string.h>
#include <stdio.h>

/* Simulated flash memory - 16MB */
#define MOCK_FLASH_SIZE (16 * 1024 * 1024)
static uint8_t mock_flash[MOCK_FLASH_SIZE];
static bool mock_flash_initialized = false;

void w25qxx_init(void)
{
	if (!mock_flash_initialized) {
		memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
		mock_flash_initialized = true;
		printf("[Mock Flash] Initialized %d MB\n", MOCK_FLASH_SIZE / (1024 * 1024));
	}
}

int w25qxx_read_buff(uint32_t address, void *buffer, uint32_t length)
{
	if (address + length > MOCK_FLASH_SIZE) {
		return -1;
	}
	
	memcpy(buffer, &mock_flash[address], length);
	return 0;
}

int w25qxx_write_buff(uint32_t address, const void *buffer, uint32_t length)
{
	if (address + length > MOCK_FLASH_SIZE) {
		return -1;
	}
	
	memcpy(&mock_flash[address], buffer, length);
	return 0;
}

int w25qxx_erase_sector(uint32_t address)
{
	if (address >= MOCK_FLASH_SIZE) {
		return -1;
	}
	
	/* Erase 4KB sector */
	uint32_t sector_start = address & ~0xFFF;
	memset(&mock_flash[sector_start], 0xFF, 4096);
	return 0;
}
