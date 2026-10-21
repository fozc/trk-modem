/*
 * test_modbus_dma_tx_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * DMA TX path of the production modbus_process (WP-C / MB-05): start
 * results, the TC completion chain, DMA-error release and the isolation of
 * the UART4 routing from other UARTs and from receive-side errors.
 */
#undef NO_SHELL_LOG
#include "unity.h"
#include "mock_bsp.h"
#include "power_board_scp.h"
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
#include "modbus_power_stats.h"
/* Local gpio declarations: including gpio.h would link the real
 * HAL-bound gpio.c via the Ceedling header convention. */
typedef uint8_t gpio_port_t;
typedef uint8_t gpio_pin_t;
#include "modbus_rtu_slave.h"

#define MODBUS_TX_USE_DMA 1
uint32_t HAL_GetTick(void);
#include "../../Application/modbus_process.c"
#include "../../Application/modbus_line_data.c"

static uint32_t fake_tick;

/* Local rf_config stub (CMock-incompatible header, fixture pattern). */
const rf_feeder_t *rf_store_get(feeder_id_t line_id);

const rf_feeder_t *rf_store_get(feeder_id_t line_id)
{
    (void)line_id;
    return NULL;
}
static modbus_configs_t modbus_cfg =
{
    .device_addr = 1U,
    .baud_rate = 115200U,
    .stop_bits = 1U,
};

/* DMA TX fakes */
static DMA_HandleTypeDef dma_tx_handle;
UART_HandleTypeDef huart4;
static UART_HandleTypeDef huart_other;
static HAL_StatusTypeDef dma_start_result;
static bool recovery_result;
static uint32_t abort_calls;
static bool dma_channel_enabled;
static bool uart_dma_request_enabled;
static uint8_t dma_data[MODBUS_BUFFER_SIZE];
static uint32_t dma_calls;
static uint16_t dma_last_size;
static uint8_t oe_level;

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

clock_time_t clock_time(void)
{
    return (clock_time_t)fake_tick;
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart,
                                        const uint8_t *data,
                                        uint16_t size)
{
    /* Model HAL's real preconditions: both the UART gState and the
     * DMA channel state must be READY (hal_dma.c:549, hal_uart.c:1440).
     * Without this check the test cannot detect a stuck DMA handle. */
    if ((huart->gState != HAL_UART_STATE_READY) ||
        (huart->hdmatx == NULL) ||
        (huart->hdmatx->State != HAL_DMA_STATE_READY) ||
        (huart->hdmatx->Parent != huart))
    {
        return HAL_BUSY;
    }
    if (dma_start_result != HAL_OK)
    {
        return dma_start_result;
    }
    dma_calls++;
    TEST_ASSERT_FALSE(dma_channel_enabled);
    dma_channel_enabled = true;
    uart_dma_request_enabled = true;
    (void)memcpy(dma_data, data, size);
    dma_last_size = size;
    huart->gState = HAL_UART_STATE_BUSY_TX;
    huart->hdmatx->State = HAL_DMA_STATE_BUSY;
    return HAL_OK;
}

/* BSP boundary: success means hardware was stopped and both handles
 * are reusable. The actual HAL recovery is tested in test_uart_tx_scenario. */
static bool abort_tx(uart_port_t port, int call_count)
{
    (void)call_count;
    TEST_ASSERT_EQUAL(UART_4, port);
    abort_calls++;
    uart_dma_request_enabled = false;
    if (!recovery_result)
    {
        dma_tx_handle.State = HAL_DMA_STATE_ERROR;
        return false;
    }
    dma_channel_enabled = false;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    dma_tx_handle.Parent = &huart4;
    huart4.gState = HAL_UART_STATE_READY;
    return true;
}

void gpio_set_pin(gpio_port_t port, gpio_pin_t pin, uint8_t val)
{
    if ((port == MODBUS_OE_BSP_GPIO) && (pin == MODBUS_OE_BSP_PIN))
    {
        oe_level = val;
    }
}

