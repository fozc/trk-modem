/*
 * bms_reader.c
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */
#include "bms_reader.h"
#include "bms.h"
#include "bsp.h"
#include "contiki.h"
#include "main.h"
#include "gpio.h"
#include "elog.h"

uint8_t bms_rx_buffer[272];
uint16_t bms_rx_index = 0;

/* Persistent decoded snapshot. Full-map and SOH responses update disjoint
 * fields of this single record, so each parse preserves the other's data. It is
 * the source the Modbus BMS-stats block reads back (see bms_reader_get_data). */
static bms_data_t s_bms_data;

void bms_reader_get_data(bms_data_t *p_out)
{
	if (p_out == NULL) {
		return;
	}
	*p_out = s_bms_data;
}

#if 1
void bms_rx_interrupt_handler(uint8_t data)
{
	if (bms_rx_index < sizeof(bms_rx_buffer)){
		bms_rx_buffer[bms_rx_index++] = data;
	}else{
		// Buffer overflow, reset index
		bms_rx_index = 0;
	}
}
#endif
static void bms_send_buff(const uint8_t *buffer, size_t length)
{
	extern UART_HandleTypeDef huart5;

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
	uint8_t request_frame[] = { 0x81, 0x03, 0x00, 0x00, 0x00, 0x7F, 0x1B, 0xEA }; // Slave Address (0x51), Function Code (0x03), Starting Address (0x0000), Quantity of Registers (0x007F), CRC (0x1BEA)

	bms_send_buff(request_frame, sizeof(request_frame));
}

static void send_soh_request(void)
{
	uint8_t request_frame[] = { 0x81, 0x03, 0x01, 0x17, 0x00, 0x01, 0x2A, 0x32}; // Slave Address (0x51), Function Code (0x03), Starting Address (0x0117), Quantity of Registers (0x0001), CRC (0x2A32)
	bms_send_buff(request_frame, sizeof(request_frame));
}


/* Report work-state transitions and SOC threshold crossings to elog;
 * payload packing and level policy live in elog.c. */
static void bms_log_transitions(const bms_data_t *d)
{
	static bool have_prev = false;
	static uint8_t prev_work = 0U;
	static bool low20 = false;
	static bool low10 = false;

	uint8_t soc_u8 = (d->soc_percent < 0.0f) ? 0U
	               : ((d->soc_percent > 100.0f) ? 100U : (uint8_t)d->soc_percent);
	uint8_t soh_u8 = (d->soh_percent < 0.0f) ? 0U
	              : ((d->soh_percent > 100.0f) ? 100U : (uint8_t)d->soh_percent);

	if (have_prev && (d->work_state != prev_work))
	{
		elog_log_battery_state_change(ELOG_BAT_SRC_BMS, prev_work,
		                              (uint8_t)d->work_state, soc_u8, soh_u8);
	}

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

	prev_work = (uint8_t)d->work_state;
	have_prev = true;
}

void bms_process_package()
{
	if (bms_rx_index >= BMS_FULL_MAP_FRAME_LEN)
	{
		bms_status_t status = BMS_ParseFullMapResponse(&s_bms_data, bms_rx_buffer, bms_rx_index);
		if (status == BMS_OK)
		{
			// Successfully parsed the BMS data; s_bms_data now holds the latest
			// full-map snapshot (readable via bms_reader_get_data / Modbus).
			float total_voltage = BMS_GetTotalVoltage(&s_bms_data);
			float current = BMS_GetCurrent(&s_bms_data);
			float soc = BMS_GetSOC(&s_bms_data);
			float soh = BMS_GetSOH(&s_bms_data);

			(void)total_voltage;
			(void)current;
			(void)soc;
			(void)soh;

			bms_log_transitions(&s_bms_data);
		}
		else
		{
			// Handle parsing error (e.g., log the error, reset the buffer, etc.)
		}

		// Reset the buffer index for the next frame
		bms_rx_index = 0;
	}
	else if (bms_rx_index >= BMS_SOH_FRAME_LEN)
	{
		bms_status_t status = BMS_ParseSOHResponse(&s_bms_data, bms_rx_buffer, bms_rx_index);
		if (status == BMS_OK)
		{
			// Successfully parsed the SOH data (only the SOH fields of the
			// persistent snapshot are touched).
			float soh = BMS_GetSOH(&s_bms_data);
			(void)soh;

			//CSLOG("BMS SOH: %.2f %% \r\n", soh);
		}
		else
		{
			// Handle parsing error (e.g., log the error, reset the buffer, etc.)
		}

		// Reset the buffer index for the next frame
		bms_rx_index = 0;
	}
}


PROCESS(bms_process, "bms_process");
PROCESS_THREAD(bms_process, ev, data)
{
    static struct etimer timer;
    static uint8_t package_id = 0;
    PROCESS_BEGIN();

    etimer_set(&timer, 1000);


    while(1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_restart(&timer);

        bms_process_package();
        if(package_id == 0)
		{
			// Send Full Map Request
			send_full_map_request();
			package_id = 1;
		}
		else if(package_id == 1)
		{
			// Send SOH Request
			send_soh_request();
			package_id = 0;
		}
    }

    PROCESS_END();
}



void bms_reader_init(void)
{
	BMS_Init(&s_bms_data);
	process_start(&bms_process, NULL);
}

