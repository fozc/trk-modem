/*
 * spic.c
 *
 *  Created on: Dec 6, 2023
 *      Author: fatih.ozcan
 */
#include "spi.h"
#include "main.h"

#define SPI_DUMMY_DATA 0xFF

#define SPI_FLASH_CS_HIGH() LL_GPIO_SetOutputPin(SPI2_CS_GPIO_Port, SPI2_CS_Pin)   /* Device deselected */
#define SPI_FLASH_CS_LOW()  LL_GPIO_ResetOutputPin(SPI2_CS_GPIO_Port, SPI2_CS_Pin) /* Device selected */

void spi_cs_high(void)
{
	SPI_FLASH_CS_HIGH();
}

void spi_cs_low(void)
{
	SPI_FLASH_CS_LOW();
}

uint8_t spi_send_byte(uint8_t data)
{
	while(!LL_SPI_IsActiveFlag_TXP(SPI2));
	LL_SPI_TransmitData8(SPI2, data);
	while(!LL_SPI_IsActiveFlag_RXP(SPI2));
	return LL_SPI_ReceiveData8(SPI2);
}

uint8_t spi_read_byte(void)
{
	return spi_send_byte(SPI_DUMMY_DATA);
}

