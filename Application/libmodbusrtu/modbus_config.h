/*
 * modbus_config.h
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */

#ifndef LIBMODBUSRTU_MODBUS_CONFIG_H_
#define LIBMODBUSRTU_MODBUS_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

/* Register addresses are NOT configurable: the map is compile-time fixed in
 * modbus_map.h. What remains stored in NVRAM are the serial-line settings
 * (device address, baud rate) and the RAM-only last-exception mirror. */

// Basic Functions
int modbus_config_sync(void);

// Global Config Access
const modbus_configs_t* modbus_config_get(void);
void modbus_config_set(const modbus_configs_t* config);

// Global Config Field Getters/Setters

// device_addr
uint8_t modbus_config_get_device_addr(void);
void modbus_config_set_device_addr(uint8_t addr);

// last_error_code
uint8_t modbus_config_get_last_error_code(void);
void modbus_config_set_last_error_code(uint8_t code);

// last_error_time (Unix epoch seconds, RAM-only - not persisted in NVRAM)
uint32_t modbus_config_get_last_error_time(void);
void modbus_config_set_last_error_time(uint32_t unix_time);

// baud_rate
uint32_t modbus_config_get_baud_rate(void);
void modbus_config_set_baud_rate(uint32_t baud);

// stop_bits: 1 (8N1, default) or 2 (8N2); other values rejected
uint8_t modbus_config_get_stop_bits(void);
bool modbus_config_set_stop_bits(uint8_t bits);

#endif /* LIBMODBUSRTU_MODBUS_CONFIG_H_ */
