/*
 * test_modbus_config_apply_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * WP-J config/apply regressions: stop-bit change applies when baud
 * unchanged; invalid values rejected; unchanged config does no work;
 * baud+stop-bit change together; abort gate protects both.
 */
#undef NO_SHELL_LOG
#include "unity.h"
#include "mock_bsp.h"
#include "mock_power_board_control.h"
#include "mock_elog.h"
#include "mock_shell.h"

TEST_SOURCE_FILE("power_board_scp.c")
TEST_SOURCE_FILE("contiki_replay_scheduler.c")
TEST_SOURCE_FILE("cp56time2a.c")
#include "mock_modbus_config.h"
#include "mock_relay.h"
#include "mock_modbus_system_stats.h"
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_breaker.h"
#include "mock_rf.h"
#include "mock_nvram.h"
#include "mock_fault_log.h"
#include "mock_reboot.h"
#include "mock_uart.h"
#include "mock_rtc.h"
#include "gpio_defs.h"
#include "modbus_rtu_slave.h"
#include "modbus_power_stats.h"
#include "modbus_system_stats.h"

#define MODBUS_TX_USE_DMA 1
uint32_t HAL_GetTick(void);
#include "../../Application/modbus_process.c"
#include "../../Application/modbus_line_data.c"

static uint32_t fake_tick;
static uint8_t response[64];
static uint16_t response_len;
static uint32_t line_config_calls;
static uint32_t last_config_baud;
static uint8_t last_config_stop;
static bool line_config_result;

UART_HandleTypeDef huart4;
static DMA_HandleTypeDef dma_tx_handle;

uint32_t HAL_GetTick(void) { return fake_tick; }
clock_time_t clock_time(void) { return (clock_time_t)fake_tick; }

static uint32_t bsp_tick_cb(int call_count)
{
    (void)call_count;
    return fake_tick;
}

/* Track uart_set_line_config calls (not mocked; real function with
 * overridden get_uart_kernel_clock via test platform). */
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart,
                                        const uint8_t *data, uint16_t size)
{
    (void)huart; (void)data; (void)size;
    return HAL_OK;
}

void gpio_set_pin(gpio_port_t port, gpio_pin_t pin, uint8_t val)
{
    (void)port; (void)pin; (void)val;
}

static void noop_enter(void) {}
static void noop_exit(void) {}
typedef uint8_t gpio_port_t_l;
typedef uint8_t gpio_pin_t_l;

const rf_feeder_t *rf_store_get(feeder_id_t line_id);
const rf_feeder_t *rf_store_get(feeder_id_t line_id)
{
    (void)line_id;
    return NULL;
}

/* This fixture covers healthy config/data paths; recovery is exercised
 * with hardware-state-aware fakes in test_modbus_dma_tx_scenario.c. */
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    TEST_FAIL_MESSAGE("Unexpected DMA recovery in healthy-path fixture");
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    TEST_FAIL_MESSAGE("Unexpected DMA init in healthy-path fixture");
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *huart)
{
    (void)huart;
    return HAL_OK;
}

/* Helper: set the nvram mock config and call apply. */
static modbus_configs_t test_cfg;

static void set_config(uint32_t baud, uint8_t stop)
{
    test_cfg.device_addr = 1U;
    test_cfg.stop_bits = stop;
    test_cfg.baud_rate = baud;
    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&test_cfg);
}

void setUp(void)
{
    elog_log_uart_recovery_Ignore();
    uart_abort_tx_IgnoreAndReturn(true);
    fake_tick = 0U;
    response_len = 0U;
    memset(response, 0, sizeof(response));
    line_config_calls = 0U;
    last_config_baud = 0U;
    last_config_stop = 0U;
    line_config_result = true;

    bsp_get_tick_StubWithCallback(bsp_tick_cb);
    relay_set_Ignore();
    nvram_get_modbus_device_addr_IgnoreAndReturn(1U);
    uart_send_buffer_rs485_Ignore();
    uart_set_rx_interrupt_Ignore();
    rf_get_phase_data_IgnoreAndReturn(false);
    rtc_get_unix_epoch_IgnoreAndReturn(0U);
    reboot_system_delayed_Ignore();
    modbus_system_stats_invalidate_snapshot_Ignore();

    /* Boot state: 115200 8N1 (matches the static initializers). */
    s_modbus_tx_busy = false;
    huart4.hdmatx = &dma_tx_handle;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart4.gState = HAL_UART_STATE_READY;
    huart4.ErrorCode = 0U;
    active_baud = 115200U;
    active_stop_bits = 1U;
    test_cfg = (modbus_configs_t){ .device_addr = 1U,
                                    .stop_bits = 1U,
                                    .baud_rate = 115200U };
    nvram_get_modbus_config_IgnoreAndReturn(&test_cfg);

    libmodbusrtu_slave_init(&s_modbus, 1U, millis, uart_write);
    libmodbusrtu_register_critical_section(&s_modbus, noop_enter, noop_exit);
}

void tearDown(void)
{
}

void test_stop_bit_change_applies_when_baud_unchanged(void)
{
    /* Boot: 115200 8N1. Change to 8N2 with same baud. */
    set_config(115200U, 2U);
    uart_set_line_config_ExpectAndReturn(UART_4, 115200U, 2U, true);
    TEST_ASSERT_TRUE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT8(2U, active_stop_bits);

    set_config(115200U, 1U);
    uart_set_line_config_ExpectAndReturn(UART_4, 115200U, 1U, true);
    TEST_ASSERT_TRUE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
}

void test_baud_and_stop_bit_change_together(void)
{
    set_config(9600U, 2U);
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, true);
    TEST_ASSERT_TRUE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT32(9600U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(2U, active_stop_bits);
}

void test_unchanged_config_does_no_work(void)
{
    /* Same baud, same stop-bits: apply should return true (no change
     * needed) without touching UART state. Verify by checking that
     * calling it twice is idempotent. */
    /* Strict mock: any uart_set_line_config call fails the test. */
    set_config(115200U, 1U);
    TEST_ASSERT_TRUE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);

    /* Second call: still no BSP call expected. */
    TEST_ASSERT_TRUE(modbus_uart_apply_config());
}

void test_invalid_stop_bit_rejected_without_touching_state(void)
{
    /* Set a valid config first. */
    set_config(115200U, 1U);
    TEST_ASSERT_TRUE(modbus_uart_apply_config());

    /* Now try invalid stop_bits values. */
    const uint8_t invalid[] = {0U, 3U, 255U};
    for (size_t i = 0U; sizeof(invalid) > i; i++)
    {
        set_config(115200U, invalid[i]);
        /* Strict mock: any BSP call fails. */
        TEST_ASSERT_FALSE(modbus_uart_apply_config());
        /* State must be preserved. */
        TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
        TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
    }
}

void test_zero_baud_rejected_even_with_valid_stop_bits(void)
{
    set_config(0U, 1U);
    /* Strict mock: any BSP call fails. */
    TEST_ASSERT_FALSE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
}

void test_bsp_failure_leaves_active_state_unchanged(void)
{
    set_config(9600U, 2U);
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, false);
    TEST_ASSERT_FALSE(modbus_uart_apply_config());
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
}
