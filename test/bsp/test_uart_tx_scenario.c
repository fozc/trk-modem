/*
 * test_uart_tx_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real UART send paths with explicit busy/ready and RS-485 boundaries.
 */
#include "unity.h"
#include "mock_elog.h"
#include "uart.h"
#include "uart_tx_test_platform.h"
#include "stm32u3xx_hal.h"

static void reset_dma_model(void);
static void log_recovery(uint8_t port, bool recovered, uint32_t uart_error,
                           uint32_t dma_error, int call_count);
#include <stddef.h>
#include <string.h>

USART_TypeDef test_uart_instances[6];
static uint32_t busy_reads;
static uint32_t poll_count;
static uint32_t tx_count;
static uint32_t tc_busy_reads;
static bool ready;
static bool complete;
static bool rs485;
static uint8_t assert_level;
static uint8_t gpio_levels[2];
static uint32_t gpio_count;
static uint8_t sent[16];
static const USART_TypeDef *expected_uart;

void uart1_send(uint8_t data);
void uart2_send(uint8_t data);
void uart3_send(uint8_t data);
void uart4_send(uint8_t data);
void uart5_send(uint8_t data);
void uart_lp_1_send(uint8_t data);

uint32_t LL_USART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart)
{
    TEST_ASSERT_EQUAL_PTR(expected_uart, uart);
    poll_count++;
    if (0U < busy_reads)
    {
        busy_reads--;
        return 0U;
    }
    ready = true;
    return 1U;
}

void LL_USART_TransmitData8(USART_TypeDef *uart, uint8_t value)
{
    TEST_ASSERT_EQUAL_PTR(expected_uart, uart);
    TEST_ASSERT_TRUE_MESSAGE(ready, "TDR written before TXE/TXFNF");
    if (rs485)
    {
        TEST_ASSERT_EQUAL_UINT32(1U, gpio_count);
        TEST_ASSERT_EQUAL_UINT8(assert_level, gpio_levels[0]);
    }
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(sent), tx_count);
    sent[tx_count++] = value;
    ready = false;
    complete = false;
    busy_reads = 2U;
}

uint32_t LL_LPUART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart)
{
    return LL_USART_IsActiveFlag_TXE_TXFNF(uart);
}

void LL_LPUART_TransmitData8(USART_TypeDef *uart, uint8_t value)
{
    LL_USART_TransmitData8(uart, value);
}

void LL_USART_ClearFlag_TC(USART_TypeDef *uart)
{
    TEST_ASSERT_EQUAL_PTR(expected_uart, uart);
    TEST_ASSERT_EQUAL_UINT32(1U, gpio_count);
    complete = false;
}

uint32_t LL_USART_IsActiveFlag_TC(const USART_TypeDef *uart)
{
    TEST_ASSERT_EQUAL_PTR(expected_uart, uart);
    TEST_ASSERT_EQUAL_UINT32(1U, gpio_count);
    if (0U < tc_busy_reads)
    {
        tc_busy_reads--;
        return 0U;
    }
    complete = true;
    return 1U;
}

void gpio_set_pin(gpio_port_t port, gpio_pin_t pin, uint8_t level)
{
    TEST_ASSERT_EQUAL_UINT8(GPIO_A, port);
    TEST_ASSERT_EQUAL_UINT8(PIN_1, pin);
    TEST_ASSERT_LESS_THAN_UINT32(2U, gpio_count);
    if (1U == gpio_count)
    {
        TEST_ASSERT_TRUE_MESSAGE(complete, "DE released before TC");
    }
    gpio_levels[gpio_count++] = level;
}

void setUp(void)
{
    reset_dma_model();
    elog_log_uart_recovery_Stub(log_recovery);
    memset(sent, 0, sizeof(sent));
    memset(gpio_levels, 0, sizeof(gpio_levels));
    busy_reads = 2U;
    poll_count = 0U;
    tx_count = 0U;
    tc_busy_reads = 2U;
    ready = false;
    complete = false;
    rs485 = false;
    assert_level = GPIO_HIGH;
    gpio_count = 0U;
    expected_uart = USART3;
}

void tearDown(void)
{
}

