/*
 * test_bms_reader_scenario.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise real BMS reader RX, parsing and periodic request control.
 */
#include "unity.h"
#include "bms.h"
#include "mock_bsp.h"
#include "mock_gpio.h"
#include <string.h>

static uint32_t fake_tick;
static uint32_t uart_irq_enabled;
static uint32_t critical_calls;
static bool inject_on_restore;
static uint32_t transmitted_bytes;
static uint32_t poll_calls;
static uint8_t received_frame[BMS_FULL_MAP_FRAME_LEN];
void bms_rx_interrupt_handler(uint8_t byte);

static uint32_t get_uart_irq_enabled(void)
{
    return uart_irq_enabled;
}

static void disable_uart_irq(void)
{
    uart_irq_enabled = 0U;
    critical_calls++;
}

static void enable_uart_irq(void)
{
    uart_irq_enabled = 1U;
    if (inject_on_restore)
    {
        inject_on_restore = false;
        bms_rx_interrupt_handler(0x51U);
    }
}

static void transmit_byte(uint8_t byte)
{
    (void)byte;
    TEST_ASSERT_EQUAL_UINT32(1U, uart_irq_enabled);
    transmitted_bytes++;
}

#define UART5_IRQn 0U
#define NVIC_GetEnableIRQ(irq) get_uart_irq_enabled()
#define NVIC_DisableIRQ(irq) disable_uart_irq()
#define NVIC_EnableIRQ(irq) enable_uart_irq()
#define UART5 0U
#define LL_USART_ClearFlag_TC(port) ((void)(port))
#define LL_USART_TransmitData8(port, byte) transmit_byte(byte)
#define LL_USART_IsActiveFlag_TXE_TXFNF(port) (1U)
#define LL_USART_IsActiveFlag_TC(port) (1U)
#include "../../Application/bms/bms_reader.c"

void process_poll(struct process *process)
{
    (void)process;
    poll_calls++;
}

void process_start(struct process *process, process_data_t argument)
{
    (void)argument;
    PT_INIT(&process->pt);
    (void)process->thread(&process->pt, PROCESS_EVENT_INIT, NULL);
}

void etimer_set(struct etimer *timer, clock_time_t interval)
{
    timer->timer.start = fake_tick;
    timer->timer.interval = interval;
}

void etimer_restart(struct etimer *timer)
{
    timer->timer.start = fake_tick;
}

int etimer_expired(struct etimer *timer)
{
    return ((fake_tick - timer->timer.start) >= timer->timer.interval);
}

void elog_log_battery_soc_threshold(uint8_t threshold, bool set,
                                    uint8_t soc, uint8_t soh)
{
    (void)threshold;
    (void)set;
    (void)soc;
    (void)soh;
    TEST_ASSERT_EQUAL_UINT32(1U, uart_irq_enabled);
}

static uint32_t get_tick(int calls)
{
    (void)calls;
    return fake_tick;
}

static void prepare_frame(size_t length)
{
    memset(received_frame, 0, sizeof(received_frame));
    received_frame[0] = BMS_SLAVE_RESP_ADDR;
    received_frame[1] = 3U;
    received_frame[2] = (uint8_t)(length - 5U);
    received_frame[3] = 0x03U;
    received_frame[4] = 0xE8U;
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length - 2U; index++)
    {
        crc = (uint16_t)(crc ^ received_frame[index]);
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                            ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    received_frame[length - 2U] = (uint8_t)(crc & 0xFFU);
    received_frame[length - 1U] = (uint8_t)(crc >> 8U);
}

static void receive_bytes(size_t first, size_t last)
{
    for (size_t index = first; index < last; index++)
    {
        bms_rx_interrupt_handler(received_frame[index]);
    }
}

void setUp(void)
{
    fake_tick = 0U;
    uart_irq_enabled = 1U;
    critical_calls = 0U;
    inject_on_restore = false;
    transmitted_bytes = 0U;
    poll_calls = 0U;
    bsp_get_tick_Stub(get_tick);
    gpio_set_pin_Ignore();
    bms_rx_index = 0U;
    bms_reader_init();
}

void tearDown(void)
{
}

void test_partial_full_map_is_kept_until_last_byte_arrives(void)
{
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, 40U);
    bms_process_package();
    TEST_ASSERT_EQUAL_UINT32(40U,
        bms_rx_index);
    TEST_ASSERT_FALSE(s_bms_data.is_data_valid);
    receive_bytes(40U, sizeof(received_frame));
    bms_process_package();
    TEST_ASSERT_TRUE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT16(1000U, s_bms_data.cell_voltage_mv[0]);
}

void test_snapshot_reset_cannot_erase_byte_received_after_irq_restore(void)
{
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    const uint32_t before = critical_calls;
    inject_on_restore = true;
    bms_process_package();
    TEST_ASSERT_TRUE(critical_calls > before);
    TEST_ASSERT_TRUE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT32(1U,
        bms_rx_index);
    TEST_ASSERT_EQUAL_UINT32(1U, uart_irq_enabled);
}

void test_full_map_and_soh_validity_expire_independently(void)
{
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    fake_tick = 2000U;
    send_soh_request();
    prepare_frame(BMS_SOH_FRAME_LEN);
    receive_bytes(0U, BMS_SOH_FRAME_LEN);
    bms_process_package();
    fake_tick = 5000U;
    bms_data_t snapshot;
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_FALSE(snapshot.is_data_valid);
    TEST_ASSERT_TRUE(snapshot.is_soh_valid);
    fake_tick = 7000U;
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_FALSE(snapshot.is_soh_valid);
}