static uint32_t bsp_tick_cb(int call_count)
{
    (void)call_count;
    return fake_tick;
}

static void feed_relay_command(void)
{
    uint8_t frame[8];
    frame[0] = 1U;
    frame[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    frame[2] = 0x27U;      /* wire register 10002 -> relay 1 */
    frame[3] = 0x12U;
    frame[4] = 0x00U;
    frame[5] = 0x01U;
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; 6U > index; index++)
    {
        crc = (uint16_t)(crc ^ frame[index]);
        for (uint8_t bit = 0U; 8U > bit; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                             ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);
    for (size_t index = 0U; 8U > index; index++)
    {
        modbus_process_isr_rx_byte(frame[index]);
    }
}

/* Frame + silence so the poll takes and answers it (one pass). */
static void exchange(void)
{
    feed_relay_command();
    fake_tick += 20U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
}

/* Simulates the real interrupt chain: DMA TC ISR (sets DMA READY) ->
 * UART TC ISR -> TxCpltCallback -> modbus_process_tx_complete_callback. */
static void simulate_tx_complete(void)
{
    dma_channel_enabled = false;
    uart_dma_request_enabled = false;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart4.gState = HAL_UART_STATE_READY;
    modbus_process_tx_complete_callback();
}

void setUp(void)
{
    elog_log_uart_recovery_Ignore();
    modbus_system_stats_invalidate_snapshot_Ignore();
    fake_tick = 0U;
    dma_start_result = HAL_OK;
    recovery_result = true;
    dma_calls = 0U;
    abort_calls = 0U;
    uart_abort_tx_Stub(abort_tx);
    dma_channel_enabled = false;
    uart_dma_request_enabled = false;
    active_baud = 115200U;
    active_stop_bits = 1U;
    s_modbus_tx_busy = false;
    dma_tx_handle.Parent = &huart4;
    huart4.ErrorCode = 0U;
    huart4.gState = HAL_UART_STATE_READY;
    huart4.hdmatx = &dma_tx_handle;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart_other.ErrorCode = 0U;
    huart_other.gState = HAL_UART_STATE_READY;
    dma_last_size = 0U;
    oe_level = 0U;
    bsp_get_tick_StubWithCallback(bsp_tick_cb);
    relay_set_IgnoreAndReturn(true);
    nvram_get_modbus_device_addr_IgnoreAndReturn(1U);
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
    uart_send_buffer_rs485_Ignore();
    uart_set_rx_interrupt_Ignore();
    rf_get_phase_data_IgnoreAndReturn(false);
    rtc_get_unix_epoch_IgnoreAndReturn(0U);
    reboot_system_delayed_Ignore();

    libmodbusrtu_slave_init(&s_modbus, 1U, millis, uart_write);
    libmodbusrtu_register_read_callback(&s_modbus, fc03_read_callback);
    libmodbusrtu_register_write_callback(&s_modbus, fc06_write_callback);
    libmodbusrtu_register_critical_section(&s_modbus, NULL, NULL);
    modbus_apply_frame_silence();
}

void tearDown(void)
{
}

void test_dma_error_releases_bus_and_next_response_is_sent(void)
{
    /* MB-05: a DMA transfer error after a successful start must release
     * OE and the busy slot, so the NEXT response can actually go out. */
    exchange();
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
    TEST_ASSERT_TRUE(s_modbus_tx_busy);

    /* Model what HAL's UART_DMAError leaves behind: it stopped the
     * transfer, restored its own state and marked ErrorCode with the DMA
     * bit before routing here. */
    /* Model the full UART_DMAError chain: HAL restores both handles
     * to READY (UART_EndTxTransfer + DMA ISR) BEFORE the app callback. */
    dma_channel_enabled = false;
    uart_dma_request_enabled = false;
    huart4.gState = HAL_UART_STATE_READY;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart4.ErrorCode = HAL_UART_ERROR_DMA;
    HAL_UART_ErrorCallback(&huart4);

    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);

    exchange();
    TEST_ASSERT_EQUAL_UINT32(2U, dma_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
}

void test_start_failure_releases_bus_and_next_response_works(void)
{
    const HAL_StatusTypeDef results[] = { HAL_BUSY, HAL_ERROR };
    for (size_t index = 0U; 2U > index; index++)
    {
        dma_start_result = results[index];
        exchange();
        /* Failed start: this response is dropped, the bus is released. */
        TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
        TEST_ASSERT_FALSE(s_modbus_tx_busy);

        dma_start_result = HAL_OK;
        exchange();
        TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
        simulate_tx_complete();
        TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    }
}

void test_normal_tc_chain_releases_bus_after_last_byte(void)
{
    exchange();
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
    TEST_ASSERT_TRUE(s_modbus_tx_busy);

    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);

    exchange();
    TEST_ASSERT_EQUAL_UINT32(2U, dma_calls);
    modbus_process_tx_complete_callback();
}

