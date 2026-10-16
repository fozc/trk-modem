/*
 * modbus_process.c
 *
 *  Created on: Oct 26, 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "modbus_process.h"
#include "modbus_rtu_slave.h"
#include "modbus_config.h"
#include "modbus_map.h"
#include "modbus_line_data.h"
#include "modbus_system_stats.h"
#include "modbus_power_stats.h"
#include "modbus_bms_stats.h"
#include "modbus_gsm_stats.h"
#include "power_board_scp.h"
#include "breaker.h"
#include "rf_hil_transport.h"
#include "bsp.h"
#include "reboot.h"
#include "relay.h"
#include "rtc.h"
#include "nvram.h"
#include "uart.h"
#include "gpio_defs.h"
#include "contiki.h"
#include "contiki_process.h"
#include "main.h"
#include "stm32u3xx_hal.h"
#include <string.h>

/*
 * Feeder block address space (fixed map, modbus_map.h). Every address inside
 * MODBUS_MAP_LINE_ADDR_FIRST..LAST answers FC03: live fields resolve to
 * their measurement, undefined feeders and reserved gaps read back as 0 with
 * the quality registers marking the values invalid. Addresses outside every
 * mapped block raise an illegal-address exception. See MODBUS_REGISTER_MAP.md.
 */

/* Modbus slave instance for the UART4 bus. Kept private so the ISR stays trivial. */
static modbus_slave_t s_modbus;
/* All FC03 callbacks run synchronously in cooperative process context.
 * Keep one age-check time for both words and all RF fields in a response.
 */
static uint32_t rf_sample_ms;

static modbus_poll_result_t modbus_poll(modbus_slave_t *slave)
{
    rf_sample_ms = HAL_GetTick();
    return libmodbusrtu_modbus_process(slave);
}

/* Baud rate currently active on UART4; skips a redundant BRR write when the
 * stored value already matches (115200 is the CubeMX MX_UART4_Init default).
 * HIL bench build: UART4 carries SCP, the Modbus baud state is unused. */
#if !RF_SCP_OVER_MODBUS_PORT
static uint32_t active_baud = 115200U;
#endif

/*
 * Modbus response transmit method (compile-time selectable):
 *   1 (default): non-blocking DMA transfer. The response is copied to a
 *                persistent buffer and shifted out by GPDMA1; the RS-485 bus is
 *                released from the UART4 transmission-complete callback.
 *   0          : blocking, byte-by-byte transmit (pre-DMA method). Holds the
 *                cooperative scheduler until the final byte has been shifted
 *                out, then releases the bus inline.
 */
#ifndef MODBUS_TX_USE_DMA
#define MODBUS_TX_USE_DMA   1
#endif

#if (MODBUS_TX_USE_DMA != 0)
/* UART4 HAL handle (defined in main.c) used for the DMA response transmit. */
extern UART_HandleTypeDef huart4;

/*
 * Persistent transmit buffer for DMA. The Modbus core builds each response on
 * its own stack and returns immediately after calling the write hook, so the
 * bytes must be copied somewhere that outlives the call for the background DMA
 * transfer to read from.
 */
static uint8_t s_modbus_tx_dma_buf[MODBUS_BUFFER_SIZE];

/* Set while a DMA response is in flight on the RS-485 bus. Cleared from the
 * UART4 transmission-complete callback once the last byte has been shifted out. */
static volatile bool s_modbus_tx_busy = false;
#endif /* MODBUS_TX_USE_DMA */

