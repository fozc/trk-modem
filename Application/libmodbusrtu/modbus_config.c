/*
 * modbus_config.c
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */
#include "modbus_config.h"
#include "nvram.h"
#include "bsp.h"

#define modbus_config (nvram_get_modbus_config_rw())
#define breaker_config (nvram_get_breaker())

int modbus_config_sync(void)
{
	return nvram_sync(false);
}

// ============================================
// Global Config Access
// ============================================

const modbus_configs_t* modbus_config_get(void)
{
	return modbus_config;
}

void modbus_config_set(const modbus_configs_t* config)
{
	if(config) {
		*modbus_config = *config;
	}
}

// ============================================
// Global Config Field Getters/Setters
// ============================================

// device_addr
uint8_t modbus_config_get_device_addr(void)
{
	return modbus_config->device_addr;
}

void modbus_config_set_device_addr(uint8_t addr)
{
	modbus_config->device_addr = addr;
}

// last_error_code
uint8_t modbus_config_get_last_error_code(void)
{
	return modbus_config->last_error_code;
}

void modbus_config_set_last_error_code(uint8_t code)
{
	modbus_config->last_error_code = code;
}

// last_error_time (Unix epoch seconds, RAM-only - not persisted in NVRAM)
static uint32_t s_last_error_time = 0U;

uint32_t modbus_config_get_last_error_time(void)
{
	return s_last_error_time;
}

void modbus_config_set_last_error_time(uint32_t unix_time)
{
	s_last_error_time = unix_time;
}

// baud_rate
uint32_t modbus_config_get_baud_rate(void)
{
	return modbus_config->baud_rate;
}

void modbus_config_set_baud_rate(uint32_t baud)
{
	modbus_config->baud_rate = baud;
}

// addr_aku_uyarisi
uint16_t modbus_config_get_addr_aku_uyarisi(void)
{
	return modbus_config->addr_aku_uyarisi;
}

void modbus_config_set_addr_aku_uyarisi(uint16_t addr)
{
	modbus_config->addr_aku_uyarisi = addr;
}

// addr_modem_reset
uint16_t modbus_config_get_addr_modem_reset(void)
{
	return modbus_config->addr_modem_reset;
}

void modbus_config_set_addr_modem_reset(uint16_t addr)
{
	modbus_config->addr_modem_reset = addr;
}

// ============================================
// Line Config Access
// ============================================

const modbus_line_config_t* modbus_get_line_config(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}

	return &breaker_config->line[line_index].modbus;
}

bool modbus_set_line_config(uint32_t line_index, const modbus_line_config_t* line_config)
{
	if(line_index >= MAX_POWER_LINE_COUNT || line_config == NULL) {
		return false;
	}

	breaker_config->line[line_index].modbus = *line_config;
	return true;
}

bool modbus_is_line_in_use(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return false;
	}

	return breaker_config->line[line_index].modbus.in_use != 0;
}

