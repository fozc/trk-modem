/*
 * test_rf_uart_bridge.c
 *
 *  Created on: Oct 11, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise the real bridge with UART and console boundaries replaced.
 */
#include "unity.h"
#include "rf_uart_bridge.h"
#include "uart_tx_test_platform.h"
#include "mock_shell.h"
#include "mock_console_logger.h"
#include <string.h>

USART_TypeDef test_uart_instances[6];
static shell_cmd_t command;
static uint32_t polls;
static uint32_t writes;
static uint32_t busy_polls;
static uint8_t last_byte;

uint32_t LL_USART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart)
{
    TEST_ASSERT_EQUAL_PTR(USART3, uart);
    polls++;
    return (polls > busy_polls) ? 1U : 0U;
}

void LL_USART_TransmitData8(USART_TypeDef *uart, uint8_t value)
{
    TEST_ASSERT_EQUAL_PTR(USART3, uart);
    TEST_ASSERT_TRUE(polls > busy_polls);
    writes++;
    last_byte = value;
}

uint32_t LL_LPUART_IsActiveFlag_TXE_TXFNF(const USART_TypeDef *uart)
{
    TEST_ASSERT_EQUAL_PTR(LPUART1, uart);
    polls++;
    return (polls > busy_polls) ? 1U : 0U;
}

void LL_LPUART_TransmitData8(USART_TypeDef *uart, uint8_t value)
{
    TEST_ASSERT_EQUAL_PTR(LPUART1, uart);
    TEST_ASSERT_TRUE(polls > busy_polls);
    writes++;
    last_byte = value;
}

static int register_command(const shell_cmd_t *value, int call_count)
{
    (void)call_count;
    command = *value;
    return 0;
}

void setUp(void)
{
    polls = 0U;
    writes = 0U;
    busy_polls = 0U;
    last_byte = 0U;
    shell_register_command_StubWithCallback(register_command);
}

void tearDown(void)
{
}

void test_bridge_boot_state_shell_registration_and_explicit_enable(void)
{
    TEST_ASSERT_FALSE(rf_uart_bridge_is_enabled());
    rf_uart_bridge_init();
    TEST_ASSERT_EQUAL_STRING("rf-bridge", command.cmd);
    TEST_ASSERT_EQUAL_UINT8(SHELL_LVL_USER, command.level);
    TEST_ASSERT_NOT_NULL(command.func);
    char name[] = "rf-bridge";
    char invalid[] = "off";
    char enable[] = "on";
    char *args[] = {name, invalid};
    TEST_ASSERT_EQUAL_INT(0, command.func(1, args));
    TEST_ASSERT_EQUAL_INT(0, command.func(2, args));
    TEST_ASSERT_FALSE(rf_uart_bridge_is_enabled());
    args[1] = enable;
    console_logger_set_enabled_Expect(false, false);
    TEST_ASSERT_EQUAL_INT(0, command.func(2, args));
    TEST_ASSERT_TRUE(rf_uart_bridge_is_enabled());
    console_logger_set_enabled_Expect(false, false);
    rf_uart_bridge_enable();
    TEST_ASSERT_TRUE(rf_uart_bridge_is_enabled());
}

void test_console_to_rf_forwards_every_byte_after_uart_is_ready(void)
{
    for (uint16_t value = 0U; 256U > value; value++)
    {
        polls = 0U;
        busy_polls = 3U;
        rf_uart_bridge_lpuart1_to_usart3((uint8_t)value);
        TEST_ASSERT_EQUAL_UINT32(4U, polls);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)value + 1U, writes);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)value, last_byte);
    }
}

void test_rf_to_console_forwards_every_byte_after_uart_is_ready(void)
{
    for (uint16_t value = 0U; 256U > value; value++)
    {
        polls = 0U;
        busy_polls = 3U;
        rf_uart_bridge_usart3_to_lpuart1((uint8_t)value);
        TEST_ASSERT_EQUAL_UINT32(4U, polls);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)value + 1U, writes);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)value, last_byte);
    }
}

/*** end of file ***/
