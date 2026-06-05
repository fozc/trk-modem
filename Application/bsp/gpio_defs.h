/*
 * gpio_bsp.h
 *
 *  Created on: Dec 29, 2023
 *      Author: fatih
 */

#ifndef BSP_GPIO_DEFS_H_
#define BSP_GPIO_DEFS_H_

#include "gpio.h"

#define FLASH_CS_GPIO     GPIO_A
#define FLASH_CS_PIN      PIN_4

/* --- LEDs --- */
#define LED1_GPIO                GPIO_E
#define LED1_PIN                 PIN_7
#define LED2_GPIO                GPIO_B
#define LED2_PIN                 PIN_2
#define LED3_GPIO                GPIO_B
#define LED3_PIN                 PIN_1

#define LED_RGB1_R_GPIO          GPIO_E
#define LED_RGB1_R_PIN           PIN_11
#define LED_RGB1_G_GPIO          GPIO_E
#define LED_RGB1_G_PIN           PIN_12
#define LED_RGB1_B_GPIO          GPIO_E
#define LED_RGB1_B_PIN           PIN_13

#define LED_RGB2_R_GPIO          GPIO_E
#define LED_RGB2_R_PIN           PIN_8
#define LED_RGB2_G_GPIO          GPIO_E
#define LED_RGB2_G_PIN           PIN_9
#define LED_RGB2_B_GPIO          GPIO_E
#define LED_RGB2_B_PIN           PIN_10

/* --- External Watchdog --- */
#define EXT_WDT_KICK_GPIO       GPIO_E
#define EXT_WDT_KICK_PIN        PIN_6

/* --- DIP Switches & Button --- */
#define DIP_SW1_GPIO             GPIO_A
#define DIP_SW1_PIN              PIN_5
#define DIP_SW2_GPIO             GPIO_A
#define DIP_SW2_PIN              PIN_4
#define USER_RESET_SW_GPIO       GPIO_A
#define USER_RESET_SW_PIN        PIN_3

/* --- Digital Inputs --- */
#define DIN1_BSP_GPIO            GPIO_B
#define DIN1_BSP_PIN             PIN_13
#define DIN2_BSP_GPIO            GPIO_B
#define DIN2_BSP_PIN             PIN_14
#define DIN3_BSP_GPIO            GPIO_B
#define DIN3_BSP_PIN             PIN_15
#define DIN4_BSP_GPIO            GPIO_D
#define DIN4_BSP_PIN             PIN_8

/* --- GSM Module --- */
#define GSM_POWER_GPIO           GPIO_D
#define GSM_POWER_PIN            PIN_7
#define GSM_ONOFF_BSP_GPIO       GPIO_B
#define GSM_ONOFF_BSP_PIN        PIN_8
#define GSM_SHUTDOWN_GPIO        GPIO_B
#define GSM_SHUTDOWN_PIN         PIN_9
#define GSM_SW_RDY_BSP_GPIO      GPIO_E
#define GSM_SW_RDY_BSP_PIN       PIN_0

/* --- RF Module --- */
#define RF_RESET_BSP_GPIO        GPIO_B
#define RF_RESET_BSP_PIN         PIN_10
#define RF_IO1_BSP_GPIO          GPIO_A
#define RF_IO1_BSP_PIN           PIN_7
#define RF_IO2_BSP_GPIO          GPIO_A
#define RF_IO2_BSP_PIN           PIN_6

/* --- RS-485 Modbus --- */
#define MODBUS_OE_BSP_GPIO       GPIO_A
#define MODBUS_OE_BSP_PIN        PIN_15
#define MODBUS_EN_FLT_BSP_GPIO   GPIO_E
#define MODBUS_EN_FLT_BSP_PIN    PIN_14

/* --- RS-485 BMS --- */
#define BMS_OE_BSP_GPIO          GPIO_D
#define BMS_OE_BSP_PIN           PIN_0
#define BMS_RE_BSP_GPIO          GPIO_D
#define BMS_RE_BSP_PIN           PIN_6

/* --- Charger --- */
#define CHARGER_EN_GPIO          GPIO_C
#define CHARGER_EN_PIN           PIN_6
#define CHARGER_FET_GPIO         GPIO_C
#define CHARGER_FET_PIN          PIN_7
#define CHARGER_STAT_BSP_GPIO    GPIO_D
#define CHARGER_STAT_BSP_PIN     PIN_14
#define CHARGER_INT_BSP_GPIO     GPIO_D
#define CHARGER_INT_BSP_PIN      PIN_15

/* --- SPI Flash CS --- */
#define FLASH_CS_BSP_GPIO        GPIO_D
#define FLASH_CS_BSP_PIN         PIN_5

/* --- Analog Sense --- */
#define VREG_SENS_BSP_GPIO       GPIO_B
#define VREG_SENS_BSP_PIN        PIN_0
#define OVP_PV_BSP_GPIO          GPIO_E
#define OVP_PV_BSP_PIN           PIN_15

#endif /* BSP_GPIO_DEFS_H_ */
