/*
 * modbus_process.c
 *
 *  Created on: Oct 26, 2025
 *      Author: fatih
 */
#include "modbus_process.h"
#include "modbus_rtu_slave.h"
#include "modbus_config.h"
#include "breaker.h"
#include "bsp.h"
#include "rtc.h"
#include "nvram.h"
#include "uart.h"
#include "gpio_defs.h"
#include "contiki.h"
#include "contiki_process.h"
#include "main.h"
#include <string.h>

/*
 * Per-line register address space. Each power line owns a contiguous block of
 * MODBUS_LINE_ADDR_STRIDE registers starting at MODBUS_HOLDING_REG_BASE. Any
 * address that falls inside this space but does not map to a live field reads
 * back as 0 (inactive lines and reserved gaps); addresses outside the space
 * raise an illegal-address exception. See MODBUS_REGISTER_MAP.md.
 */
#define MODBUS_LINE_ADDR_STRIDE   100U
#define MODBUS_LINE_ADDR_FIRST    MODBUS_HOLDING_REG_BASE
#define MODBUS_LINE_ADDR_LAST     ((uint16_t)(MODBUS_HOLDING_REG_BASE + \
                                   (MAX_POWER_LINE_COUNT * MODBUS_LINE_ADDR_STRIDE) - 1U))

/*
 * Per-line field resolution model (compile-time selectable):
 *   1 (default): fixed address map. The firmware derives each field from the
 *                line base plus a fixed offset (see MODBUS_REGISTER_MAP.md); the
 *                per-field addresses stored in NVRAM are ignored. This is the
 *                industry-standard slave behaviour and guarantees a contiguous,
 *                bulk-readable block.
 *   0          : configurable map. Field addresses are taken from NVRAM
 *                configuration, allowing them to be changed at runtime.
 */
#ifndef MODBUS_USE_FIXED_ADDR_MAP
#define MODBUS_USE_FIXED_ADDR_MAP   1
#endif

/*
 * Fixed-map register offsets from the line base (used when
 * MODBUS_USE_FIXED_ADDR_MAP == 1). FLOAT32 fields occupy two registers per phase
 * (high word first, ABCD); UINT16 fields occupy one. Live block spans offsets
 * 0..26; offsets 27..MODBUS_LINE_ADDR_STRIDE-1 are reserved (read back as 0).
 */
#define MODBUS_OFF_ARIZA_AKIMI      0U    /* FLOAT32 x3 phases -> 0..5   */
#define MODBUS_OFF_ANLIK_AKIM       6U    /* FLOAT32 x3 phases -> 6..11  */
#define MODBUS_OFF_ARIZA_SURESI     12U   /* UINT16  x3 phases -> 12..14 */
#define MODBUS_OFF_ARIZA_KALICIMI   15U   /* UINT16  x3 phases -> 15..17 */
#define MODBUS_OFF_ENERJI_VARYOK    18U   /* UINT16  x3 phases -> 18..20 */
#define MODBUS_OFF_NOMINAL_VARYOK   21U   /* UINT16  x3 phases -> 21..23 */
#define MODBUS_OFF_RF_VARYOK        24U   /* UINT16  x3 phases -> 24..26 */
#define MODBUS_OFF_LIVE_END         27U   /* first reserved offset       */
#define MODBUS_FLOAT_REG_PER_PHASE  2U    /* registers per FLOAT32 phase */

/* Modbus slave instance for the UART4 bus. Kept private so the ISR stays trivial. */
static modbus_slave_t s_modbus;

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

/**
 * @brief Extract one 16-bit word of an IEEE-754 float for Modbus transmission.
 *
 * 32-bit values are transmitted big-endian, high word first (ABCD): the field's
 * configured address holds the high word and address+1 holds the low word.
 *
 * @param[in] value          Measurement value (e.g. current in amperes).
 * @param[in] want_high_word  true for the high word, false for the low word.
 * @return The selected 16-bit word.
 */
