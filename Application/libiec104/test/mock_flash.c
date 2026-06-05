/*
 * mock_flash.c
 *
 * Mock flash implementation for host-based IEC104 queue testing
 */

#include "mock_flash.h"
#include <string.h>
#include <stdio.h>

/* Simulated flash memory - 16MB */
#define MOCK_FLASH_SIZE (16 * 1024 * 1024)
static uint8_t mock_flash[MOCK_FLASH_SIZE];
static bool mock_flash_initialized = false;

void mock_flash_init(void)
{
	if (!mock_flash_initialized) {
		memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
		mock_flash_initialized = true;
		printf("[Mock Flash] Initialized %d MB\n", MOCK_FLASH_SIZE / (1024 * 1024));
	}
}

void mock_flash_read(uint32_t address, void *buffer, uint32_t length)
{
	if (address + length > MOCK_FLASH_SIZE) {
		printf("[Mock Flash] Read out of bounds: addr=0x%X, len=%u\n", address, length);
		return;
	}
	memcpy(buffer, &mock_flash[address], length);
}

int mock_flash_write(uint32_t address, const void *buffer, uint32_t length)
{
	if (address + length > MOCK_FLASH_SIZE) {
		printf("[Mock Flash] Write out of bounds: addr=0x%X, len=%u\n", address, length);
		return -1;
	}
	memcpy(&mock_flash[address], buffer, length);
	return 0;
}

void mock_flash_erase_sector(uint32_t address)
{
	if (address >= MOCK_FLASH_SIZE) {
		printf("[Mock Flash] Erase out of bounds: addr=0x%X\n", address);
		return;
	}
	/* Erase 4KB sector */
	uint32_t sector_start = address & ~0xFFF;
	memset(&mock_flash[sector_start], 0xFF, 4096);
}

void mock_flash_reset(void)
{
	memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
	printf("[Mock Flash] Reset to 0xFF\n");
}
