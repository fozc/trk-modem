/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32u3xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32u3xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rf_process.h"
#include "modbus_rtu_slave.h"
#include "modbus_process.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
#include <stdbool.h>
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_NodeTypeDef Node_GPDMA1_Channel0;
extern DMA_QListTypeDef List_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim17;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */

  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32U3xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32u3xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 0 */

  /* USER CODE END GPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 1 */

  /* USER CODE END GPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 1 global interrupt.
  */
void GPDMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 0 */

  /* USER CODE END GPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 1 */

  /* USER CODE END GPDMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 2 global interrupt.
  */
void GPDMA1_Channel2_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 0 */

  /* USER CODE END GPDMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel2);
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 1 */

  /* USER CODE END GPDMA1_Channel2_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */
	uint32_t isrflags = READ_REG(USART1->ISR);
	uint32_t cr1its   = READ_REG(USART1->CR1);

	/* --- Error flags: clear unconditionally --- */
	if (LL_USART_IsActiveFlag_PE(USART1))  { LL_USART_ClearFlag_PE(USART1);  }
	if (LL_USART_IsActiveFlag_FE(USART1))  { LL_USART_ClearFlag_FE(USART1);  }
	if (LL_USART_IsActiveFlag_NE(USART1))  { LL_USART_ClearFlag_NE(USART1);  }
	if (LL_USART_IsActiveFlag_RTO(USART1)) { LL_USART_ClearFlag_RTO(USART1); }
	if (LL_USART_IsActiveFlag_ORE(USART1)) { LL_USART_ClearFlag_ORE(USART1); }

	/* --- GSM modem RX --- */
	if (LL_USART_IsActiveFlag_RXNE_RXFNE(USART1)
		&& (cr1its & USART_CR1_RXNEIE_RXFNEIE))
	{
		uint8_t rx_byte = LL_USART_ReceiveData8(USART1);

		if (!(isrflags & (USART_ISR_FE | USART_ISR_PE)))
		{
			extern void at_engine_rx_byte(uint8_t byte);
			at_engine_rx_byte(rx_byte);
		}
	}

	/* --- DMA TX complete (TC) ---
	 * Must be handled here BEFORE HAL_UART_IRQHandler.
	 * Reason: We enable RXNEIE via LL for AT engine RX.  HAL_UART_IRQHandler
	 * treats RXNEIE as an "error-interrupt-enabled" flag.  When any error flag
	 * (FE/NE/ORE — common on GSM UART) is set at the same time, HAL enters
	 * its error-handling path and returns WITHOUT reaching the TC processing
	 * code, so HAL_UART_TxCpltCallback is never called. */
	if ((isrflags & USART_ISR_TC) && (cr1its & USART_CR1_TCIE))
	{
		LL_USART_DisableIT_TC(USART1);
		LL_USART_ClearFlag_TC(USART1);

		huart1.gState = HAL_UART_STATE_READY;
		huart1.TxISR  = NULL;

		extern void at_engine_dma_tx_complete_callback(void);
		at_engine_dma_tx_complete_callback();
	}
  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */
	uint32_t isrflags = READ_REG(USART3->ISR);
	uint32_t cr1its   = READ_REG(USART3->CR1);

	/* --- Error flags: clear unconditionally --- */
	if (LL_USART_IsActiveFlag_PE(USART3))  { LL_USART_ClearFlag_PE(USART3);  }
	if (LL_USART_IsActiveFlag_FE(USART3))  { LL_USART_ClearFlag_FE(USART3);  }
	if (LL_USART_IsActiveFlag_NE(USART3))  { LL_USART_ClearFlag_NE(USART3);  }
	if (LL_USART_IsActiveFlag_RTO(USART3)) { LL_USART_ClearFlag_RTO(USART3); }
	if (LL_USART_IsActiveFlag_ORE(USART3)) { LL_USART_ClearFlag_ORE(USART3); }

	/* --- RF module RX --- */
	if (LL_USART_IsActiveFlag_RXNE_RXFNE(USART3)
		&& (cr1its & USART_CR1_RXNEIE_RXFNEIE))
	{
		uint8_t rx_byte = LL_USART_ReceiveData8(USART3);

		if (!(isrflags & (USART_ISR_FE | USART_ISR_PE)))
		{
			rf_rx_interrupt_handler(rx_byte);
		}
	}
  /* USER CODE END USART3_IRQn 0 */
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}

/**
  * @brief This function handles UART4 global interrupt.
  */
