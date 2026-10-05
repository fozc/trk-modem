/*
 * spi.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "spi.h"
#include "main.h"

#define SPI_DUMMY_DATA       0xFFU
#define SPI_WAIT_TIMEOUT_MS  10U
/* Bound waits even when the HAL tick cannot advance (e.g. IRQ context). */
#define SPI_WAIT_MAX_POLLS   100000U

#define SPI_FLASH_CS_HIGH() \
    LL_GPIO_SetOutputPin(SPI2_CS_GPIO_Port, SPI2_CS_Pin)
#define SPI_FLASH_CS_LOW() \
    LL_GPIO_ResetOutputPin(SPI2_CS_GPIO_Port, SPI2_CS_Pin)

static spi_transfer_status_t transfer_status = SPI_TRANSFER_OK;

void spi_cs_high(void)
{
    SPI_FLASH_CS_HIGH();
}

void spi_cs_low(void)
{
    if (SPI_TRANSFER_OK == transfer_status)
    {
        SPI_FLASH_CS_LOW();
    }
}

spi_transfer_status_t spi_get_transfer_status(void)
{
    return transfer_status;
}

void spi_clear_transfer_error(void)
{
    transfer_status = SPI_TRANSFER_OK;
}

static uint8_t spi_wait_ready(uint8_t receive)
{
    const uint32_t start = HAL_GetTick();
    uint32_t polls = SPI_WAIT_MAX_POLLS;

    while (0U != polls)
    {
        const uint32_t ready = (0U != receive)
            ? LL_SPI_IsActiveFlag_RXP(SPI2)
            : LL_SPI_IsActiveFlag_TXP(SPI2);
        if (0U != ready)
        {
            return 1U;
        }
        if ((HAL_GetTick() - start) >= SPI_WAIT_TIMEOUT_MS)
        {
            break;
        }
        polls--;
    }
    transfer_status = SPI_TRANSFER_TIMEOUT;
    spi_cs_high();
    return 0U;
}

uint8_t spi_send_byte(uint8_t data)
{
    if ((SPI_TRANSFER_OK != transfer_status) || (0U == spi_wait_ready(0U)))
    {
        return SPI_DUMMY_DATA;
    }
    LL_SPI_TransmitData8(SPI2, data);
    if (0U == spi_wait_ready(1U))
    {
        return SPI_DUMMY_DATA;
    }
    return LL_SPI_ReceiveData8(SPI2);
}

uint8_t spi_read_byte(void)
{
    return spi_send_byte(SPI_DUMMY_DATA);
}
/*** end of file ***/
