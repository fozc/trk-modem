/*
 * gpio.h
 *
 *  Created on: Dec 19, 2023
 *      Author: fatih.ozcan
 */

#ifndef BSP_GPIO_H_
#define BSP_GPIO_H_

#include "gpio_defs.h"
#include <stdint.h>

typedef uint8_t gpio_port_t;
typedef uint8_t gpio_pin_t;

enum
{
	GPIO_A = 0,
	GPIO_B = 1,
	GPIO_C = 2,
	GPIO_D = 3,
	GPIO_E = 4,
	GPIO_F = 5,
	GPIO_G = 6,
	GPIO_H = 7
};

enum
{
	PIN_0 = 0,
	PIN_1 = 1,
	PIN_2 = 2,
	PIN_3 = 3,
	PIN_4 = 4,
	PIN_5 = 5,
	PIN_6 = 6,
	PIN_7 = 7,
	PIN_8 = 8,
	PIN_9 = 9,
	PIN_10 = 10,
	PIN_11 = 11,
	PIN_12 = 12,
	PIN_13 = 13,
	PIN_14 = 14,
	PIN_15 = 15
};


#define GPIO_LOW  0
#define GPIO_HIGH 1

enum
{
	GPIO_INPUT = 0,
	GPIO_HIGHZ = GPIO_INPUT,
	GPIO_OUTPUT = 1,
	GPIO_ALTERNATE = 2,
	GPIO_ANALOG = 3
};


typedef struct
{
	gpio_port_t port;
	gpio_pin_t pin;
}io_pin_t;


void gpio_set_port(gpio_port_t port, uint8_t val);
void gpio_set_pin(gpio_port_t port, gpio_pin_t pin, uint8_t val);
void gpio_toggle_pin(gpio_port_t port, gpio_pin_t pin);
uint8_t gpio_read_pin(gpio_port_t port, gpio_pin_t pin);
uint16_t gpio_read_port(gpio_port_t port);
void gpio_set_pin_mode(gpio_port_t port, gpio_pin_t pin, uint8_t mode);

#endif /* BSP_GPIO_H_ */
