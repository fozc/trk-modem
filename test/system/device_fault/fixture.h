/*
 * fixture.h
 *
 *  Created on: Oct 20, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Temporary bench fault commands, excluded from production builds.
 */
#ifndef DEVICE_FAULT_FIXTURE_H
#define DEVICE_FAULT_FIXTURE_H

#include "shell.h"
#include "uart.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart4;

/* Only the cooperative shell and abort call paths access these values.
 * They are not accessed from an interrupt handler. */
static bool fixture_abort_error;
static bool fixture_deinit_error;
static bool fixture_irq_masked;

HAL_StatusTypeDef __real_HAL_UART_AbortTransmit(UART_HandleTypeDef *handle);
HAL_StatusTypeDef __real_HAL_DMA_DeInit(DMA_HandleTypeDef *handle);
HAL_StatusTypeDef __wrap_HAL_UART_AbortTransmit(UART_HandleTypeDef *handle);
HAL_StatusTypeDef __wrap_HAL_DMA_DeInit(DMA_HandleTypeDef *handle);
void hw_fault_fixture_init(void);

HAL_StatusTypeDef __wrap_HAL_UART_AbortTransmit(UART_HandleTypeDef *handle)
{
    const HAL_StatusTypeDef result = __real_HAL_UART_AbortTransmit(handle);
    if ((&huart4 == handle) && fixture_abort_error)
    {
        fixture_abort_error = false;
        return HAL_ERROR;
    }
    return result;
}

HAL_StatusTypeDef __wrap_HAL_DMA_DeInit(DMA_HandleTypeDef *handle)
{
    const HAL_StatusTypeDef result = __real_HAL_DMA_DeInit(handle);
    if ((huart4.hdmatx == handle) && fixture_deinit_error)
    {
        fixture_deinit_error = false;
        return HAL_ERROR;
    }
    return result;
}

static int hw_fault_command(int argc, char **argv)
{
    if ((2 != argc) || (NULL == argv))
    {
        SHELL_LOG("hwtest hardfault|timeout|recover|fail|resume|status\r\n");
        return -1;
    }
    if (0 == strcmp(argv[1], "hardfault"))
    {
        SHELL_LOG("HWTEST: triggering undefined instruction\r\n");
        __asm volatile("udf #0");
    }
    else if (0 == strcmp(argv[1], "resume"))
    {
        if (fixture_irq_masked)
        {
            NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);
            fixture_irq_masked = false;
        }
        SHELL_LOG("HWTEST: DMA IRQ restored\r\n");
    }
    else if (0 == strcmp(argv[1], "status"))
    {
        SHELL_LOG("HWTEST: uart=%u dma=%u irq=%u\r\n",
                  (unsigned)huart4.gState,
                  (unsigned)huart4.hdmatx->State,
                  (unsigned)NVIC_GetEnableIRQ(GPDMA1_Channel2_IRQn));
    }
    else if ((0 == strcmp(argv[1], "timeout")) ||
             (0 == strcmp(argv[1], "recover")) ||
             (0 == strcmp(argv[1], "fail")))
    {
        if ((HAL_UART_STATE_READY != huart4.gState) ||
            (HAL_DMA_STATE_READY != huart4.hdmatx->State))
        {
            SHELL_LOG("HWTEST: busy, not armed\r\n");
            return -1;
        }
        fixture_abort_error = (0 != strcmp(argv[1], "timeout"));
        fixture_deinit_error = (0 == strcmp(argv[1], "fail"));
        if (0U == NVIC_GetEnableIRQ(GPDMA1_Channel2_IRQn))
        {
            SHELL_LOG("HWTEST: DMA IRQ already masked, not armed\r\n");
            return -1;
        }
        NVIC_DisableIRQ(GPDMA1_Channel2_IRQn);
        fixture_irq_masked = true;
        SHELL_LOG("HWTEST: armed %s; send one Modbus request\r\n",
                  argv[1]);
    }
    else
    {
        return -1;
    }
    return 0;
}

void hw_fault_fixture_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "hwtest", .desc = "Temporary fault fixture",
        .level = SHELL_LVL_SUPER_USER, .func = hw_fault_command
    });
    CSLOG_WARN("HWTEST FIXTURE ACTIVE - NOT PRODUCTION\r\n");
}

#endif
/*** end of file ***/
