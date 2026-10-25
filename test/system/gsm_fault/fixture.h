/*
 * fixture.h
 *
 *  Created on: Oct 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Temporary GSM bench fault injection, excluded from production.
 */
#ifndef GSM_FAULT_FIXTURE_H
#define GSM_FAULT_FIXTURE_H

#include "shell.h"
#include "gsm_init.h"
#include "gsm_engine.h"
#include <stdatomic.h>

typedef enum
{
    GSM_FAULT_NONE,
    GSM_FAULT_SILENT,
    GSM_FAULT_HOLD,
    GSM_FAULT_PROMPT,
    GSM_FAULT_ACK,
    GSM_FAULT_DMA_BUSY,
    GSM_FAULT_INIT,
    GSM_FAULT_STALL
} gsm_fault_mode_t;

/* Fault modes are owned by cooperative main context. Only the completion
 * suppression token crosses main/ISR; it carries no associated payload. */
static gsm_fault_mode_t fault_mode;
static uint32_t fault_until;
static uint16_t fault_discarded;
static uint8_t fault_errors;
static _Atomic uint32_t fault_drop_tc;
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
               "ISR fault token must be lock-free");

void gsm_fault_age_liveness(void);
void gsm_fault_age_busy(void);
bool gsm_fault_stall_process(void);
void gsm_fault_fixture_init(void);
HAL_StatusTypeDef __real_HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t size);
HAL_StatusTypeDef __wrap_HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t size);

bool gsm_fault_drop_completion(void)
{
    return atomic_exchange_explicit(&fault_drop_tc, 0U,
                                     memory_order_relaxed) != 0U;
}

bool gsm_fault_stall_process(void)
{
    return fault_mode == GSM_FAULT_STALL;
}

bool gsm_fault_pause_rx(void)
{
    if ((fault_mode == GSM_FAULT_SILENT) || (fault_mode == GSM_FAULT_HOLD))
    {
        if ((int32_t)(bsp_get_tick() - fault_until) >= 0)
        {
            fault_mode = GSM_FAULT_NONE;
            CSLOG_WARN("GSMTEST: RX fault expired\r\n");
            return false;
        }
        if (fault_mode == GSM_FAULT_HOLD)
        {
            return true;
        }
    }
    else if (fault_mode == GSM_FAULT_PROMPT)
    {
        if ((at_engine.expected_response_len != 4U) ||
            (memcmp(at_engine.expected_response, "\r\n> ", 4U) != 0))
        {
            return false;
        }
    }
    else if (fault_mode == GSM_FAULT_ACK)
    {
        if (!at_engine.is_data_mode)
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    uint8_t byte;
    while (rbuff_read_safe(&rx_ringbuf, &byte))
    {
        fault_discarded++;
    }
    const uint16_t required = (fault_mode == GSM_FAULT_PROMPT) ? 4U : 6U;
    if (((fault_mode == GSM_FAULT_PROMPT) ||
         (fault_mode == GSM_FAULT_ACK)) && (fault_discarded >= required))
    {
        CSLOG_WARN("GSMTEST: dropped response mode=%u bytes=%u\r\n",
                   (unsigned)fault_mode, (unsigned)fault_discarded);
        fault_mode = GSM_FAULT_NONE;
    }
    return true;
}

void gsm_fault_override_result(void)
{
    if ((fault_mode == GSM_FAULT_INIT) &&
        (at_engine.result == AT_ENGINE_RESULT_OK) &&
        (strncmp((const char *)at_engine.cmd, "AT+CGDCONT=", 11U) == 0))
    {
        static const char error[] = "\r\nERROR\r\n";
        memcpy(at_engine.response_buffer, error, sizeof(error));
        at_engine.response_buffer_len = sizeof(error) - 1U;
        at_engine.result = AT_ENGINE_RESULT_ERROR;
        fault_errors++;
        CSLOG_WARN("GSMTEST: init error injected count=%u\r\n",
                   (unsigned)fault_errors);
    }
}

HAL_StatusTypeDef __wrap_HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t size)
{
    if ((handle->Instance == USART1) && (fault_mode == GSM_FAULT_DMA_BUSY))
    {
        fault_mode = GSM_FAULT_NONE;
        CSLOG_WARN("GSMTEST: HAL DMA busy injected\r\n");
        return HAL_BUSY;
    }
    return __real_HAL_UART_Transmit_DMA(handle, data, size);
}

static int gsm_fault_arm(const char *action)
{
    if (at_engine_is_busy() || gsm_is_busy())
    {
        SHELL_LOG("GSMTEST: busy, not armed\r\n");
        return -1;
    }
    fault_discarded = 0U;
    fault_errors = 0U;
    if (strcmp(action, "silent") == 0)
    {
        fault_mode = GSM_FAULT_SILENT;
        fault_until = bsp_get_tick() + 15000U;
    }
    else if (strcmp(action, "hold") == 0)
    {
        fault_mode = GSM_FAULT_HOLD;
        fault_until = bsp_get_tick() + 12000U;
    }
    else if (strcmp(action, "prompt") == 0)
    {
        fault_mode = GSM_FAULT_PROMPT;
    }
    else if (strcmp(action, "ack") == 0)
    {
        fault_mode = GSM_FAULT_ACK;
    }
    else if (strcmp(action, "dmabusy") == 0)
    {
        fault_mode = GSM_FAULT_DMA_BUSY;
    }
    else if (strcmp(action, "tc") == 0)
    {
        atomic_store_explicit(&fault_drop_tc, 1U, memory_order_relaxed);
    }
    else if (strcmp(action, "initfail") == 0)
    {
        fault_mode = GSM_FAULT_INIT;
        gsm_reset_process_old();
    }
    else if ((strcmp(action, "stall") == 0) ||
             (strcmp(action, "busy") == 0))
    {
        fault_mode = GSM_FAULT_STALL;
        if (strcmp(action, "busy") == 0)
        {
            gsm_fault_age_busy();
        }
        else
        {
            gsm_fault_age_liveness();
        }
    }
    else
    {
        return -1;
    }
    return 0;
}

static int gsm_fault_command(int argc, char **argv)
{
    if (argc != 2)
    {
        SHELL_LOG("gsmtest status|resume|silent|hold|prompt|ack|dmabusy|tc"
                  "|initfail|stall|age|busy|agebusy\r\n");
        return -1;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        SHELL_LOG("GSMTEST: mode=%u discarded=%u errors=%u txdone=%u\r\n",
            (unsigned)fault_mode, (unsigned)fault_discarded,
            (unsigned)fault_errors, (unsigned)at_engine_is_tx_done());
        return 0;
    }
    if (strcmp(argv[1], "age") == 0)
    {
        gsm_fault_age_liveness();
    }
    else if (strcmp(argv[1], "agebusy") == 0)
    {
        gsm_fault_age_busy();
    }
    else if (strcmp(argv[1], "resume") == 0)
    {
        fault_mode = GSM_FAULT_NONE;
        atomic_store_explicit(&fault_drop_tc, 0U, memory_order_relaxed);
        at_engine_cancel();
        gsm_set_free();
        gsm_process_init_old();
    }
    else if (gsm_fault_arm(argv[1]) != 0)
    {
        return -1;
    }
    SHELL_LOG("GSMTEST: armed %s\r\n", argv[1]);
    return 0;
}

void gsm_fault_fixture_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "gsmtest", .desc = "Temporary GSM fault fixture",
        .level = SHELL_LVL_SUPER_USER, .func = gsm_fault_command
    });
    CSLOG_WARN("GSMTEST FIXTURE ACTIVE - NOT PRODUCTION\r\n");
}

#endif
/*** end of file ***/
