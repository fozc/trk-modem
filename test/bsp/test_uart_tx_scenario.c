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
#include "uart.h"
#include "uart_tx_test_platform.h"
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
/*** end of file ***/