void test_partial_soh_waits_for_both_crc_bytes(void)
{
    send_soh_request();
    prepare_frame(BMS_SOH_FRAME_LEN);
    receive_bytes(0U, 5U);
    bms_process_package();
    TEST_ASSERT_FALSE(s_bms_data.is_soh_valid);
    TEST_ASSERT_EQUAL_UINT32(5U,
        bms_rx_index);
    receive_bytes(5U, BMS_SOH_FRAME_LEN);
    bms_process_package();
    TEST_ASSERT_TRUE(s_bms_data.is_soh_valid);
    TEST_ASSERT_FALSE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, s_bms_data.soh_percent);
}

void test_soh_response_cannot_satisfy_pending_full_map_request(void)
{
    send_full_map_request();
    prepare_frame(BMS_SOH_FRAME_LEN);
    receive_bytes(0U, BMS_SOH_FRAME_LEN);
    bms_process_package();
    TEST_ASSERT_FALSE(s_bms_data.is_soh_valid);
    TEST_ASSERT_FALSE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT16(BMS_FULL_MAP_FRAME_LEN, expected_length);
    TEST_ASSERT_EQUAL_UINT32(BMS_SOH_FRAME_LEN,
        bms_rx_index);
}

void test_poll_completes_response_without_sending_next_request_early(void)
{
    fake_tick = 1000U;
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_TIMER, NULL);
    TEST_ASSERT_EQUAL_UINT32(8U, transmitted_bytes);
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    TEST_ASSERT_EQUAL_UINT32(2U, poll_calls);
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_POLL, NULL);
    TEST_ASSERT_TRUE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT32(8U, transmitted_bytes);
}

void test_partial_response_expires_before_next_request_and_next_soh_succeeds(void)
{
    fake_tick = 1000U;
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_TIMER, NULL);
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, 40U);
    fake_tick = 1999U;
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_POLL, NULL);
    TEST_ASSERT_EQUAL_UINT32(40U,
        bms_rx_index);
    TEST_ASSERT_EQUAL_UINT32(8U, transmitted_bytes);
    fake_tick = 2000U;
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_TIMER, NULL);
    TEST_ASSERT_EQUAL_UINT32(16U, transmitted_bytes);
    TEST_ASSERT_EQUAL_UINT32(0U,
        bms_rx_index);
    TEST_ASSERT_EQUAL_UINT16(BMS_SOH_FRAME_LEN, expected_length);
    prepare_frame(BMS_SOH_FRAME_LEN);
    receive_bytes(0U, BMS_SOH_FRAME_LEN);
    (void)bms_process.thread(&bms_process.pt, PROCESS_EVENT_POLL, NULL);
    TEST_ASSERT_TRUE(s_bms_data.is_soh_valid);
    TEST_ASSERT_FALSE(s_bms_data.is_data_valid);
}

void test_overflow_stays_rejected_and_next_request_recovers(void)
{
    send_full_map_request();
    for (size_t index = 0U; index < sizeof(bms_rx_buffer) + 20U; index++)
    {
        bms_rx_interrupt_handler(0xA5U);
    }
    TEST_ASSERT_EQUAL_UINT32(sizeof(bms_rx_buffer) + 1U,
        bms_rx_index);
    bms_process_package();
    TEST_ASSERT_FALSE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT32(0U,
        bms_rx_index);
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    TEST_ASSERT_TRUE(s_bms_data.is_data_valid);
}

void test_bad_crc_does_not_refresh_last_good_sample_time(void)
{
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    fake_tick = 4999U;
    send_full_map_request();
    received_frame[sizeof(received_frame) - 1U] ^= 1U;
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    TEST_ASSERT_TRUE(s_bms_data.is_data_valid);
    TEST_ASSERT_EQUAL_UINT32(0U, last_full_map_tick);
    fake_tick = 5000U;
    bms_data_t snapshot;
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_FALSE(snapshot.is_data_valid);
    TEST_ASSERT_EQUAL_UINT16(1000U, snapshot.cell_voltage_mv[0]);
}

void test_validity_age_handles_tick_wrap_and_fresh_response_recovers(void)
{
    fake_tick = UINT32_MAX - 2000U;
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    fake_tick += 4999U;
    bms_data_t snapshot;
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_TRUE(snapshot.is_data_valid);
    fake_tick++;
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_FALSE(snapshot.is_data_valid);
    send_full_map_request();
    receive_bytes(0U, sizeof(received_frame));
    bms_process_package();
    bms_reader_get_data(&snapshot);
    TEST_ASSERT_TRUE(snapshot.is_data_valid);
}

void test_snapshot_preserves_preexisting_disabled_uart_irq(void)
{
    send_full_map_request();
    prepare_frame(sizeof(received_frame));
    receive_bytes(0U, 40U);
    uart_irq_enabled = 0U;
    bms_process_package();
    TEST_ASSERT_EQUAL_UINT32(0U, uart_irq_enabled);
    bms_prepare_request(BMS_SOH_FRAME_LEN);
    TEST_ASSERT_EQUAL_UINT32(0U, uart_irq_enabled);
    uart_irq_enabled = 1U;
}

/*** end of file ***/