static void uart_write(const uint8_t *data, uint16_t len)
{
#if (MODBUS_TX_USE_DMA != 0)
    if ((data == NULL) || (len == 0U) || (len > sizeof(s_modbus_tx_dma_buf))) {
        return;
    }

    /* Modbus is strictly request/response, so a previous response has always
     * left the bus before the next request is parsed: s_modbus_tx_busy is a
     * pure safety guard. If a transfer is somehow still in flight, drop this
     * response rather than block the cooperative scheduler. */
    if (s_modbus_tx_busy) {
        CSLOG_ERR("Modbus TX busy, dropping response\r\n");
        return;
    }

    (void)memcpy(s_modbus_tx_dma_buf, data, len);
    s_modbus_tx_busy = true;

    /* Half-duplex RS-485: drive the bus via the MODBUS_OE pin (active high).
     * The frame is sent by DMA; the OE pin is released from
     * modbus_process_tx_complete_callback() once the final byte has been
     * shifted out. */
    gpio_set_pin(MODBUS_OE_BSP_GPIO, MODBUS_OE_BSP_PIN, GPIO_HIGH);

    /* Clear any stale transmission-complete flag: HAL enables TCIE at the end
     * of the DMA transfer without clearing TC first, so a leftover flag would
     * fire the completion interrupt prematurely. */
    LL_USART_ClearFlag_TC(UART4);

    if (HAL_UART_Transmit_DMA(&huart4, s_modbus_tx_dma_buf, len) != HAL_OK) {
        /* Failed to start: release the bus and clear the busy flag. */
        gpio_set_pin(MODBUS_OE_BSP_GPIO, MODBUS_OE_BSP_PIN, GPIO_LOW);
        s_modbus_tx_busy = false;
    }
#if 0
    // Debug print
    CSLOG_NODT("\r\nTX[%d]: ", len);
    for (uint16_t i = 0; i < len; i++) {
    	CSLOG_NODT("%02X ", data[i]);
    }
    CSLOG_NODT("\r\n");
#endif
#else  /* Blocking transmit (pre-DMA method). */
    if ((data == NULL) || (len == 0U)) {
        return;
    }

    /* Half-duplex RS-485: assert MODBUS_OE (active high), shift every byte out
     * inline and release the bus once the final byte has left the shifter. */
    uart_send_buffer_rs485(UART_4, MODBUS_OE_BSP_GPIO, MODBUS_OE_BSP_PIN,
                           true, data, len);
#endif /* MODBUS_TX_USE_DMA */
}

void modbus_process_tx_complete_callback(void)
{
#if (MODBUS_TX_USE_DMA != 0)
    /* The last byte has been fully shifted out: release the RS-485 bus so other
     * nodes (and our own receiver) can drive it, then allow the next response. */
    gpio_set_pin(MODBUS_OE_BSP_GPIO, MODBUS_OE_BSP_PIN, GPIO_LOW);
    s_modbus_tx_busy = false;
#endif /* MODBUS_TX_USE_DMA */
}


/*
 * RX-timeout (T3.5) idle gap in bit-times. Modbus RTU fixes T3.5 at 1.75 ms for
 * baud rates above 19200; 1.75 ms x 115200 baud ~= 202 bit-times.
 */
#define MODBUS_RTO_BIT_TIMES        202U

/* Value written to addr_modem_reset that triggers a system reset. */
#define MODBUS_MODEM_RESET_TRIGGER  1U

/* Inter-frame gap re-poll period (ticks) for software frame-end detection. */
#define MODBUS_RX_GAP_TICKS         MODBUS_TIMEOUT_MS

/*
 * Battery-warning register (fixed 50000): each battery alarm of the power
 * board (R2 section 5.3) gets one dedicated register bit, in table order.
 * The register is 16 bits wide, so alarm codes above 15 cannot keep their
 * code number as a bit index - the mapping below is the contract, mirrored
 * in MODBUS_REGISTER_MAP.md section 6:
 *   bit 0 = kod 0  AKU_COK_DUSUK      bit 5 = kod 7  AKU_OVP_SARJ_YASAGI
 *   bit 1 = kod 2  AKU_SOGUK          bit 6 = kod 8  AKU_OVP_KALICI
 *   bit 2 = kod 3  AKU_SICAK          bit 7 = kod 18 SOC_DUSUK
 *   bit 3 = kod 4  AKU_DUSUK          bit 8 = kod 20 AKU_YOK
 *   bit 4 = kod 5  AKU_YUKSEK
 */
#define MODBUS_AKU_ALARM_COUNT  9U
static const uint8_t aku_alarm_codes[MODBUS_AKU_ALARM_COUNT] =
{
    0U, 2U, 3U, 4U, 5U, 7U, 8U, 18U, 20U
};

/* Live battery-warning word: the battery-related power-board alarm mask.
 * Zero when no summary has arrived or the alarm mask is no longer current
 * (stale summaries drop POWER_VALID_ALARMS); a nonzero value means a
 * battery warning is present and the bits name it. */
static uint16_t aku_uyarisi_value(void)
{
    power_board_snapshot_t power;
    uint16_t value = 0U;

    (void)power_board_get_snapshot(HAL_GetTick(), &power);
    if (power.has_summary &&
        (0U != (power.valid_fields & POWER_VALID_ALARMS)))
    {
        for (uint32_t i = 0U; i < MODBUS_AKU_ALARM_COUNT; i++)
        {
            if (0U != (power.active_alarms & (1UL << aku_alarm_codes[i])))
            {
                value |= (uint16_t)(1U << i);
            }
        }
    }

    return value;
}

/**
 * @brief Read-register callback for FC03 (Read Holding Registers).
 * @param[in]  reg_addr Resolved logical register reference.
 * @param[out] p_value  Destination for the register value.
 * @return MODBUS_REG_OK if mapped, MODBUS_REG_ERR_ADDRESS otherwise.
 */
