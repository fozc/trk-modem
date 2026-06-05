/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u3xx_hal.h"
#include "stm32u3xx_ll_lpuart.h"
#include "stm32u3xx_ll_rcc.h"
#include "stm32u3xx_ll_spi.h"
#include "stm32u3xx_ll_usart.h"
#include "stm32u3xx_ll_system.h"
#include "stm32u3xx_ll_gpio.h"
#include "stm32u3xx_ll_exti.h"
#include "stm32u3xx_ll_bus.h"
#include "stm32u3xx_ll_cortex.h"
#include "stm32u3xx_ll_utils.h"
#include "stm32u3xx_ll_pwr.h"
#include "stm32u3xx_ll_dma.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EWDT_FEED_Pin LL_GPIO_PIN_6
#define EWDT_FEED_GPIO_Port GPIOE
#define USB_RX_Pin LL_GPIO_PIN_0
#define USB_RX_GPIO_Port GPIOC
#define USB_TX_Pin LL_GPIO_PIN_1
#define USB_TX_GPIO_Port GPIOC
#define SENS_3V3_Pin LL_GPIO_PIN_0
#define SENS_3V3_GPIO_Port GPIOA
#define SENS_3V8_Pin LL_GPIO_PIN_1
#define SENS_3V8_GPIO_Port GPIOA
#define SENS_5V_Pin LL_GPIO_PIN_2
#define SENS_5V_GPIO_Port GPIOA
#define USER_HARD_RESET_SW_Pin LL_GPIO_PIN_3
#define USER_HARD_RESET_SW_GPIO_Port GPIOA
#define DIP_SW2_Pin LL_GPIO_PIN_4
#define DIP_SW2_GPIO_Port GPIOA
#define DIP_SW1_Pin LL_GPIO_PIN_5
#define DIP_SW1_GPIO_Port GPIOA
#define RF_IO2_Pin LL_GPIO_PIN_6
#define RF_IO2_GPIO_Port GPIOA
#define RF_IO1_Pin LL_GPIO_PIN_7
#define RF_IO1_GPIO_Port GPIOA
#define RF_TX_Pin LL_GPIO_PIN_4
#define RF_TX_GPIO_Port GPIOC
#define RF_RX_Pin LL_GPIO_PIN_5
#define RF_RX_GPIO_Port GPIOC
#define VREG_SENS_Pin LL_GPIO_PIN_0
#define VREG_SENS_GPIO_Port GPIOB
#define LED_3_Pin LL_GPIO_PIN_1
#define LED_3_GPIO_Port GPIOB
#define LED_2_Pin LL_GPIO_PIN_2
#define LED_2_GPIO_Port GPIOB
#define LED_1_Pin LL_GPIO_PIN_7
#define LED_1_GPIO_Port GPIOE
#define LED_RGB2_RED_Pin LL_GPIO_PIN_8
#define LED_RGB2_RED_GPIO_Port GPIOE
#define LED_RGB2_GREEN_Pin LL_GPIO_PIN_9
#define LED_RGB2_GREEN_GPIO_Port GPIOE
#define LED_RGB2_BLUE_Pin LL_GPIO_PIN_10
#define LED_RGB2_BLUE_GPIO_Port GPIOE
#define LED_RGB1_RED_Pin LL_GPIO_PIN_11
#define LED_RGB1_RED_GPIO_Port GPIOE
#define LED_RGB1_GREEN_Pin LL_GPIO_PIN_12
#define LED_RGB1_GREEN_GPIO_Port GPIOE
#define LED_RGB1_BLUE_Pin LL_GPIO_PIN_13
#define LED_RGB1_BLUE_GPIO_Port GPIOE
#define MODBUS_EN_FLT_Pin LL_GPIO_PIN_14
#define MODBUS_EN_FLT_GPIO_Port GPIOE
#define OVP_PV_Pin LL_GPIO_PIN_15
#define OVP_PV_GPIO_Port GPIOE
#define RF_RESET_Pin LL_GPIO_PIN_10
#define RF_RESET_GPIO_Port GPIOB
#define O_GPIO2_Pin LL_GPIO_PIN_12
#define O_GPIO2_GPIO_Port GPIOB
#define DIN1_Pin LL_GPIO_PIN_13
#define DIN1_GPIO_Port GPIOB
#define DIN2_Pin LL_GPIO_PIN_14
#define DIN2_GPIO_Port GPIOB
#define DIN3_Pin LL_GPIO_PIN_15
#define DIN3_GPIO_Port GPIOB
#define DIN4_Pin LL_GPIO_PIN_8
#define DIN4_GPIO_Port GPIOD
#define CHARGER_SCL_Pin LL_GPIO_PIN_12
#define CHARGER_SCL_GPIO_Port GPIOD
#define CHARGER_SDA_Pin LL_GPIO_PIN_13
#define CHARGER_SDA_GPIO_Port GPIOD
#define CHARGER_STAT_Pin LL_GPIO_PIN_14
#define CHARGER_STAT_GPIO_Port GPIOD
#define RELAY1_PWM_Pin LL_GPIO_PIN_8
#define RELAY1_PWM_GPIO_Port GPIOC
#define RELAY2_PWM_Pin LL_GPIO_PIN_9
#define RELAY2_PWM_GPIO_Port GPIOC
#define HEATER_PWM_Pin LL_GPIO_PIN_8
#define HEATER_PWM_GPIO_Port GPIOA
#define PWM1_Pin LL_GPIO_PIN_9
#define PWM1_GPIO_Port GPIOA
#define MODBUS_OE_Pin LL_GPIO_PIN_15
#define MODBUS_OE_GPIO_Port GPIOA
#define MODBUS_TX_Pin LL_GPIO_PIN_10
#define MODBUS_TX_GPIO_Port GPIOC
#define MODBUS_RX_Pin LL_GPIO_PIN_11
#define MODBUS_RX_GPIO_Port GPIOC
#define BMS_TX_Pin LL_GPIO_PIN_12
#define BMS_TX_GPIO_Port GPIOC
#define BMS_OE_Pin LL_GPIO_PIN_0
#define BMS_OE_GPIO_Port GPIOD
#define BMS_RX_Pin LL_GPIO_PIN_2
#define BMS_RX_GPIO_Port GPIOD
#define SPI2_CS_Pin LL_GPIO_PIN_5
#define SPI2_CS_GPIO_Port GPIOD
#define BMS_RE_Pin LL_GPIO_PIN_6
#define BMS_RE_GPIO_Port GPIOD
#define GSM_POWER_ONOFF_Pin LL_GPIO_PIN_7
#define GSM_POWER_ONOFF_GPIO_Port GPIOD
#define GSM_TX_Pin LL_GPIO_PIN_6
#define GSM_TX_GPIO_Port GPIOB
#define GSM_RX_Pin LL_GPIO_PIN_7
#define GSM_RX_GPIO_Port GPIOB
#define GSM_ONOFF_Pin LL_GPIO_PIN_8
#define GSM_ONOFF_GPIO_Port GPIOB
#define GSM_SHUTDOWM_Pin LL_GPIO_PIN_9
#define GSM_SHUTDOWM_GPIO_Port GPIOB
#define GSM_SW_RDY_Pin LL_GPIO_PIN_0
#define GSM_SW_RDY_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
