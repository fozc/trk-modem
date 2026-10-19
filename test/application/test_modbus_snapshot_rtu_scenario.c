/*
 * test_modbus_snapshot_rtu_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * MB-08 RTU-path: two FC03 responses through byte-input -> core ->
 * response. Calendar fields in one response come from one rtc_now()
 * snapshot; a new response takes a fresh snapshot.
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
#include "mock_modbus_bms_stats.h"
#include "mock_modbus_gsm_stats.h"
#include "mock_breaker.h"
#include "mock_rf.h"
#include "mock_nvram.h"
#include "mock_fault_log.h"
#include "mock_reboot.h"
#include "mock_uart.h"
#include "mock_rtc.h"
#include "mock_reset_source.h"
#include "mock_adc.h"
#include "mock_digital_input.h"
#include "modbus_rtu_slave.h"
#include "modbus_system_stats.h"
#include "modbus_power_stats.h"
#include "power_board_scp.h"

/* Real system_stats module (not mocked): lazy snapshot verified through
 * the production fc03_read_callback path. */
#define MODBUS_TX_USE_DMA 1
uint32_t HAL_GetTick(void);
#include "../../Application/modbus_process.c"
#include "../../Application/modbus_line_data.c"

static uint32_t fake_tick;
static rtc_t current_calendar;
static modbus_configs_t modbus_cfg = { .device_addr = 1U,
                                        .baud_rate = 115200U, .stop_bits = 1U };
static uint8_t response[64];
static uint16_t response_len;

UART_HandleTypeDef huart4;
static uint32_t dma_calls;

uint32_t HAL_GetTick(void) { return fake_tick; }
clock_time_t clock_time(void) { return (clock_time_t)fake_tick; }

static uint32_t rtc_now_call_count;
static rtc_t rtc_now_stub(int call_count)
{
    (void)call_count;
    rtc_now_call_count++;
    return current_calendar;
}

static uint32_t bsp_tick_cb(int call_count)
{
    (void)call_count;
    return fake_tick;
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart,
                                        const uint8_t *data, uint16_t size)
{
    (void)huart;
    memcpy(response, data, size);
    response_len = size;
    dma_calls++;
    huart->gState = 0x20U; /* READY */
    return HAL_OK;
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

static void feed_calendar_read(void)
{
    uint8_t frame[8] = {1U, 3U, 0x23, 0x28, 0x00, 0x06, 0, 0};
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; 6U > i; i++)
    {
        crc = (uint16_t)(crc ^ frame[i]);
        for (uint8_t b = 0U; 8U > b; b++)
        {
            crc = (uint16_t)((crc >> 1) ^
                             ((crc & 1U) ? 0xA001U : 0U));
        }
    }
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    for (size_t i = 0U; 8U > i; i++)
    {
        modbus_process_isr_rx_byte(frame[i]);
    }
}

static uint16_t resp_reg(size_t index)
{
    return (uint16_t)((response[3 + 2 * index] * 256U) +
                      response[4 + 2 * index]);
}

void setUp(void)
{
    fake_tick = 0U;
    dma_calls = 0U;
    response_len = 0U;
    memset(response, 0, sizeof(response));

    bsp_get_tick_StubWithCallback(bsp_tick_cb);
    current_calendar = (rtc_t){0};
    rtc_now_call_count = 0U;
    rtc_now_StubWithCallback(rtc_now_stub);
    relay_set_Ignore();
    nvram_get_modbus_device_addr_IgnoreAndReturn(1U);
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
    uart_send_buffer_rs485_Ignore();
    uart_set_rx_interrupt_Ignore();
    rf_get_phase_data_IgnoreAndReturn(false);
    rtc_get_unix_epoch_IgnoreAndReturn(0U);
    reboot_system_delayed_Ignore();

    s_modbus_tx_busy = false;  /* static state from previous test */
    libmodbusrtu_slave_init(&s_modbus, 1U, millis, uart_write);
    libmodbusrtu_register_read_callback(&s_modbus, fc03_read_callback);
    libmodbusrtu_register_write_callback(&s_modbus, fc06_write_callback);
    libmodbusrtu_register_critical_section(&s_modbus, noop_enter, noop_exit);
    modbus_apply_frame_silence();
}

void tearDown(void) {}

