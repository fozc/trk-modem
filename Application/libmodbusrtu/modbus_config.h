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

// baud_rate
uint32_t modbus_config_get_baud_rate(void);
void modbus_config_set_baud_rate(uint32_t baud);

// addr_aku_uyarisi
uint16_t modbus_config_get_addr_aku_uyarisi(void);
void modbus_config_set_addr_aku_uyarisi(uint16_t addr);

// addr_modem_reset
uint16_t modbus_config_get_addr_modem_reset(void);
void modbus_config_set_addr_modem_reset(uint16_t addr);

// Line Config Access
const modbus_line_config_t* modbus_get_line_config(uint32_t line_index);
bool modbus_set_line_config(uint32_t line_index, const modbus_line_config_t* line_config);
bool modbus_is_line_in_use(uint32_t line_index);

#endif /* LIBMODBUSRTU_MODBUS_CONFIG_H_ */