void test_uart_busy_first_byte_waits_before_writing_and_before_return(void)
{
    uart_send_byte(UART_3, 0xA5U);
    TEST_ASSERT_EQUAL_UINT32(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(0xA5U, sent[0]);
    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(6U, poll_count);
}

void test_uart_ready_first_byte_keeps_existing_return_contract(void)
{
    busy_reads = 0U;
    ready = true;
    uart_send_byte(UART_3, 0x5AU);
    TEST_ASSERT_EQUAL_UINT32(1U, tx_count);
    TEST_ASSERT_TRUE(ready);
}

void test_uart_buffer_preserves_all_bytes_and_order(void)
{
    const char frame[] = {0x01, 0x22, 0x33};
    uart_send_buffer(UART_3, frame, (int)sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(frame), tx_count);
    TEST_ASSERT_EQUAL_MEMORY(frame, sent, sizeof(frame));
    TEST_ASSERT_TRUE(ready);
}

static void assert_rs485(bool active_high)
{
    const uint8_t frame[] = {0x81U, 0x03U, 0x55U};
    rs485 = true;
    assert_level = active_high ? GPIO_HIGH : GPIO_LOW;
    uart_send_buffer_rs485(UART_3, GPIO_A, PIN_1, active_high,
                           frame, (uint16_t)sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(frame), tx_count);
    TEST_ASSERT_EQUAL_MEMORY(frame, sent, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(2U, gpio_count);
    TEST_ASSERT_EQUAL_UINT8(active_high ? GPIO_LOW : GPIO_HIGH,
                            gpio_levels[1]);
    TEST_ASSERT_TRUE(complete);
}

void test_rs485_active_high_waits_for_ready_and_tc_before_release(void)
{
    assert_rs485(true);
}

void test_rs485_active_low_waits_for_ready_and_tc_before_release(void)
{
    assert_rs485(false);
}

void test_uart_invalid_port_does_not_access_hardware(void)
{
    uart_send_byte(UART_PORT_MAX, 0x12U);
    TEST_ASSERT_EQUAL_UINT32(0U, poll_count);
    TEST_ASSERT_EQUAL_UINT32(0U, tx_count);
}

void test_legacy_uart_send_entries_wait_before_writing(void)
{
    void (*const senders[])(uint8_t) = {
        uart1_send, uart2_send, uart3_send, uart4_send, uart5_send
    };
    for (size_t index = 0U; index < 5U; index++)
    {
        expected_uart = &test_uart_instances[index];
        busy_reads = 2U;
        ready = false;
        senders[index]((uint8_t)index);
        TEST_ASSERT_TRUE(ready);
    }
    TEST_ASSERT_EQUAL_UINT32(5U, tx_count);
}

void test_lpuart_generic_and_legacy_entries_wait_before_writing(void)
{
    expected_uart = LPUART1;
    uart_lp_send_byte(UART_LP_1, 0x23U);
    TEST_ASSERT_TRUE(ready);
    ready = false;
    busy_reads = 2U;
    uart_lp_1_send(0x45U);
    TEST_ASSERT_TRUE(ready);
    TEST_ASSERT_EQUAL_UINT32(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(0x23U, sent[0]);
    TEST_ASSERT_EQUAL_UINT8(0x45U, sent[1]);
}


/* ---- uart_set_line_config test infrastructure ---- */
static uint32_t ll_call_order[8];
static uint32_t ll_call_count;
static uint32_t ll_written_baud;
static uint32_t ll_written_stop;

enum ll_step
{
    LL_NONE = 0,
    LL_DISABLE,
    LL_SET_BAUD,
    LL_SET_STOP,
    LL_ENABLE,
};

void LL_USART_Disable(USART_TypeDef *uart)
{
    (void)uart;
    if (ll_call_count < 8U)
    {
        ll_call_order[ll_call_count++] = LL_DISABLE;
    }
}

void LL_USART_Enable(USART_TypeDef *uart)
{
    (void)uart;
    if (ll_call_count < 8U)
    {
        ll_call_order[ll_call_count++] = LL_ENABLE;
    }
}

uint32_t LL_USART_GetOverSampling(const USART_TypeDef *uart)
{
    (void)uart;
    return 16U;
}

void LL_USART_SetBaudRate(USART_TypeDef *uart, uint32_t clock,
                          uint32_t prescaler, uint32_t sampling,
                          uint32_t baudrate)
{
    (void)uart; (void)clock; (void)prescaler; (void)sampling;
    ll_written_baud = baudrate;
    if (ll_call_count < 8U)
    {
        ll_call_order[ll_call_count++] = LL_SET_BAUD;
    }
}

void LL_USART_SetStopBitsLength(USART_TypeDef *uart, uint32_t bits)
{
    (void)uart;
    ll_written_stop = bits;
    if (ll_call_count < 8U)
    {
        ll_call_order[ll_call_count++] = LL_SET_STOP;
    }
}

uint32_t LL_RCC_GetUSARTClockFreq(uint32_t source)
{
    (void)source;
    return 64000000U;  /* valid kernel clock */
}

uint32_t LL_RCC_GetUARTClockFreq(uint32_t source)
{
    (void)source;
    return 64000000U;
}

static void reset_ll_tracking(void)
{
    ll_call_count = 0U;
    ll_written_baud = 0U;
    ll_written_stop = 0U;
    memset(ll_call_order, 0, sizeof(ll_call_order));
}

/* ---- uart_set_line_config tests (WP-J) ---- */

void test_line_config_invalid_stop_bits_no_hardware_write(void)
{
    reset_ll_tracking();
    TEST_ASSERT_FALSE(uart_set_line_config(UART_4, 115200U, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, ll_call_count);
    TEST_ASSERT_FALSE(uart_set_line_config(UART_4, 115200U, 3U));
    TEST_ASSERT_EQUAL_UINT32(0U, ll_call_count);
    TEST_ASSERT_FALSE(uart_set_line_config(UART_4, 115200U, 255U));
    TEST_ASSERT_EQUAL_UINT32(0U, ll_call_count);
}

void test_line_config_zero_baud_no_hardware_write(void)
{
    reset_ll_tracking();
    TEST_ASSERT_FALSE(uart_set_line_config(UART_4, 0U, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, ll_call_count);
}

void test_line_config_invalid_port_no_hardware_write(void)
{
    reset_ll_tracking();
    TEST_ASSERT_FALSE(uart_set_line_config(99U, 115200U, 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, ll_call_count);
}

void test_line_config_success_disable_baud_stop_enable_order(void)
{
    reset_ll_tracking();
    TEST_ASSERT_TRUE(uart_set_line_config(UART_4, 9600U, 2U));

    /* Sequence: disable, baud, stop, enable. */
    TEST_ASSERT_EQUAL_UINT32(4U, ll_call_count);
    TEST_ASSERT_EQUAL_UINT32(LL_DISABLE, ll_call_order[0]);
    TEST_ASSERT_EQUAL_UINT32(LL_SET_BAUD, ll_call_order[1]);
    TEST_ASSERT_EQUAL_UINT32(LL_SET_STOP, ll_call_order[2]);
    TEST_ASSERT_EQUAL_UINT32(LL_ENABLE,  ll_call_order[3]);

    /* Values written. */
    TEST_ASSERT_EQUAL_UINT32(9600U, ll_written_baud);
    TEST_ASSERT_EQUAL_UINT32(LL_USART_STOPBITS_2, ll_written_stop);
}

void test_line_config_stop_1_writes_stopbits_1(void)
{
    reset_ll_tracking();
    TEST_ASSERT_TRUE(uart_set_line_config(UART_4, 115200U, 1U));
    TEST_ASSERT_EQUAL_UINT32(LL_USART_STOPBITS_1, ll_written_stop);
    TEST_ASSERT_EQUAL_UINT32(115200U, ll_written_baud);
}

/* DMA recovery boundary model: the real BSP helper is linked by Ceedling. */
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart4;
static DMA_HandleTypeDef dma_handles[2];
static bool dma_request[2];
static bool channel_enabled[2];
static bool irq_enabled[2];
static bool abort_timeout;
static HAL_StatusTypeDef deinit_result;
static HAL_StatusTypeDef init_result;
static uint32_t abort_calls;
static uint32_t deinit_calls;
static uint32_t init_calls;
static uint32_t recovery_logs;
static uint8_t logged_port;
static bool logged_result;
static uint32_t logged_uart_error;
static uint32_t logged_dma_error;

static void log_recovery(uint8_t port, bool recovered, uint32_t uart_error,
                           uint32_t dma_error, int call_count)
{
    (void)call_count;
    recovery_logs++;
    logged_port = port;
    logged_result = recovered;
    logged_uart_error = uart_error;
    logged_dma_error = dma_error;
}

static void reset_dma_model(void)
{
    huart1.hdmatx = &dma_handles[0];
    huart4.hdmatx = &dma_handles[1];
    huart1.gState = HAL_UART_STATE_READY;
    huart4.gState = HAL_UART_STATE_READY;
    for (size_t index = 0U; 2U > index; index++)
    {
        dma_handles[index].State = HAL_DMA_STATE_READY;
        dma_handles[index].Parent = (0U == index) ? &huart1 : &huart4;
        dma_request[index] = false;
        channel_enabled[index] = false;
        irq_enabled[index] = true;
    }
    abort_timeout = false;
    deinit_result = HAL_OK;
    init_result = HAL_OK;
    recovery_logs = 0U;
    abort_calls = 0U;
    deinit_calls = 0U;
    init_calls = 0U;
}

static size_t dma_index(const DMA_HandleTypeDef *dma)
{
    TEST_ASSERT_TRUE((dma == &dma_handles[0]) || (dma == &dma_handles[1]));
    return (dma == &dma_handles[0]) ? 0U : 1U;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle)
{
    TEST_ASSERT_TRUE((handle == &huart1) || (handle == &huart4));
    const size_t index = dma_index(handle->hdmatx);
    abort_calls++;
    if (dma_request[index])
    {
        dma_request[index] = false;
        if (abort_timeout)
        {
            handle->hdmatx->State = HAL_DMA_STATE_ERROR;
            handle->hdmatx->ErrorCode = 0x20U;
            handle->ErrorCode = HAL_UART_ERROR_DMA;
            return HAL_TIMEOUT;
        }
        channel_enabled[index] = false;
        handle->hdmatx->State = HAL_DMA_STATE_READY;
    }
    handle->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *dma)
{
    const size_t index = dma_index(dma);
    TEST_ASSERT_FALSE(irq_enabled[index]);
    deinit_calls++;
    if (HAL_OK == deinit_result)
    {
        channel_enabled[index] = false;
        dma->State = HAL_DMA_STATE_RESET;
        dma->ErrorCode = 0U;
        dma->Parent = NULL;
    }
    return deinit_result;
}

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma)
{
    const size_t index = dma_index(dma);
    TEST_ASSERT_FALSE(irq_enabled[index]);
    TEST_ASSERT_FALSE(channel_enabled[index]);
    TEST_ASSERT_EQUAL_UINT32(HAL_DMA_STATE_RESET, dma->State);
    init_calls++;
    dma->State = (HAL_OK == init_result) ? HAL_DMA_STATE_READY :
                                         HAL_DMA_STATE_ERROR;
    return init_result;
}

static size_t irq_index(IRQn_Type irq)
{
    TEST_ASSERT_TRUE((GPDMA1_Channel1_IRQn == irq) ||
                     (GPDMA1_Channel2_IRQn == irq));
    return (GPDMA1_Channel1_IRQn == irq) ? 0U : 1U;
}

uint32_t NVIC_GetEnableIRQ(IRQn_Type irq)
{
    return irq_enabled[irq_index(irq)] ? 1U : 0U;
}
void NVIC_DisableIRQ(IRQn_Type irq)
{
    irq_enabled[irq_index(irq)] = false;
}
void NVIC_EnableIRQ(IRQn_Type irq)
{
    irq_enabled[irq_index(irq)] = true;
}

static void start_dma_model(size_t index)
{
    UART_HandleTypeDef *handle = (0U == index) ? &huart1 : &huart4;
    handle->gState = HAL_UART_STATE_BUSY_TX;
    dma_handles[index].State = HAL_DMA_STATE_BUSY;
    channel_enabled[index] = true;
    dma_request[index] = true;
}

void test_abort_timeout_recovers_each_uart_without_touching_other_channel(void)
{
    const uart_port_t ports[] = {UART_1, UART_4};
    for (size_t index = 0U; 2U > index; index++)
    {
        reset_dma_model();
        start_dma_model(0U);
        start_dma_model(1U);
        abort_timeout = true;
        TEST_ASSERT_TRUE(uart_abort_tx(ports[index]));
        TEST_ASSERT_EQUAL_UINT32(1U, deinit_calls);
        TEST_ASSERT_EQUAL_UINT32(1U, init_calls);
        TEST_ASSERT_EQUAL_UINT32(2U, abort_calls);
        TEST_ASSERT_EQUAL_UINT32(1U, recovery_logs);
        TEST_ASSERT_EQUAL_UINT8((0U == index) ? 1U : 4U, logged_port);
        TEST_ASSERT_TRUE(logged_result);
        TEST_ASSERT_EQUAL_UINT32(HAL_UART_ERROR_DMA, logged_uart_error);
        TEST_ASSERT_EQUAL_UINT32(0x20U, logged_dma_error);
        TEST_ASSERT_FALSE(channel_enabled[index]);
        TEST_ASSERT_TRUE(channel_enabled[1U - index]);
        TEST_ASSERT_TRUE(irq_enabled[index]);
        TEST_ASSERT_EQUAL_UINT32(HAL_DMA_STATE_READY, dma_handles[index].State);
        UART_HandleTypeDef *handle = (0U == index) ? &huart1 : &huart4;
        TEST_ASSERT_EQUAL_PTR(handle, dma_handles[index].Parent);
        TEST_ASSERT_EQUAL_UINT32(HAL_UART_STATE_READY, handle->gState);
    }
}

void test_dma_reset_failure_preserves_error_and_disabled_irq(void)
{
    start_dma_model(0U);
    irq_enabled[0] = false;
    abort_timeout = true;
    deinit_result = HAL_ERROR;
    TEST_ASSERT_FALSE(uart_abort_tx(UART_1));
    TEST_ASSERT_FALSE(irq_enabled[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, recovery_logs);
    TEST_ASSERT_FALSE(logged_result);
    TEST_ASSERT_TRUE(channel_enabled[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, init_calls);
    TEST_ASSERT_EQUAL_UINT32(HAL_UART_STATE_BUSY_TX, huart1.gState);
    TEST_ASSERT_EQUAL_UINT32(HAL_DMA_STATE_ERROR, dma_handles[0].State);
}

void test_dma_init_failure_does_not_force_uart_ready(void)
{
    start_dma_model(1U);
    abort_timeout = true;
    init_result = HAL_ERROR;
    TEST_ASSERT_FALSE(uart_abort_tx(UART_4));
    TEST_ASSERT_TRUE(irq_enabled[1]);
    TEST_ASSERT_EQUAL_PTR(&huart4, dma_handles[1].Parent);
    TEST_ASSERT_EQUAL_UINT32(HAL_UART_STATE_BUSY_TX, huart4.gState);
    TEST_ASSERT_EQUAL_UINT32(HAL_DMA_STATE_ERROR, dma_handles[1].State);
}

void test_successful_recovery_preserves_previously_disabled_irq(void)
{
    start_dma_model(1U);
    abort_timeout = true;
    irq_enabled[1] = false;
    TEST_ASSERT_TRUE(uart_abort_tx(UART_4));
    TEST_ASSERT_FALSE(irq_enabled[1]);
}

void test_healthy_idle_and_normal_abort_do_not_reset_dma(void)
{
    TEST_ASSERT_TRUE(uart_abort_tx(UART_1));
    TEST_ASSERT_EQUAL_UINT32(0U, abort_calls);
    start_dma_model(1U);
    TEST_ASSERT_TRUE(uart_abort_tx(UART_4));
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, deinit_calls);
}

void test_abort_rejects_unsupported_port_and_missing_dma(void)
{
    TEST_ASSERT_FALSE(uart_abort_tx(UART_3));
    TEST_ASSERT_FALSE(uart_abort_tx(UART_PORT_MAX));
    huart1.hdmatx = NULL;
    TEST_ASSERT_FALSE(uart_abort_tx(UART_1));
    TEST_ASSERT_EQUAL_UINT32(0U, abort_calls);
}

/*** end of file ***/