static uint16_t modbus_float_to_word(float value, bool want_high_word)
{
    uint32_t bits = 0U;
    (void)memcpy(&bits, &value, sizeof(bits));

    if (want_high_word) {
        return (uint16_t)(bits >> 16);
    }

    return (uint16_t)(bits & 0xFFFFU);
}

/**
 * @brief Encode a fault duration (milliseconds) into one holding register.
 *
 * The duration is an unsigned 16-bit millisecond count. Negative inputs clamp
 * to 0 and values beyond UINT16_MAX clamp to UINT16_MAX.
 *
 * @param[in] value_ms Fault duration in milliseconds.
 * @return The clamped UINT16 register value.
 */
static uint16_t modbus_encode_duration_ms(float value_ms)
{
    if (value_ms <= 0.0f) {
        return 0U;
    }

    float rounded = value_ms + 0.5f;

    if (rounded >= (float)UINT16_MAX) {
        return (uint16_t)UINT16_MAX;
    }

    return (uint16_t)rounded;
}

/**
 * @brief Test whether an address lies within the per-line register space.
 * @param[in] reg_addr Requested holding-register address.
 * @return true if the address is inside [first .. last] line space.
 */
static bool modbus_addr_in_line_space(uint16_t reg_addr)
{
    return (reg_addr >= MODBUS_LINE_ADDR_FIRST) && (reg_addr <= MODBUS_LINE_ADDR_LAST);
}

#if (MODBUS_USE_FIXED_ADDR_MAP == 1)

/**
 * @brief Resolve a fixed-map register offset to a live phase measurement.
 *
 * Maps an offset within a line block (0..MODBUS_LINE_ADDR_STRIDE-1) to the
 * matching phase field. FLOAT32 fields span two registers (high word first).
 *
 * @param[in]  p_data  Live feeder data for the resolved line.
 * @param[in]  offset  Register offset from the line base.
 * @param[out] p_value Destination for the resolved value on a match.
 * @return true if the offset maps to a live field, false for reserved gaps.
 */
static bool modbus_resolve_offset(const feeder_data_t *p_data,
                                  uint16_t offset,
                                  uint16_t *p_value)
{
    bool matched = true;

    if (offset < MODBUS_OFF_ANLIK_AKIM) 
    {
        uint16_t rel = offset - MODBUS_OFF_ARIZA_AKIMI;
        uint8_t  ph  = (uint8_t)(rel / MODBUS_FLOAT_REG_PER_PHASE);
        bool     high = ((rel % MODBUS_FLOAT_REG_PER_PHASE) == 0U);
        *p_value = modbus_float_to_word(p_data->phase[ph].ariza_akimi, high);
    } 
    else if (offset < MODBUS_OFF_ARIZA_SURESI) 
    {
        uint16_t rel = offset - MODBUS_OFF_ANLIK_AKIM;
        uint8_t  ph  = (uint8_t)(rel / MODBUS_FLOAT_REG_PER_PHASE);
        bool     high = ((rel % MODBUS_FLOAT_REG_PER_PHASE) == 0U);
        *p_value = modbus_float_to_word(p_data->phase[ph].anlik_akim, high);
    } 
    else if (offset < MODBUS_OFF_ARIZA_KALICIMI) 
    {
        uint8_t ph = (uint8_t)(offset - MODBUS_OFF_ARIZA_SURESI);
        *p_value = modbus_encode_duration_ms(p_data->phase[ph].ariza_suresi);
    } 
    else if (offset < MODBUS_OFF_ENERJI_VARYOK) 
    {
        uint8_t ph = (uint8_t)(offset - MODBUS_OFF_ARIZA_KALICIMI);
        *p_value = (uint16_t)p_data->phase[ph].ariza_kalicimi;
    } 
    else if (offset < MODBUS_OFF_NOMINAL_VARYOK) 
    {
        uint8_t ph = (uint8_t)(offset - MODBUS_OFF_ENERJI_VARYOK);
        *p_value = (uint16_t)p_data->phase[ph].enerji_varyok;
    } 
    else if (offset < MODBUS_OFF_RF_VARYOK) 
    {
        uint8_t ph = (uint8_t)(offset - MODBUS_OFF_NOMINAL_VARYOK);
        *p_value = (uint16_t)p_data->phase[ph].nominal_akim_varyok;
    } 
    else if (offset < MODBUS_OFF_LIVE_END) 
    {
        uint8_t ph = (uint8_t)(offset - MODBUS_OFF_RF_VARYOK);
        *p_value = (uint16_t)p_data->phase[ph].rf_haberlesme_varyok;
    } 
    else 
    {
        /* Reserved gap (fault-log blocks, not yet mapped). */
        matched = false;
    }

    return matched;
}

