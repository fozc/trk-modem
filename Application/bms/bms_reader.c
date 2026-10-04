/*
 * bms_reader.c
 *
 *  Created on: Aug 1, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#include "bms_reader.h"
#include "bms.h"
#include "bsp.h"
#include "contiki.h"
#include "main.h"
#include "gpio.h"
#include "elog.h"
#include <string.h>

#define BMS_DATA_MAX_AGE_MS   5000U

static uint8_t bms_rx_buffer[272];
/* Main accesses RX state only while the sole writer's IRQ is disabled. */
static volatile uint32_t bms_rx_index;
static uint16_t expected_length;
static uint8_t package_id;
static uint32_t last_full_map_tick;
static uint32_t last_soh_tick;
PROCESS_NAME(bms_process);

/* Persistent decoded snapshot. Full-map and SOH responses update disjoint
 * fields of this single record, so each parse preserves the other's data. It is
 * the source the Modbus BMS-stats block reads back (see bms_reader_get_data). */
static bms_data_t s_bms_data;

static void bms_expire_data(void)
{
    const uint32_t now = bsp_get_tick();
    if ((now - last_full_map_tick) >= BMS_DATA_MAX_AGE_MS)
    {
        s_bms_data.is_data_valid = false;
    }
    if ((now - last_soh_tick) >= BMS_DATA_MAX_AGE_MS)
    {
        s_bms_data.is_soh_valid = false;
    }
}

static void bms_prepare_request(uint16_t length)
{
    const uint32_t irq_enabled = NVIC_GetEnableIRQ(UART5_IRQn);
    NVIC_DisableIRQ(UART5_IRQn);
    bms_rx_index = 0U;
    expected_length = length;
    if (0U != irq_enabled)
    {
        NVIC_EnableIRQ(UART5_IRQn);
    }
}

void bms_reader_get_data(bms_data_t *p_out)
{
	if (p_out == NULL) {
		return;
	}
    bms_expire_data();
	*p_out = s_bms_data;
}

#if 1
void bms_rx_interrupt_handler(uint8_t data)
{
    const uint32_t count = bms_rx_index;
    if (count < sizeof(bms_rx_buffer))
    {
        bms_rx_buffer[count] = data;
        bms_rx_index = count + 1U;
        if (((count + 1U) == BMS_SOH_FRAME_LEN) ||
            ((count + 1U) == BMS_FULL_MAP_FRAME_LEN))
        {
            process_poll(&bms_process);
        }
    }
    else
    {
        /* Latch overflow until the current request is discarded. */
        bms_rx_index = sizeof(bms_rx_buffer) + 1U;
    }
}
#endif
static void bms_send_buff(const uint8_t *buffer, size_t length)
{

    gpio_set_pin(BMS_OE_BSP_GPIO, BMS_OE_BSP_PIN, GPIO_HIGH);
    gpio_set_pin(BMS_RE_BSP_GPIO, BMS_RE_BSP_PIN, GPIO_HIGH);

    /* Clear any stale transmission-complete flag before starting. */
    LL_USART_ClearFlag_TC(UART5);

    for (uint16_t i = 0U; i < length; i++) {
        LL_USART_TransmitData8(UART5, buffer[i]);
        while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5)) {
            /* Wait until the data register can accept the next byte. */
        }
    }

    /* Wait for the last byte to be fully shifted out before releasing the bus. */
    while (!LL_USART_IsActiveFlag_TC(UART5)) {
        /* Wait for transmission-complete. */
    }

    gpio_set_pin(BMS_OE_BSP_GPIO, BMS_OE_BSP_PIN, GPIO_LOW);
    gpio_set_pin(BMS_RE_BSP_GPIO, BMS_RE_BSP_PIN, GPIO_LOW);
}


static void send_full_map_request(void)
{
    bms_prepare_request(BMS_FULL_MAP_FRAME_LEN);
    /* Address 0x81, FC03, start 0, 127 registers; CRC low byte first. */
    const uint8_t request_frame[] =
    {
        0x81U, 0x03U, 0x00U, 0x00U, 0x00U, 0x7FU, 0x1BU, 0xEAU
    };
    bms_send_buff(request_frame, sizeof(request_frame));
}

