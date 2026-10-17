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
static HAL_StatusTypeDef abort_result;
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
    (void)data;
    /* Model HAL's real preconditions: both the UART gState and the
     * DMA channel state must be READY (hal_dma.c:549, hal_uart.c:1440).
     * Without this check the test cannot detect a stuck DMA handle. */
    if ((huart->gState != HAL_UART_STATE_READY) ||
        (huart->hdmatx == NULL) ||
        (huart->hdmatx->State != HAL_DMA_STATE_READY))
    {
        return HAL_BUSY;
    }
    if (dma_start_result != HAL_OK)
    {
        return dma_start_result;
    }
    dma_calls++;
    dma_last_size = size;
    huart->gState = HAL_UART_STATE_BUSY_TX;
    huart->hdmatx->State = HAL_DMA_STATE_BUSY;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *huart)
{
    (void)huart;
    if (abort_result == HAL_OK)
    {
        huart->gState = HAL_UART_STATE_READY;
        if (huart->hdmatx != NULL)
        {
            huart->hdmatx->State = HAL_DMA_STATE_READY;
        }
    }
    else
    {
        /* HAL abort failure: DMA channel NOT reset; handle goes to
         * HAL_DMA_STATE_ERROR (hal_dma.c:645). gState stays BUSY_TX
         * (uart abort returned before READY restore). */
        if (huart->hdmatx != NULL)
        {
            huart->hdmatx->State = HAL_DMA_STATE_ERROR;
        }
    }
    return abort_result;
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
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
}

/* Simulates the real interrupt chain: DMA TC ISR (sets DMA READY) ->
 * UART TC ISR -> TxCpltCallback -> modbus_process_tx_complete_callback. */
static void simulate_tx_complete(void)
{
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart4.gState = HAL_UART_STATE_READY;
    modbus_process_tx_complete_callback();
}

void setUp(void)
{
    modbus_system_stats_invalidate_snapshot_Ignore();
    fake_tick = 0U;
    dma_start_result = HAL_OK;
    abort_result = HAL_OK;
    dma_calls = 0U;
    huart4.ErrorCode = 0U;
    huart4.gState = HAL_UART_STATE_READY;
    huart4.hdmatx = &dma_tx_handle;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    huart_other.ErrorCode = 0U;
    huart_other.gState = HAL_UART_STATE_READY;
    dma_last_size = 0U;
    oe_level = 0U;
    bsp_get_tick_StubWithCallback(bsp_tick_cb);
    relay_set_Ignore();
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

void test_abort_timeout_leaves_transport_unavailable_baud_unchanged(void)
{
    static modbus_configs_t cfg_9600 = { .device_addr = 1U,
                                         .baud_rate = 9600U, .stop_bits = 1U };
    exchange();                       /* response DMA in flight */
    TEST_ASSERT_TRUE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(1U, oe_level);

    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&cfg_9600);
    abort_result = HAL_TIMEOUT;
    modbus_uart_apply_config();

    /* OE released, app busy cleared, baud unchanged. NO state forcing:
     * both handles stay in their non-READY states (UART=BUSY_TX,
     * DMA=ERROR). */
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_NOT_EQUAL(HAL_UART_STATE_READY, huart4.gState);
    TEST_ASSERT_NOT_EQUAL(1U, huart4.hdmatx->State); /* not DMA_READY */

    /* Next exchange: Transmit_DMA fails at precondition check (both
     * handles non-READY) -> HAL_BUSY -> start-failure handler drops
     * the response and releases the bus. dma_calls unchanged. */
    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
    dma_start_result = HAL_OK;   /* even with OK, preconditions fail */
    exchange();
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);  /* NOT incremented */

    /* Cleanup: force states for the next test. */
    huart4.gState = HAL_UART_STATE_READY;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
}

void test_notify_apply_timeout_transport_down_baud_old(void)
{
    /* Kapsam: notify() seviyesinde apply-timeout davranisi.
     * Setter/Flash zinciri ayrica test_json_config_bounds'da.
     * Abort timeout: HAL kanali resetlemeden cikar; transport
     * dogal olarak kullanilamaz hale gelir (state'ler READY degil). */
    static modbus_configs_t cfg_9600 = { .device_addr = 1U,
                                         .baud_rate = 9600U, .stop_bits = 1U };
    exchange();                       /* response DMA in flight */
    TEST_ASSERT_TRUE(s_modbus_tx_busy);

    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&cfg_9600);
    abort_result = HAL_TIMEOUT;

    /* notify = apply: abort timeout -> return -1 -> web hata. */
    TEST_ASSERT_EQUAL_INT(-1, modbus_process_notify_config_changed());

    /* UART eski (baud degismedi), OE/busy birakildi, gState restore. */
    TEST_ASSERT_EQUAL_UINT32(115200U, active_baud);
    TEST_ASSERT_FALSE(s_modbus_tx_busy);
    TEST_ASSERT_EQUAL_UINT8(0U, oe_level);
    TEST_ASSERT_NOT_EQUAL(HAL_UART_STATE_READY, huart4.gState);
    TEST_ASSERT_NOT_EQUAL(1U, huart4.hdmatx->State);

    /* Cleanup: force states for the next test. */
    huart4.gState = HAL_UART_STATE_READY;
    dma_tx_handle.State = HAL_DMA_STATE_READY;
    abort_result = HAL_OK;
    nvram_get_modbus_config_StopIgnore();
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
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