/**
 * @brief Resolve a holding-register address using the fixed address map.
 *
 * The line index and offset are derived from the address arithmetically; the
 * per-field NVRAM addresses are not consulted. Addresses on inactive lines or
 * reserved gaps do not match (the caller reads them back as 0).
 *
 * @param[in]  reg_addr Requested holding-register address.
 * @param[out] p_value  Destination for the resolved value on a match.
 * @return true if the address mapped to a live value, false otherwise.
 */
static bool modbus_resolve_register(uint16_t reg_addr, uint16_t *p_value)
{
    if (!modbus_addr_in_line_space(reg_addr)) {
        return false;
    }

    uint16_t rel    = (uint16_t)(reg_addr - MODBUS_LINE_ADDR_FIRST);
    uint32_t line   = (uint32_t)(rel / MODBUS_LINE_ADDR_STRIDE);
    uint16_t offset = (uint16_t)(rel % MODBUS_LINE_ADDR_STRIDE);

    if (!modbus_is_line_in_use(line)) {
        return false;
    }

    const feeder_data_t *p_data = breaker_get_feeder_data(line);

    if (p_data == NULL) {
        return false;
    }

    return modbus_resolve_offset(p_data, offset, p_value);
}

#else /* MODBUS_USE_FIXED_ADDR_MAP == 0 : configurable NVRAM address map */

/**
 * @brief Resolve a holding-register address to a live per-line measurement.
 *
 * Walks every in-use power line and phase, comparing the requested address
 * against the per-field Modbus addresses held in configuration. Float fields
 * (fault/instant current) occupy two registers: the configured address carries
 * the high word and address+1 the low word (big-endian, ABCD). Fault duration is
 * an UINT16 millisecond count; the remaining status fields are passed through.
 * A configured address of 0 is treated as unmapped and never matches.
 *
 * @param[in]  reg_addr Requested holding-register address.
 * @param[out] p_value  Destination for the resolved value on a match.
 * @return true if the address mapped to a live value, false otherwise.
 */