static modbus_reg_status_t fc03_read_callback(uint16_t reg_addr, uint16_t *p_value)
{
    /* Feeder blocks (fixed map, modbus_map.h): every address inside the four
     * feeder blocks answers OK; undefined feeders and reserved gaps read back
     * as 0 with the quality registers marking the values invalid. */
    if ((reg_addr >= MODBUS_MAP_LINE_ADDR_FIRST) &&
        (reg_addr <= MODBUS_MAP_LINE_ADDR_LAST))
    {
        (void)modbus_line_data_read(reg_addr, rf_sample_ms, p_value);
        return MODBUS_REG_OK;
    }

    /* Fixed control registers. */
    if ((MODBUS_MAP_CTRL_RELAY1 == reg_addr) ||
        (MODBUS_MAP_CTRL_RELAY2 == reg_addr))
    {
        const relay_channel_t channel =
            (MODBUS_MAP_CTRL_RELAY1 == reg_addr) ? RELAY_CH_1 : RELAY_CH_2;
        *p_value = relay_is_on(channel) ? 1U : 0U;
        return MODBUS_REG_OK;
    }

    if (reg_addr == MODBUS_MAP_CTRL_AKU_UYARISI) {
        *p_value = aku_uyarisi_value();
        return MODBUS_REG_OK;
    }

    if (reg_addr == MODBUS_MAP_CTRL_MODEM_RESET) {
        /* Write-only command register: reads back as 0. */
        *p_value = 0U;
        return MODBUS_REG_OK;
    }

    /* System statistics block (base 49000): uptime, RTC, reset reason,
     * supply voltages... Read-only, contiguous and bulk-readable in a single
     * FC03 window. */
    if (modbus_system_stats_read(reg_addr, p_value)) {
        return MODBUS_REG_OK;
    }

    /* PowerBoard telemetry block (base 49200): battery/PV/DC voltages,
     * charge & bus currents, temperatures, SoC/SoH and alarms. Read-only,
     * contiguous and bulk-readable in a single FC03 window. */
    if (modbus_power_stats_read(reg_addr, p_value)) {
        return MODBUS_REG_OK;
    }

    /* BMS telemetry block (base 49300): pack voltage/current, SoC/SoH,
     * per-cell voltages, temperatures, work state, MOS and fault bitfields.
     * Read-only, contiguous and bulk-readable in a single FC03 window. */
    if (modbus_bms_stats_read(reg_addr, p_value)) {
        return MODBUS_REG_OK;
    }

    /* GSM-status block (base 49400): GSM modem state/signal/RAT and the
     * last Modbus exception code/time. Read-only, contiguous and
     * bulk-readable in a single FC03 window. */
    if (modbus_gsm_stats_read(reg_addr, p_value)) {
        return MODBUS_REG_OK;
    }

    /* Unmapped register - signal an exception to the master. */
    CSLOG_WARN("MODBUS FC03 hata: tanimsiz adres %u (exception 0x02)\r\n",
               reg_addr);
    return MODBUS_REG_ERR_ADDRESS;
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
    CSLOG("FC06 Request - Addr: %d, Value: 0x%04X (%d)\r\n", reg_addr, value, value);

    if ((MODBUS_MAP_CTRL_RELAY1 == reg_addr) ||
        (MODBUS_MAP_CTRL_RELAY2 == reg_addr))
    {
        if (1U < value)
        {
            return MODBUS_REG_ERR_VALUE;
        }
        const relay_channel_t channel =
            (MODBUS_MAP_CTRL_RELAY1 == reg_addr) ? RELAY_CH_1 : RELAY_CH_2;
        relay_set(channel, 1U == value);
        return MODBUS_REG_OK;
    }

    /* All other writes must target the existing modem-reset command. */
    if (reg_addr != MODBUS_MAP_CTRL_MODEM_RESET) {
        CSLOG_WARN("MODBUS FC06 hata: salt-okunur adres %u (exception 0x02)\r\n",
                   reg_addr);
        return MODBUS_REG_ERR_ADDRESS;
    }

    if (value != MODBUS_MODEM_RESET_TRIGGER) {
        CSLOG_WARN("MODBUS FC06 hata: gecersiz deger %u (exception 0x03)\r\n",
                   value);
        return MODBUS_REG_ERR_VALUE;
    }

    CSLOG("  -> Modem reset requested via Modbus\r\n");
    /* Allow the FC06 response to be sent before the scheduled reset. */
    reboot_system_delayed(1000U);
    return MODBUS_REG_OK;
}

