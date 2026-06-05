/*
 * modbus_process.c
 *
 *  Created on: Oct 26, 2025
 *      Author: fatih
 */
#include "modbus_process.h"
#include "modbus_rtu_slave.h"
#include "bsp.h"
#include "nvram.h"
#include "uart.h"
#include "contiki.h"
#include "contiki_process.h"

static float current_1 = 312.5f;      // Ampere
static float current_2 = 300.75f;      // Ampere
static float current_3 = 15.3f;      // Ampere
static uint16_t switch1_state = 1;   // ON
static uint16_t switch2_state = 0;   // OFF
static uint16_t switch3_state = 2;   // Unknown
static uint16_t switch4_state = 1;   // ON
static uint16_t switch_control = 0;  // OFF

static void uart_write(const uint8_t *data, uint16_t len)
{
	uart_send_buffer(UART_1, (const char *)data, len);

    // Debug print
    CSLOG_NODT("\r\nTX[%d]: ", len);
    for (uint16_t i = 0; i < len; i++) {
    	CSLOG_NODT("%02X ", data[i]);
    }
    CSLOG_NODT("\r\n");
}

/**
 * @brief Convert float to two uint16_t registers (Modbus format)
 */
static void float_to_registers(float value, uint16_t *reg_high, uint16_t *reg_low)
{
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));
    *reg_high = (uint16_t)(temp >> 16);
    *reg_low = (uint16_t)(temp & 0xFFFF);
}

/**
 * @brief Convert two uint16_t registers to float (Modbus format)
 */
static float registers_to_float(uint16_t reg_high, uint16_t reg_low)
{
    uint32_t temp = ((uint32_t)reg_high << 16) | reg_low;
    float value;
    memcpy(&value, &temp, sizeof(float));
    return value;
}

/**
 * @brief Callback for Function Code 03 - Read Holding Registers
 */
static void fc03_callback(uint16_t start_addr, uint16_t count, uint16_t *data)
{
    CSLOG("FC03 Request - Start: %d, Count: %d\n", start_addr, count);

    // Handle each register based on address
    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr = start_addr + i;
        uint16_t reg_high, reg_low;

        switch (addr) {
            case 40000:  // Current 1 - High word
                float_to_registers(current_1, &reg_high, &reg_low);
                data[i] = reg_high;
                break;

            case 40001:  // Current 1 - Low word
                float_to_registers(current_1, &reg_high, &reg_low);
                data[i] = reg_low;
                break;

            case 40002:  // Current 2 - High word
                float_to_registers(current_2, &reg_high, &reg_low);
                data[i] = reg_high;
                break;

            case 40003:  // Current 2 - Low word
                float_to_registers(current_2, &reg_high, &reg_low);
                data[i] = reg_low;
                break;

            case 40004:  // Current 3 - High word
                float_to_registers(current_3, &reg_high, &reg_low);
                data[i] = reg_high;
                break;

            case 40005:  // Current 3 - Low word
                float_to_registers(current_3, &reg_high, &reg_low);
                data[i] = reg_low;
                break;

            case 40006:  // Switch 1 State
                data[i] = switch1_state;
                break;

            case 40007:  // Switch 2 State
                data[i] = switch2_state;
                break;

            case 40008:  // Switch 3 State
                data[i] = switch3_state;
                break;

            case 40009:  // Switch 4 State
                data[i] = switch4_state;
                break;

            case 40010:  // Switch Control - Read current state
                data[i] = switch_control;
                break;

            default:
                // Unknown register - return 0
                data[i] = 0;
                break;
        }
    }
}

/**
 * @brief Callback for Function Code 06 - Write Single Register
 */
static uint8_t fc06_callback(uint16_t addr, uint16_t value)
{
    CSLOG("FC06 Request - Addr: %d, Value: 0x%04X (%d)\n", addr, value, value);

    // Only switch control register (40010) is writable
    if (addr == 40010) {
        if (value == 0 || value == 1) {
            switch_control = value;
            CSLOG("  -> Switch Control set to: %s\n", value ? "ON" : "OFF");

            // TODO: Here you would control actual hardware
            // e.g., GPIO pin control for relay/switch

            return 1; // Success
        } else {
            CSLOG("  -> Invalid value! Only 0 (OFF) or 1 (ON) allowed\n");
            return 0; // Invalid value
        }
    }

    // All other registers are read-only
    CSLOG("  -> Register is read-only!\n");
    return 0; // Fail - read-only register
}

/**
 * @brief Simulate current readings (for demo)
 */
void update_current_readings(void)
{
    static uint32_t last_update = 0;
    uint32_t now = millis();

    // Update every 1000ms
    if (now - last_update >= 1000000) {
        // Simulate changing currents (realistic values)
        current_1 = 10.0f + (now % 100) / 10.0f;   // 10.0 - 20.0 A
        current_2 = 5.0f + (now % 50) / 10.0f;     // 5.0 - 10.0 A
        current_3 = 15.0f + (now % 80) / 10.0f;    // 15.0 - 23.0 A

        // Simulate switch states (cycle through states)
        switch1_state = (now / 2000) % 3;  // 0, 1, 2
        switch2_state = (now / 3000) % 3;  // 0, 1, 2
        switch3_state = (now / 4000) % 3;  // 0, 1, 2
        switch4_state = (now / 5000) % 3;  // 0, 1, 2

        last_update = now;
    }
}

PROCESS(modbus_process, "modbus_process");
PROCESS_THREAD(modbus_process, ev, data)
{
	static struct etimer timer;

	PROCESS_BEGIN();

	uint8_t id = nvram_get_modbus_device_addr();
	libmodbusrtu_slave_init(id, millis, uart_write);

    // Register callbacks
    libmodbusrtu_register_fc03_callback(fc03_callback);
    libmodbusrtu_register_fc06_callback(fc06_callback);

	etimer_set(&timer, 100);
	while (1)
	{
 		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
 		etimer_restart(&timer);
 		libmodbusrtu_modbus_process();

	}

	PROCESS_END();
}



void modbus_process_init(void)
{
	process_start(&modbus_process, NULL);
}

