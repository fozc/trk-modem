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

uint8_t bms_rx_buffer[272];
uint16_t bms_rx_index = 0;

void bms_rx_interrupt_handler(uint8_t data)
{
	if (bms_rx_index < sizeof(bms_rx_buffer)){
		bms_rx_buffer[bms_rx_index++] = data;
	}else{
		// Buffer overflow, reset index
		bms_rx_index = 0;
	}
}

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


void bms_process_package()
{
	if (bms_rx_index >= BMS_FULL_MAP_FRAME_LEN)
	{
		bms_data_t bms_data;
		BMS_Init(&bms_data);

		bms_status_t status = BMS_ParseFullMapResponse(&bms_data, bms_rx_buffer, bms_rx_index);
		if (status == BMS_OK)
		{
			// Successfully parsed the BMS data
			// You can now use the bms_data structure to access the parsed values
			// For example:
			float total_voltage = BMS_GetTotalVoltage(&bms_data);
			float current = BMS_GetCurrent(&bms_data);
			float soc = BMS_GetSOC(&bms_data);
			float soh = BMS_GetSOH(&bms_data);

			// Do something with the parsed data (e.g., log it, send it over another interface, etc.)

			CSLOG("BMS Data: Total Voltage: %.2f V, Current: %.2f A, SOC: %.2f %%, SOH: %.2f %% \r\n", total_voltage, current, soc, soh);
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
		bms_data_t bms_data;
		BMS_Init(&bms_data);

		bms_status_t status = BMS_ParseSOHResponse(&bms_data, bms_rx_buffer, bms_rx_index);
		if (status == BMS_OK)
		{
			// Successfully parsed the SOH data
			float soh = BMS_GetSOH(&bms_data);

			// Do something with the parsed SOH data (e.g., log it, send it over another interface, etc.)
			CSLOG("BMS SOH: %.2f %% \r\n", soh);
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
	process_start(&bms_process, NULL);
}