static bool modbus_resolve_register(uint16_t reg_addr, uint16_t *p_value)
{
    for (uint32_t line = 0U; line < MAX_POWER_LINE_COUNT; line++) {
        if (!modbus_is_line_in_use(line)) {
            continue;
        }

        const modbus_line_config_t *p_cfg = modbus_get_line_config(line);
        const feeder_data_t        *p_data = breaker_get_feeder_data(line);

        if ((p_cfg == NULL) || (p_data == NULL)) {
            continue;
        }

        for (uint8_t ph = 0U; ph < PHASE_MAX; ph++) {
            const phase_data_t *p_phase = &p_data->phase[ph];

            /* ariza_akimi: FLOAT32 across two registers (high at addr, low at addr+1). */
            if (p_cfg->ariza_akimi[ph] != 0U) {
                if (reg_addr == p_cfg->ariza_akimi[ph]) {
                    *p_value = modbus_float_to_word(p_phase->ariza_akimi, true);
                    return true;
                }
                if (reg_addr == (uint16_t)(p_cfg->ariza_akimi[ph] + 1U)) {
                    *p_value = modbus_float_to_word(p_phase->ariza_akimi, false);
                    return true;
                }
            }

            /* anlik_akim: FLOAT32 across two registers (high at addr, low at addr+1). */
            if (p_cfg->anlik_akim[ph] != 0U) {
                if (reg_addr == p_cfg->anlik_akim[ph]) {
                    *p_value = modbus_float_to_word(p_phase->anlik_akim, true);
                    return true;
                }
                if (reg_addr == (uint16_t)(p_cfg->anlik_akim[ph] + 1U)) {
                    *p_value = modbus_float_to_word(p_phase->anlik_akim, false);
                    return true;
                }
            }

            /* ariza_suresi: UINT16 millisecond count (single register). */
            if ((p_cfg->ariza_suresi[ph] != 0U) && (reg_addr == p_cfg->ariza_suresi[ph])) {
                *p_value = modbus_encode_duration_ms(p_phase->ariza_suresi);
                return true;
            }
            if ((p_cfg->ariza_kalicimi[ph] != 0U) && (reg_addr == p_cfg->ariza_kalicimi[ph])) {
                *p_value = (uint16_t)p_phase->ariza_kalicimi;
                return true;
            }
            if ((p_cfg->enerji_varyok[ph] != 0U) && (reg_addr == p_cfg->enerji_varyok[ph])) {
                *p_value = (uint16_t)p_phase->enerji_varyok;
                return true;
            }
            if ((p_cfg->nominal_akim_varyok[ph] != 0U) && (reg_addr == p_cfg->nominal_akim_varyok[ph])) {
                *p_value = (uint16_t)p_phase->nominal_akim_varyok;
                return true;
            }
            if ((p_cfg->rf_haberlesme_varyok[ph] != 0U) && (reg_addr == p_cfg->rf_haberlesme_varyok[ph])) {
                *p_value = (uint16_t)p_phase->rf_haberlesme_varyok;
                return true;
            }
        }
    }

    return false;
}

#endif /* MODBUS_USE_FIXED_ADDR_MAP */

/*
 * RX-timeout (T3.5) idle gap in bit-times. Modbus RTU fixes T3.5 at 1.75 ms for
 * baud rates above 19200; 1.75 ms x 115200 baud ~= 202 bit-times.
 */
#define MODBUS_RTO_BIT_TIMES        202U

/* Value written to addr_modem_reset that triggers a system reset. */
#define MODBUS_MODEM_RESET_TRIGGER  1U

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
    /* Global status/command registers. */
    if (reg_addr == modbus_config_get_addr_aku_uyarisi()) {
        /* TODO: bind to the real battery-warning source once available. */
        *p_value = 23U;
        return MODBUS_REG_OK;
    }

    if (reg_addr == modbus_config_get_addr_modem_reset()) {
        /* Write-only command register: reads back as 0. */
        *p_value = 0U;
        return MODBUS_REG_OK;
    }

    /* Per-line measurement registers (addresses come from configuration). */
    if (modbus_resolve_register(reg_addr, p_value)) {
        return MODBUS_REG_OK;
    }

    /*
     * Address sits in the per-line space but maps to no live field: this covers
     * inactive lines and reserved gaps (e.g. fault-log blocks). Read back as 0 so
     * a master can read a whole line block in one window without an exception.
     */
    if (modbus_addr_in_line_space(reg_addr)) {
        *p_value = 0U;
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

    /* Only the modem-reset command register is writable. */
    if (reg_addr != modbus_config_get_addr_modem_reset()) {
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
    bsp_system_reset();

    /* bsp_system_reset() does not return; kept for a well-formed signature. */
    return MODBUS_REG_OK;
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
			modbus_poll_result_t result = libmodbusrtu_modbus_process(&s_modbus);

			if (result == MODBUS_POLL_HANDLED)
			{
				// Mirror the last Modbus exception so the web UI can report it.
				uint8_t exc =
				    libmodbusrtu_modbus_get_last_exception(&s_modbus);

				if (exc != 0U)
				{
					modbus_config_set_last_error_code(exc);
					modbus_config_set_last_error_time(rtc_get_unix_epoch());

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