/**
 * @brief Apply the NVRAM Modbus baud rate to UART4.
 *
 * Skips the write when the stored value equals the active baud rate, so a
 * boot-time call with the factory default (115200) leaves BRR untouched.
 * Enforces the "UART idle" precondition itself: an in-flight response DMA
 * is aborted, the RS-485 driver is released and the RX framing state is
 * reset before the peripheral is reprogrammed.
 */
static void modbus_uart_apply_baudrate(void)
{
#if RF_SCP_OVER_MODBUS_PORT
    /* HIL bench build: UART4 belongs to the SCP transport at 230400;
     * a stale web save of the Modbus config must not reprogram it. */
    return;
#else
    const modbus_configs_t *cfg = nvram_get_modbus_config();

    if ((cfg->baud_rate != 0U) && (cfg->baud_rate != active_baud))
    {
#if (MODBUS_TX_USE_DMA != 0)
        /* Havadaki yanit DMA'sini temizce iptal et, OE'yu birak. Yarim
         * kalan yanit gider; master timeout/retry yapar. Takili DMA +
         * OE basili + tx_busy sonsuz senaryosu boyle kokten kalkar. */
        if (s_modbus_tx_busy)
        {
            if (HAL_UART_AbortTransmit(&huart4) != HAL_OK)
            {
                CSLOG_ERR("MODBUS: TX abort basarisiz\r\n");
            }
            gpio_set_pin(MODBUS_OE_BSP_GPIO, MODBUS_OE_BSP_PIN, GPIO_LOW);
            s_modbus_tx_busy = false;
        }
#endif
        libmodbusrtu_modbus_reset_rx(&s_modbus);

        uart_set_baudrate(UART_4, cfg->baud_rate);
        active_baud = cfg->baud_rate;
        CSLOG_WARN("MODBUS: UART baudrate set to %u\r\n", active_baud);
    }
#endif /* !RF_SCP_OVER_MODBUS_PORT */
}

PROCESS(modbus_process, "modbus_process");
PROCESS_THREAD(modbus_process, ev, data)
{
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
	// mode.
	while (1)
	{
		PROCESS_WAIT_EVENT();

		if ((ev == PROCESS_EVENT_POLL) ||
		    ((ev == PROCESS_EVENT_TIMER) && (data == &rx_timer)))
		{
			// Try to frame and process the buffered bytes.
			modbus_poll_result_t result = modbus_poll(&s_modbus);

			if (result == MODBUS_POLL_HANDLED)
			{
				// Mirror the last Modbus exception so the web UI can report it.
				uint8_t exc =
				    libmodbusrtu_modbus_get_last_exception(&s_modbus);

				if (exc != 0U)
				{
					modbus_config_set_last_error_code(exc);
					modbus_config_set_last_error_time(rtc_get_unix_epoch());

					CSLOG_WARN("MODBUS exception: 0x%02X at %lu\r\n",
					         exc, modbus_config_get_last_error_time());

					// FC 0x01 (illegal function) is rejected in the core
					// before any callback runs, so log it here. 0x02/0x03
					// are logged with detail in the read/write callbacks.
					if (exc == MODBUS_EXCEPTION_ILLEGAL_FUNCTION)
					{
						CSLOG_WARN("MODBUS hata: desteklenmeyen "
						           "fonksiyon (exception 0x01)\n");
					}
				}
			}
			else if (result == MODBUS_POLL_RECEIVING)
			{
				// Frame still arriving: re-poll soon to detect its end.
				etimer_set(&rx_timer, MODBUS_RX_GAP_TICKS);
			}
			else
			{
				// Bus idle: nothing to do.
			}
		}
	}

	PROCESS_END();
}



void modbus_process_init(void)
{
	modbus_uart_apply_baudrate();
	process_start(&modbus_process, NULL);
}

void modbus_process_poll(void)
{
	process_poll(&modbus_process);
}

void modbus_process_notify_config_changed(void)
{
	/* Aninda uygula: yeni hiz hemen devreye girer. */
	modbus_uart_apply_baudrate();

	/* CihazID acilista bir kez okunup uygulanir; web'den degistirilince
	 * de aninda uygula (API 1..247 disini reddeder, web tarafi zaten
	 * dogruluyor). Yoksa kullanici "kaydedildi" alir ama cihaz eski
	 * adresten yanitlamaya devam ederdi. */
	uint8_t id = nvram_get_modbus_device_addr();
	libmodbusrtu_modbus_set_slave_id(&s_modbus, id);
	CSLOG_WARN("MODBUS: cihaz adresi %u olarak uygulandi\r\n", (unsigned)id);
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

/*** end of file ***/
