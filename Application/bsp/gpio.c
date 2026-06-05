/*
 * gpio.c
 *
 *  Created on: Dec 19, 2023
 *      Author: fatih.ozcan
 *
 *
 *  GPIO API Version 1.0.1
 */
#include "gpio.h"
#include "main.h"


static const uint16_t pin_map[] = {
		[PIN_0] = LL_GPIO_PIN_0,
		[PIN_1] = LL_GPIO_PIN_1,
		[PIN_2] = LL_GPIO_PIN_2,
		[PIN_3] = LL_GPIO_PIN_3,
		[PIN_4] = LL_GPIO_PIN_4,
		[PIN_5] = LL_GPIO_PIN_5,
		[PIN_6] = LL_GPIO_PIN_6,
		[PIN_7] = LL_GPIO_PIN_7,
		[PIN_8] = LL_GPIO_PIN_8,
		[PIN_9] = LL_GPIO_PIN_9,
		[PIN_10] = LL_GPIO_PIN_10,
		[PIN_11] = LL_GPIO_PIN_11,
		[PIN_12] = LL_GPIO_PIN_12,
		[PIN_13] = LL_GPIO_PIN_13,
		[PIN_14] = LL_GPIO_PIN_14,
		[PIN_15] = LL_GPIO_PIN_15
};

static const uint16_t pin_mode_map[] = {
		[GPIO_INPUT]     = LL_GPIO_MODE_INPUT,
		[GPIO_OUTPUT]    = LL_GPIO_MODE_OUTPUT,
		[GPIO_ALTERNATE] = LL_GPIO_MODE_ALTERNATE,
		[GPIO_ANALOG]    = LL_GPIO_MODE_ANALOG,

};

/**
  * @brief  This function converts a GPIO enum value to
  *         MCU specific port address.
  * @param  port : one of the gpio port
  * @param  val : 16-bit pin states
  * @retval None
  */
static inline GPIO_TypeDef * port_to_stm32(uint8_t port)
{
	switch(port)
	{
	#if defined (GPIOA)
	    case GPIO_A:
	        return GPIOA;
	#endif

	#if defined (GPIOB)
	    case GPIO_B:
	        return GPIOB;
	#endif

	#if defined (GPIOC)
	    case GPIO_C:
	        return GPIOC;
	#endif

	#if defined (GPIOD)
	    case GPIO_D:
	        return GPIOD;
	#endif

	#if defined (GPIOE)
	    case GPIO_E:
	        return GPIOE;
	#endif

	#if defined (GPIOF)
	    case GPIO_F:
	        return GPIOF;
	#endif

	#if defined (GPIOG)
	    case GPIO_G:
	        return GPIOG;
	#endif

#if defined (GPIOH)
    case GPIO_H:
        return GPIOH;
#endif
	}


	while(1){
		//Stuck the cpu here
	}

	return GPIOA;
}

/**
  * @brief  This function set a value to an GPIO port
  * @param  port : one of the gpio port
  * @param  val : 16-bit pin states
  * @retval None
  */
void gpio_set_port(uint8_t port, uint8_t val)
{
	LL_GPIO_WriteOutputPort(port_to_stm32(port), val);
}

/**
  * @brief  This function set a value to an IO
  * @param  port : one of the gpio port
  * @param  pin : one of the gpio pin
  * @param  val : 0 to set to low, any other value to set to high
  * @retval None
  */
void gpio_set_pin(uint8_t port, uint8_t pin, uint8_t val)
{
	if(val)
		LL_GPIO_SetOutputPin(port_to_stm32(port), pin_map[pin]);
	else
		LL_GPIO_ResetOutputPin(port_to_stm32(port), pin_map[pin]);
}

/**
  * @brief  This function toggle value of an IO
  * @param  port : one of the gpio port
  * @param  pin : one of the gpio pin
  * @retval None
  */
void gpio_toggle_pin(uint8_t port, uint8_t pin)
{
	LL_GPIO_TogglePin(port_to_stm32(port), pin_map[pin]);
}

/**
  * @brief  This function read the value of an IO
  * @param  port : one of the gpio port
  * @param  pin : one of the gpio pin
  * @retval The pin state (LOW or HIGH)
  */
uint8_t gpio_read_pin(uint8_t port, uint8_t pin)
{
	return  LL_GPIO_IsInputPinSet(port_to_stm32(port), pin_map[pin]);
}

/**
  * @brief  This function read the value of an GPIO port
  * @param  port : one of the gpio port
  * @retval 16-bit pin states
  */
uint16_t gpio_read_port(uint8_t port)
{
	return LL_GPIO_ReadInputPort(port_to_stm32(port));
}


/**
  * @brief  This function Configure gpio mode for a
  * dedicated pin on dedicated port. I/O mode can be Input mode,
  * General purpose output, Alternate function mode or Analog.
  * @param  port : one of the gpio port
  * @param  pin : one of the gpio pin
  *         mode: 	GPIO_MODE_INPUT
					GPIO_MODE_INPUT
					GPIO_MODE_OUTPUT
					GPIO_MODE_ALTERNATE
					GPIO_MODE_ANALOG
  * @retval None
  */
void gpio_set_pin_mode(uint8_t port, uint8_t pin, uint8_t mode)
{
	LL_GPIO_SetPinMode(port_to_stm32(port), pin_map[pin], pin_mode_map[mode]);
}