void test_calendar_response_uses_one_rtc_now_snapshot(void)
{
    /* rtc_now is called once by the lazy snapshot; the response carries
     * all 6 calendar fields from that single call. */
    const rtc_t cal = { .second = 59U, .minute = 42U, .hour = 14U,
                        .day = 17U, .month = 10U, .year = 26U };
    current_calendar = cal;

    feed_calendar_read();
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));

    /* All 6 calendar words from the same rtc_t struct. */
    TEST_ASSERT_EQUAL_UINT16(59U,   resp_reg(0)); /* sec   */
    TEST_ASSERT_EQUAL_UINT16(42U,   resp_reg(1)); /* min   */
    TEST_ASSERT_EQUAL_UINT16(14U,   resp_reg(2)); /* hour  */
    TEST_ASSERT_EQUAL_UINT16(17U,   resp_reg(3)); /* day   */
    TEST_ASSERT_EQUAL_UINT16(10U,   resp_reg(4)); /* month */
    TEST_ASSERT_EQUAL_UINT16(2026U, resp_reg(5)); /* year  */

    /* rtc_now called exactly once by the lazy snapshot for this response. */
    TEST_ASSERT_EQUAL_UINT32(1U, rtc_now_call_count);
    /* FC03 response: slave(1)+func(1)+count(1)+6regs*2bytes+CRC(2) = 17. */
    TEST_ASSERT_EQUAL_UINT16(17U, response_len);
}

static void feed_power_seq_read(void)
{
    uint8_t frame[8] = {1U, 3U, 0x23, 0xF2, 0x00, 0x01, 0, 0};
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; 6U > i; i++)
    {
        crc = (uint16_t)(crc ^ frame[i]);
        for (uint8_t b = 0U; 8U > b; b++)
        {
            crc = (uint16_t)((crc >> 1) ^
                             ((crc & 1U) ? 0xA001U : 0U));
        }
    }
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    for (size_t i = 0U; 8U > i; i++)
    {
        modbus_process_isr_rx_byte(frame[i]);
    }
}

static void prepare_power(uint8_t seq)
{
    rf_scp_message_t msg =
    {
        .cmd = RF_SCP_CMD_PWR_SUMMARY, .type = SCP_TYPE_SET,
        .body.power =
        {
            .flags = 0x83U, .flags2 = 5U, .soc_flags = 3U,
            .source = 1U, .session = 7U, .charge_phase = 2U,
            .battery_ma = -321, .soc_tenths = -125,
            .battery_temperature = 24, .board_temperature = 31,
            .capacity_ah = 12U, .soh_percent = 96U
        }
    };
    msg.body.power.seq = seq;
    msg.body.power.battery_mv = 12000U;
    TEST_ASSERT_TRUE(power_board_handle_summary(&msg, fake_tick));
}

void test_power_board_fresh_between_rtU_responses(void)
{
    /* PowerBoard seq refreshes between two RTU responses (lazy snapshot). */
    prepare_power(5U);
    feed_power_seq_read();
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
    TEST_ASSERT_EQUAL_UINT16(5U, resp_reg(0));
    modbus_process_tx_complete_callback();

    /* Underlying PowerBoard data changes between responses. */
    prepare_power(7U);
    feed_power_seq_read();
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
    TEST_ASSERT_EQUAL_UINT16(7U, resp_reg(0));
}

void test_second_response_takes_fresh_calendar_snapshot(void)
{
    /* Response 1: calendar at t1. */
    rtc_t t1 = { .second = 30U, .minute = 10U, .hour = 12U,
                 .day = 1U, .month = 5U, .year = 26U };
    current_calendar = t1;
    feed_calendar_read();
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
    TEST_ASSERT_EQUAL_UINT16(30U, resp_reg(0));
    TEST_ASSERT_EQUAL_UINT16(10U, resp_reg(1));
    modbus_process_tx_complete_callback(); /* release bus for response 2 */

    /* Response 2: calendar at t2 (minute ticked over). */
    rtc_t t2 = { .second = 5U, .minute = 11U, .hour = 12U,
                 .day = 1U, .month = 5U, .year = 26U };
    current_calendar = t2;
    feed_calendar_read();
    fake_tick += 4U;
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED, modbus_poll(&s_modbus));
    TEST_ASSERT_EQUAL_UINT16(5U,  resp_reg(0)); /* fresh sec   */
    TEST_ASSERT_EQUAL_UINT16(11U, resp_reg(1)); /* fresh min   */
    TEST_ASSERT_EQUAL_UINT16(12U, resp_reg(2)); /* same hour   */
}
