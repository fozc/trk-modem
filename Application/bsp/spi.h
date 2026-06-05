/*
 * spi.h
 *
 *  Created on: Dec 6, 2023
 *      Author: fatih
 */

#ifndef LIBS_SPI_H_
#define LIBS_SPI_H_

#include <stdint.h>


void spi_cs_high(void);
void spi_cs_low(void);
uint8_t spi_send_byte(uint8_t data);
uint8_t spi_read_byte(void);

#endif /* LIBS_SPI_H_ */