void UART4_IRQHandler(void)
{
  /* USER CODE BEGIN UART4_IRQn 0 */
	uint32_t isrflags = READ_REG(UART4->ISR);
	uint32_t cr1its   = READ_REG(UART4->CR1);

	/* --- Error flags: clear unconditionally --- */
	if (LL_USART_IsActiveFlag_PE(UART4))  { LL_USART_ClearFlag_PE(UART4);  }
	if (LL_USART_IsActiveFlag_FE(UART4))  { LL_USART_ClearFlag_FE(UART4);  }
	if (LL_USART_IsActiveFlag_NE(UART4))  { LL_USART_ClearFlag_NE(UART4);  }
	if (LL_USART_IsActiveFlag_RTO(UART4)) { LL_USART_ClearFlag_RTO(UART4); }
	if (LL_USART_IsActiveFlag_ORE(UART4)) { LL_USART_ClearFlag_ORE(UART4); }

	/* --- Modbus RTU RX --- */
	if (LL_USART_IsActiveFlag_RXNE_RXFNE(UART4)
		&& (cr1its & USART_CR1_RXNEIE_RXFNEIE))
	{
		uint8_t rx_byte = LL_USART_ReceiveData8(UART4);

		if (!(isrflags & (USART_ISR_FE | USART_ISR_PE)))
		{
			/* Dumb RX: just buffer the byte (and, in software mode, wake the
			 * Modbus process). All framing and parsing happen in the main loop. */
			modbus_process_isr_rx_byte(rx_byte);
		}
	}

#if (MODBUS_USE_HW_RTO == 1)
	/* --- Modbus RTU frame-end (RX timeout = T3.5 idle gap) --- */
	if ((isrflags & USART_ISR_RTOF) && (cr1its & USART_CR1_RTOIE))
	{
		/* Hardware frame-end: flag the buffered frame and wake the process. */
		modbus_process_isr_rx_timeout();
	}
#endif
  /* USER CODE END UART4_IRQn 0 */
  HAL_UART_IRQHandler(&huart4);
  /* USER CODE BEGIN UART4_IRQn 1 */

  /* USER CODE END UART4_IRQn 1 */
}

/**
  * @brief This function handles UART5 global interrupt.
  */
void UART5_IRQHandler(void)
{
  /* USER CODE BEGIN UART5_IRQn 0 */
	uint32_t isrflags = READ_REG(UART5->ISR);
	uint32_t cr1its   = READ_REG(UART5->CR1);

	/* --- Error flags: clear unconditionally --- */
	if (LL_USART_IsActiveFlag_PE(UART5))  { LL_USART_ClearFlag_PE(UART5);  }
	if (LL_USART_IsActiveFlag_FE(UART5))  { LL_USART_ClearFlag_FE(UART5);  }
	if (LL_USART_IsActiveFlag_NE(UART5))  { LL_USART_ClearFlag_NE(UART5);  }
	if (LL_USART_IsActiveFlag_RTO(UART5)) { LL_USART_ClearFlag_RTO(UART5); }
	if (LL_USART_IsActiveFlag_ORE(UART5)) { LL_USART_ClearFlag_ORE(UART5); }

	/* --- RX (unused) --- */
	if (LL_USART_IsActiveFlag_RXNE_RXFNE(UART5)
		&& (cr1its & USART_CR1_RXNEIE_RXFNEIE))
	{
		uint8_t rx_byte = LL_USART_ReceiveData8(UART5);

		if (!(isrflags & (USART_ISR_FE | USART_ISR_PE)))
		{
			(void)rx_byte;
		}
	}
  return;
  /* USER CODE END UART5_IRQn 0 */
  HAL_UART_IRQHandler(&huart5);
  /* USER CODE BEGIN UART5_IRQn 1 */

  /* USER CODE END UART5_IRQn 1 */
}

/**
  * @brief This function handles LPUART1 global interrupt.
  */
void LPUART1_IRQHandler(void)
{
  /* USER CODE BEGIN LPUART1_IRQn 0 */
	uint32_t isr = LPUART1->ISR;
	uint32_t cr1 = LPUART1->CR1;

	uint32_t error_flags = isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
	if (error_flags)
	{
		LPUART1->ICR = (USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF);
	}

	/* Console RX — feed bytes to shell or xmodem */
	if ((isr & USART_ISR_RXNE_RXFNE) && (cr1 & USART_CR1_RXNEIE_RXFNEIE))
	{
		uint8_t data = (uint8_t)LPUART1->RDR;

		if (!(error_flags & (USART_ISR_FE | USART_ISR_PE)))
		{
			extern bool xmodem_is_active(void);
			if (xmodem_is_active())
			{
				extern void xmodem_rx_isr(uint8_t data);
				xmodem_rx_isr(data);
			}
			else
			{
				extern void shell_on_rx_received(uint8_t data);
				shell_on_rx_received(data);
			}
		}
	}

  /* USER CODE END LPUART1_IRQn 0 */
  /* USER CODE BEGIN LPUART1_IRQn 1 */

  /* USER CODE END LPUART1_IRQn 1 */
}

/**
  * @brief This function handles TIM17 global interrupt.
  */
void TIM17_IRQHandler(void)
{
  /* USER CODE BEGIN TIM17_IRQn 0 */
	extern void bsp_tick_handler(void);
	bsp_tick_handler();
  /* USER CODE END TIM17_IRQn 0 */
  HAL_TIM_IRQHandler(&htim17);
  /* USER CODE BEGIN TIM17_IRQn 1 */

  /* USER CODE END TIM17_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
