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
#include "gpio_defs.h"
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

/* Modbus slave instance for the UART4 bus. Kept private so the ISR stays trivial. */
static modbus_slave_t s_modbus;

static void uart_write(const uint8_t *data, uint16_t len)
{
    /* Half-duplex RS-485: drive the bus via the MODBUS_OE pin (active high). */
    uart_send_buffer_rs485(UART_4,
                           MODBUS_OE_BSP_GPIO,
                           MODBUS_OE_BSP_PIN,
                           true,
                           data,
                           len);

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

/* Demo holding-register map (logical references, 40000-based). */
#define MODBUS_REG_CURRENT1_HI      40000U
#define MODBUS_REG_CURRENT1_LO      40001U
#define MODBUS_REG_CURRENT2_HI      40002U
#define MODBUS_REG_CURRENT2_LO      40003U
#define MODBUS_REG_CURRENT3_HI      40004U
#define MODBUS_REG_CURRENT3_LO      40005U
#define MODBUS_REG_SWITCH1_STATE    40006U
#define MODBUS_REG_SWITCH2_STATE    40007U
#define MODBUS_REG_SWITCH3_STATE    40008U
#define MODBUS_REG_SWITCH4_STATE    40009U
#define MODBUS_REG_SWITCH_CONTROL   40010U

#define MODBUS_SWITCH_OFF           0U
#define MODBUS_SWITCH_ON            1U

/*
 * RX-timeout (T3.5) idle gap in bit-times. Modbus RTU fixes T3.5 at 1.75 ms for
 * baud rates above 19200; 1.75 ms x 115200 baud ~= 202 bit-times.
 */
#define MODBUS_RTO_BIT_TIMES        202U

/* Demo-data refresh period (ticks). 1 Contiki tick == 1 ms on this platform. */
#define MODBUS_DEMO_UPDATE_TICKS    500U

/* Inter-frame gap re-poll period (ticks) for software frame-end detection. */
#define MODBUS_RX_GAP_TICKS         MODBUS_TIMEOUT_MS

/**
 * @brief Read-register callback for FC03 (Read Holding Registers).
 * @param[in]  reg_addr Resolved logical register reference.
 * @param[out] p_value  Destination for the register value.
 * @return MODBUS_REG_OK if mapped, MODBUS_REG_ERR_ADDRESS otherwise.
 */
static modbus_reg_status_t fc03_read_callback(uint16_t reg_addr, uint16_t *p_value)
{
    uint16_t reg_high;
    uint16_t reg_low;

    switch (reg_addr) {
        case MODBUS_REG_CURRENT1_HI:
            float_to_registers(current_1, &reg_high, &reg_low);
            *p_value = reg_high;
            break;

        case MODBUS_REG_CURRENT1_LO:
            float_to_registers(current_1, &reg_high, &reg_low);
            *p_value = reg_low;
            break;

        case MODBUS_REG_CURRENT2_HI:
            float_to_registers(current_2, &reg_high, &reg_low);
            *p_value = reg_high;
            break;

        case MODBUS_REG_CURRENT2_LO:
            float_to_registers(current_2, &reg_high, &reg_low);
            *p_value = reg_low;
            break;

        case MODBUS_REG_CURRENT3_HI:
            float_to_registers(current_3, &reg_high, &reg_low);
            *p_value = reg_high;
            break;

        case MODBUS_REG_CURRENT3_LO:
            float_to_registers(current_3, &reg_high, &reg_low);
            *p_value = reg_low;
            break;

        case MODBUS_REG_SWITCH1_STATE:
            *p_value = switch1_state;
            break;

        case MODBUS_REG_SWITCH2_STATE:
            *p_value = switch2_state;
            break;

        case MODBUS_REG_SWITCH3_STATE:
            *p_value = switch3_state;
            break;

        case MODBUS_REG_SWITCH4_STATE:
            *p_value = switch4_state;
            break;

        case MODBUS_REG_SWITCH_CONTROL:
            *p_value = switch_control;
            break;

        default:
            /* Unmapped register - signal an exception to the master. */
            return MODBUS_REG_ERR_ADDRESS;
    }

    return MODBUS_REG_OK;
}

/**
 * @brief Write-register callback for FC06 (Write Single Register).
 * @param[in] reg_addr Resolved logical register reference.
 * @param[in] value    Value to write.
 * @return MODBUS_REG_OK on success, MODBUS_REG_ERR_ADDRESS for read-only/unmapped
 *         registers, MODBUS_REG_ERR_VALUE for an out-of-range value.
 */
static modbus_reg_status_t fc06_write_callback(uint16_t reg_addr, uint16_t value)
{
    CSLOG("FC06 Request - Addr: %d, Value: 0x%04X (%d)\n", reg_addr, value, value);

    /* Only the switch-control register is writable. */
    if (reg_addr != MODBUS_REG_SWITCH_CONTROL) {
        CSLOG("  -> Register is read-only!\n");
        return MODBUS_REG_ERR_ADDRESS;
    }

    if ((value != MODBUS_SWITCH_OFF) && (value != MODBUS_SWITCH_ON)) {
        CSLOG("  -> Invalid value! Only 0 (OFF) or 1 (ON) allowed\n");
        return MODBUS_REG_ERR_VALUE;
    }

    switch_control = value;
    CSLOG("  -> Switch Control set to: %s\n", (value != MODBUS_SWITCH_OFF) ? "ON" : "OFF");

    // TODO: Drive the actual relay/switch hardware here.

    return MODBUS_REG_OK;
}

/**
 * @brief Simulate current readings (for demo)
 */
void update_current_readings(void)
{
    static uint32_t last_update = 0;
    uint32_t now = millis();

    // Update every 1000ms (millis() resolution is 1 ms)
    if (now - last_update >= 1000U) {
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
	static struct etimer demo_timer;
	static struct etimer rx_timer;     /* Re-poll timer for the inter-frame gap. */

	PROCESS_BEGIN();

	uint8_t id = nvram_get_modbus_device_addr();
	libmodbusrtu_slave_init(&s_modbus, id, millis, uart_write);

	// Register per-register read/write callbacks
	libmodbusrtu_register_read_callback(&s_modbus, fc03_read_callback);
	libmodbusrtu_register_write_callback(&s_modbus, fc06_write_callback);

	// Enable the UART4 RX interrupt so received bytes reach the slave core
	uart_set_rx_interrupt(UART_4, UART_RX_INT_ENABLE);

#if (MODBUS_USE_HW_RTO == 1)
	// Hardware frame-end detection: raise an interrupt after the T3.5 idle gap
	uart_set_rx_timeout(UART_4, MODBUS_RTO_BIT_TIMES);
#endif

	// Event-driven: the RX interrupt buffers bytes and wakes this process, which
	// then frames and parses them. A short rx_timer re-poll catches the
	// inter-frame idle gap that ends variable / unknown-length frames in software
	// mode. The demo_timer only paces the simulated demo data.
	etimer_set(&demo_timer, MODBUS_DEMO_UPDATE_TICKS);
	while (1)
	{
		PROCESS_WAIT_EVENT();

		if ((ev == PROCESS_EVENT_POLL) ||
		    ((ev == PROCESS_EVENT_TIMER) && (data == &rx_timer)))
		{
			// Try to frame and process the buffered bytes.
			modbus_poll_result_t result = libmodbusrtu_modbus_process(&s_modbus);

			if (result == MODBUS_POLL_RECEIVING)
			{
				// Frame still arriving: re-poll soon to detect its end.
				etimer_set(&rx_timer, MODBUS_RX_GAP_TICKS);
			}
		}

		if (etimer_expired(&demo_timer))
		{
			// Refresh the simulated current readings and switch states.
			update_current_readings();
			etimer_reset(&demo_timer);
		}
	}

	PROCESS_END();
}



void modbus_process_init(void)
{
	process_start(&modbus_process, NULL);
}

void modbus_process_poll(void)
{
	process_poll(&modbus_process);
}

void modbus_process_isr_rx_byte(uint8_t byte)
{
	libmodbusrtu_modbus_rx_byte(&s_modbus, byte);
#if (MODBUS_USE_HW_RTO == 0)
	/* Software mode: wake the loop so it can frame and parse the bytes. */
	process_poll(&modbus_process);
#endif
}

#if (MODBUS_USE_HW_RTO == 1)
void modbus_process_isr_rx_timeout(void)
{
	libmodbusrtu_modbus_rx_timeout(&s_modbus);
	process_poll(&modbus_process);
}
#endif