static void send_soh_request(void)
{
    bms_prepare_request(BMS_SOH_FRAME_LEN);
    /* Address 0x81, FC03, start 0x0117, one register. */
    const uint8_t request_frame[] =
    {
        0x81U, 0x03U, 0x01U, 0x17U, 0x00U, 0x01U, 0x2AU, 0x32U
    };
    bms_send_buff(request_frame, sizeof(request_frame));
}


/* Report work-state transitions and SOC threshold crossings to elog;
 * payload packing and level policy live in elog.c. */
static void bms_log_transitions(const bms_data_t *d)
{
	static bool low20 = false;
	static bool low10 = false;

	uint8_t soc_u8 = (d->soc_percent < 0.0f) ? 0U
	               : ((d->soc_percent > 100.0f) ? 100U : (uint8_t)d->soc_percent);
	uint8_t soh_u8 = (d->soh_percent < 0.0f) ? 0U
	              : ((d->soh_percent > 100.0f) ? 100U : (uint8_t)d->soh_percent);

	/* Yalnizca batarya dusuk seviye kayitlari tutulur: SOC esikleri.
	 * Work-state gecisleri (sarj/desarj hukumleri) loglanmaz. */

	/* SOC thresholds with hysteresis: set at <=20/<=10, clear at >=25/>=15. */
	if ((!low20 && (d->soc_percent <= 20.0f)) || (low20 && (d->soc_percent >= 25.0f)))
	{
		bool set = (d->soc_percent <= 20.0f);
		if (set != low20)
		{
			low20 = set;
			elog_log_battery_soc_threshold(20U, set, soc_u8, soh_u8);
		}
	}

	if ((!low10 && (d->soc_percent <= 10.0f)) || (low10 && (d->soc_percent >= 15.0f)))
	{
		bool set = (d->soc_percent <= 10.0f);
		if (set != low10)
		{
			low10 = set;
			elog_log_battery_soc_threshold(10U, set, soc_u8, soh_u8);
		}
	}
}

static void bms_process_package(void)
{
    uint8_t frame[BMS_FULL_MAP_FRAME_LEN];
    const uint16_t length = expected_length;
    bms_expire_data();
    if (0U == length)
    {
        return;
    }

    const uint32_t irq_enabled = NVIC_GetEnableIRQ(UART5_IRQn);
    NVIC_DisableIRQ(UART5_IRQn);
    const uint32_t count = bms_rx_index;
    if (count == length)
    {
        memcpy(frame, bms_rx_buffer, length);
    }
    if (count >= length)
    {
        bms_rx_index = 0U;
        expected_length = 0U;
    }
    if (0U != irq_enabled)
    {
        NVIC_EnableIRQ(UART5_IRQn);
    }

    if (count != length)
    {
        return;
    }
    if (BMS_FULL_MAP_FRAME_LEN == length)
    {
        if (BMS_OK == BMS_ParseFullMapResponse(&s_bms_data, frame, length))
        {
            last_full_map_tick = bsp_get_tick();
            bms_log_transitions(&s_bms_data);
        }
    }
    else if (BMS_SOH_FRAME_LEN == length)
    {
        if (BMS_OK == BMS_ParseSOHResponse(&s_bms_data, frame, length))
        {
            last_soh_tick = bsp_get_tick();
        }
    }
    else
    {
        /* Only the two existing request lengths are issued. */
    }
}

PROCESS(bms_process, "bms_process");
PROCESS_THREAD(bms_process, ev, data)
{
    (void)data;
    (void)ev;

    static struct etimer timer;
    PROCESS_BEGIN();

    etimer_set(&timer, CLOCK_SECOND);


    for (;;)
    {
        PROCESS_WAIT_EVENT();
        bms_process_package();
        if (!etimer_expired(&timer))
        {
            continue;
        }
        etimer_restart(&timer);
        /* A partial response expires before the next request is issued. */
        if (0U == package_id)
        {
            send_full_map_request();
            package_id = 1U;
        }
        else
        {
            send_soh_request();
            package_id = 0U;
        }
    }

    PROCESS_END();
}



void bms_reader_init(void)
{
    bms_prepare_request(0U);
    package_id = 0U;
    last_full_map_tick = 0U;
    last_soh_tick = 0U;
	BMS_Init(&s_bms_data);
	process_start(&bms_process, NULL);
}

/*** end of file ***/
