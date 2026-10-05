/*
 * spi.h
 *
 *  Created on: Dec 6, 2023
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef LIBS_SPI_H_
#define LIBS_SPI_H_

#include <stdint.h>

typedef enum
{
    SPI_TRANSFER_OK = 0,
    SPI_TRANSFER_TIMEOUT
} spi_transfer_status_t;

/* Errors remain latched across CS transactions. No automatic retry/reset.
 * Byte-returning calls yield 0xFF on error; this is also valid payload,
 * so callers must check status before accepting data. */
spi_transfer_status_t spi_get_transfer_status(void);
/* Only clear at an explicit peripheral/device reinitialization boundary. */
void spi_clear_transfer_error(void);


void spi_cs_high(void);
void spi_cs_low(void);
uint8_t spi_send_byte(uint8_t data);
uint8_t spi_read_byte(void);

#endif /* LIBS_SPI_H_ */