void test_error_callback_for_other_uart_leaves_tx_alone(void)
{
    exchange();
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    huart_other.ErrorCode = HAL_UART_ERROR_DMA;
    HAL_UART_ErrorCallback(&huart_other);

    /* Not UART4: the in-flight transfer keeps its bus and completes. */
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
}

static modbus_configs_t changed_config =
{
    .device_addr = 1U,
    .baud_rate = 9600U,
    .stop_bits = 2U,
};

static void change_config_during_tx(void)
{
    exchange();
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&changed_config);
    recovery_result = true;
}

static void assert_next_response(void)
{
    /* Compare the actual FC06 wire response, not just a HAL call count. */
    static const uint8_t previous_response[] =
    {
        0x01U, 0x06U, 0x27U, 0x12U, 0x00U, 0x01U, 0xE2U, 0xBBU
    };
    TEST_ASSERT_EQUAL_UINT16(sizeof(previous_response), dma_last_size);
    fake_tick += 20U;
    exchange();
    TEST_ASSERT_EQUAL_UINT32(2U, dma_calls);
    TEST_ASSERT_EQUAL_UINT16(sizeof(previous_response), dma_last_size);
    TEST_ASSERT_EQUAL_MEMORY(previous_response, dma_data,
                             sizeof(previous_response));
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
}

