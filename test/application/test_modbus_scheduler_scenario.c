/*
 * test_modbus_scheduler_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Real Contiki scheduler path through the production modbus_process thread
 * (reviewer R2): a second complete frame must be answered without waiting
 * for new bus traffic.
 */
#undef NO_SHELL_LOG
#include "unity.h"
#include "mock_runtime_stats.h"
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
#include "modbus_rtu_slave.h"
#include "modbus_power_stats.h"

#define MODBUS_TX_USE_DMA 0
uint32_t HAL_GetTick(void);
#include "../../Application/modbus_process.c"
#include "../../Application/modbus_line_data.c"

static uint32_t fake_tick;

/* Local rf_config stubs: rf_config.h signatures are not CMock-
 * compatible, same pattern as modbus_rf_snapshot_fixture.h. */
const rf_feeder_t *rf_store_get(feeder_id_t line_id);

const rf_feeder_t *rf_store_get(feeder_id_t line_id)
{
    (void)line_id;
    return NULL;
}
static uint32_t relay_on_calls;
static modbus_configs_t modbus_cfg =
{
    .device_addr = 1U,
    .baud_rate = 115200U,
    .stop_bits = 1U,
};

uint32_t HAL_GetTick(void)
{
    return fake_tick;
}

clock_time_t clock_time(void)
{
    return (clock_time_t)fake_tick;
}

static uint32_t bsp_tick_cb(int call_count)
{
    (void)call_count;
    return fake_tick;
}

static bool relay_set_cb(relay_channel_t channel, bool on, int call_count)
{
    (void)call_count;
    if ((RELAY_CH_1 == channel) && on)
    {
        relay_on_calls++;
    }
    return true;
}

static void pump(void)
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

/* Feed one 8-byte FC06 relay command through the real ISR entry. */
static void isr_feed_command(uint8_t slave)
{
    uint8_t frame[8];
    frame[0] = slave;
    frame[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    frame[2] = 0x27U;   /* wire register 10002 -> relay 1 (logical 50002) */
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

void setUp(void)
{
    runtime_stats_add_Ignore();
    modbus_system_stats_invalidate_snapshot_Ignore();
    fake_tick = 0U;
    relay_on_calls = 0U;
    bsp_get_tick_StubWithCallback(bsp_tick_cb);
    relay_set_StubWithCallback(relay_set_cb);
    nvram_get_modbus_device_addr_IgnoreAndReturn(1U);
    nvram_get_modbus_config_IgnoreAndReturn(&modbus_cfg);
    uart_send_buffer_rs485_Ignore();
    uart_set_rx_interrupt_Ignore();
    rf_get_phase_data_IgnoreAndReturn(false);
    rtc_get_unix_epoch_IgnoreAndReturn(0U);
    reboot_system_delayed_Ignore();
    process_init();
    process_start(&etimer_process, NULL);
    process_start(&modbus_process, NULL);
    pump();
}

void tearDown(void)
{
}

void test_frame_behind_unconsumed_frame_is_dropped_then_retry_works(void)
{
    /* Foreign slave frame completes, then our command arrives behind it
     * while the process has not run once: Contiki coalesces the per-byte
     * polls into a single event and the single RX buffer still holds the
     * foreign frame. Our command must be dropped (not merged, not left
     * pending), the line must recover, and the master retry must be
     * answered through the real scheduler path. */
    isr_feed_command(5U);
    fake_tick += 4U;
    isr_feed_command(1U);
    fake_tick += 4U;

    pump();
    TEST_ASSERT_EQUAL_UINT32(0U, relay_on_calls);

    fake_tick += 4U;      /* recovery gap */
    isr_feed_command(1U); /* master retry */
    fake_tick += 4U;

    pump();
    TEST_ASSERT_EQUAL_UINT32(1U, relay_on_calls);
}