void test_abort_timeout_recovers_channel_and_next_request_is_answered(void)
{
    change_config_during_tx();
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, true);
    TEST_ASSERT_EQUAL_INT(0, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT32(9600U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(2U, active_stop_bits);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    TEST_ASSERT_EQUAL_PTR(&huart4, dma_tx_handle.Parent);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    assert_next_response();
}

void test_successful_recovery_applies_new_slave_id_after_line_config(void)
{
    change_config_during_tx();
    nvram_get_modbus_device_addr_StopIgnore();
    nvram_get_modbus_device_addr_IgnoreAndReturn(2U);
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, true);
    TEST_ASSERT_EQUAL_INT(0, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT8(2U, s_modbus.slave_id);
    TEST_ASSERT_EQUAL_UINT32(9600U, active_baud);
}

void test_channel_reset_failure_reports_error_and_keeps_active_config(void)
{
    change_config_during_tx();
    nvram_get_modbus_device_addr_StopIgnore();
    nvram_get_modbus_device_addr_IgnoreAndReturn(2U);
    recovery_result = false;
    TEST_ASSERT_EQUAL_INT(-1, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT8(1U, s_modbus.slave_id);
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
    TEST_ASSERT_NOT_EQUAL(HAL_UART_STATE_READY, huart4.gState);
    TEST_ASSERT_NOT_EQUAL(HAL_DMA_STATE_READY, dma_tx_handle.State);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    exchange();
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
}

void test_normal_abort_does_not_reinitialize_dma(void)
{
    change_config_during_tx();
    recovery_result = true;
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, true);
    TEST_ASSERT_EQUAL_INT(0, modbus_process_notify_config_changed());
    assert_next_response();
}

void test_explicit_apply_after_failed_recovery_can_restore_old_config(void)
{
    change_config_during_tx();
    recovery_result = false;
    TEST_ASSERT_EQUAL_INT(-1, modbus_process_notify_config_changed());
    /* Returning to the old target must not report success with DMA stuck. */
    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
    recovery_result = true;
    uart_set_line_config_ExpectAndReturn(UART_4, 115200U, 1U, true);
    TEST_ASSERT_EQUAL_INT(0, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT32(2U, abort_calls);
    TEST_ASSERT_EQUAL_UINT32(HAL_UART_STATE_READY, huart4.gState);
    TEST_ASSERT_EQUAL_UINT32(HAL_DMA_STATE_READY, dma_tx_handle.State);
    assert_next_response();
}

void test_missing_dma_handle_reports_error_without_applying_config(void)
{
    change_config_during_tx();
    recovery_result = false;
    huart4.hdmatx = NULL;
    TEST_ASSERT_EQUAL_INT(-1, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
}

void test_failed_line_config_after_recovery_still_answers_at_old_baud(void)
{
    change_config_during_tx();
    uart_set_line_config_ExpectAndReturn(UART_4, 9600U, 2U, false);
    TEST_ASSERT_EQUAL_INT(-1, modbus_process_notify_config_changed());
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_EQUAL_UINT8(1U, active_stop_bits);
    assert_next_response();
}

void test_rx_sourced_hal_error_callback_keeps_tx_alive(void)
{
    exchange();
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    /* A receive-side error (ORE) reaching the HAL error callback must not
     * release the in-flight transmission: HAL routes several receive
     * error paths to the same callback WITHOUT the DMA bit, so the app
     * must discriminate on ErrorCode, not on UART identity alone. */
    huart4.ErrorCode = HAL_UART_ERROR_ORE;
    HAL_UART_ErrorCallback(&huart4);

    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    /* The transmission still completes normally through the TC chain. */
    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
}

void test_rx_error_during_tx_does_not_touch_tx(void)
{
    exchange();
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    /* Receive-side line error routes to the RX invalidation path only. */
    modbus_process_isr_rx_error();

    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);
    simulate_tx_complete();
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
}


void test_missing_tc_recovers_without_a_new_master_request(void)
{
    exchange();
    dma_channel_enabled = false;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    fake_tick += 10U;
    TEST_ASSERT_EQUAL(MODBUS_POLL_IDLE, modbus_poll(&s_modbus));
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    assert_next_response();
}

void test_tx_deadline_preserves_frame_until_wire_time_and_silence_elapse(void)
{
    active_baud = 9600U;
    active_stop_bits = 2U;
    modbus_apply_frame_silence();
    exchange();
    /* 8 * 11 bits / 9600 -> ceil 9.167 ms = 10 ms; core gap = 7 ms. */
    fake_tick += 16U;
    (void)modbus_poll(&s_modbus);
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT32(0U, abort_calls);
    fake_tick++;
    (void)modbus_poll(&s_modbus);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
}

void test_tx_timeout_is_wrap_safe_and_failed_recovery_releases_bus(void)
{
    fake_tick = UINT32_MAX - 25U;
    exchange();
    recovery_result = false;
    fake_tick += 10U;
    (void)modbus_poll(&s_modbus);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    (void)modbus_poll(&s_modbus);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
}


static void pump_scheduler(void)
{
    for (size_t step = 0U; 200U > step; step++)
    {
        if (0 == process_run())
        {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Modbus scheduler did not settle");
}

void test_process_timer_recovers_tx_without_further_rx_events(void)
{
    process_init();
    process_start(&etimer_process, NULL);
    modbus_process_init();
    pump_scheduler();
    feed_relay_command();
    pump_scheduler();
    fake_tick += 20U;
    etimer_request_poll();
    pump_scheduler();
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
    fake_tick += 10U;
    etimer_request_poll();
    pump_scheduler();
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
}

/*** end of file ***/
