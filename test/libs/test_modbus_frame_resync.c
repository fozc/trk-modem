/*
 * test_modbus_frame_resync.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Frame boundary, overflow recovery and line-error invalidation through the
 * production RTU receive path (solution plan WP-A; findings MB-01/03/04).
 */
#include "unity.h"
#include "modbus_rtu_slave.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

TEST_SOURCE_FILE("modbus_rtu_slave.c")

static struct
{
    uint8_t before[16];
    modbus_slave_t context;
    uint8_t after[16];
} guarded;
static uint32_t fake_tick;
static uint32_t read_calls;
static uint32_t write_calls;
static uint8_t response[MODBUS_BUFFER_SIZE];
static uint16_t response_len;
static uint32_t response_count;
static bool inject_byte_during_enter;
static bool inject_error_during_enter;

static uint16_t frame_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; length > index; index++)
    {
        crc = (uint16_t)(crc ^ data[index]);
        for (uint8_t bit = 0U; 8U > bit; bit++)
        {
            crc = (uint16_t)((crc >> 1U) ^
                             ((0U != (crc & 1U)) ? 0xA001U : 0U));
        }
    }
    return crc;
}

static uint32_t get_tick(void)
{
    return fake_tick;
}

static void capture_response(const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(sizeof(response), length);
    memcpy(response, data, length);
    response_len = length;
    response_count++;
}

static modbus_reg_status_t read_register(uint16_t address, uint16_t *value)
{
    (void)address;
    read_calls++;
    *value = 0x1357U;
    return MODBUS_REG_OK;
}

static uint16_t last_written_value;

static modbus_reg_status_t write_register(uint16_t address, uint16_t value)
{
    (void)address;
    last_written_value = value;
    write_calls++;
    return MODBUS_REG_OK;
}

/* Build an 8-byte FC06 command (write value to register 0). */
static void build_command(uint8_t frame[8], uint8_t slave, uint16_t value)
{
    frame[0] = slave;
    frame[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    frame[2] = 0U;
    frame[3] = 0U;
    frame[4] = (uint8_t)(value >> 8);
    frame[5] = (uint8_t)(value & 0xFFU);
    const uint16_t crc = frame_crc(frame, 6U);
    frame[6] = (uint8_t)(crc & 0xFFU);
    frame[7] = (uint8_t)(crc >> 8U);
}

static void feed_bytes(const uint8_t *bytes, size_t length)
{
    for (size_t index = 0U; length > index; index++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, bytes[index]);
    }
}

static void feed_command_value(uint8_t slave, uint16_t value)
{
    uint8_t frame[8];
    build_command(frame, slave, value);
    feed_bytes(frame, 8U);
}

static void feed_command(uint8_t slave, bool valid_crc)
{
    uint8_t frame[8];
    build_command(frame, slave, valid_crc ? 0x0001U : 0x0001U);
    if (!valid_crc)
    {
        frame[7] ^= 1U;
    }
    feed_bytes(frame, 8U);
}

/* Run the poll until it stops handling frames (bounded). */
static void drain_poll(void)
{
    for (uint32_t rounds = 0U; 4U > rounds; rounds++)
    {
        if (MODBUS_POLL_HANDLED !=
            libmodbusrtu_modbus_process(&guarded.context))
        {
            break;
        }
    }
}

static void advance_silence(void)
{
    fake_tick += (uint32_t)guarded.context.frame_silence_ms + 1U;
}

void setUp(void)
{
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0xA5, sizeof(guarded.before));
    memset(guarded.after, 0xA5, sizeof(guarded.after));
    fake_tick = 0U;
    last_written_value = 0U;
    inject_byte_during_enter = false;
    inject_error_during_enter = false;
    read_calls = 0U;
    write_calls = 0U;
    response_len = 0U;
    response_count = 0U;
    memset(response, 0, sizeof(response));
    libmodbusrtu_slave_init(&guarded.context, 1U, get_tick, capture_response);
    libmodbusrtu_register_read_callback(&guarded.context, read_register);
    libmodbusrtu_register_write_callback(&guarded.context, write_register);
}

void tearDown(void)
{
    for (size_t index = 0U; sizeof(guarded.before) > index; index++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.before[index]);
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.after[index]);
    }
}

void test_foreign_then_local_frame_never_merges_when_poll_is_late(void)
{
    /* Master polls another slave, then this one with a legal inter-frame
     * gap - while the poll has not run between the two frames. The
     * single RX buffer keeps the first (complete) frame for the poll and
     * DROPS ours instead of merging; nothing mixes. */
    feed_command(5U, true);
    advance_silence();
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_TRUE(guarded.context.discard);

    /* Master retry after the recovery gap is accepted exactly once. */
    advance_silence();
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
    TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
    TEST_ASSERT_EQUAL_UINT16(0U, guarded.context.rx_count);
    TEST_ASSERT_FALSE(guarded.context.discard);
}

void test_uart_error_invalidates_frame_and_resyncs_on_idle(void)
{
    uint8_t frame[8];
    build_command(frame, 1U, true);

    /* A line error mid-frame marks the frame untrusted: the rest of the
     * burst must not execute and must not respond. */
    feed_bytes(frame, 4U);
    libmodbusrtu_modbus_rx_error(&guarded.context);
    feed_bytes(&frame[4], 4U);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_TRUE(guarded.context.discard);

    /* After a verified idle gap the next command is accepted once. */
    advance_silence();
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
    TEST_ASSERT_FALSE(guarded.context.discard);
}

void test_midframe_gap_splits_stream_and_executes_nothing(void)
{
    uint8_t frame[8];
    build_command(frame, 1U, true);

    /* Guard the construction: the two halves must stay CRC-invalid when
     * parsed as standalone frames (the second half starts with 0x00, the
     * broadcast address, so a CRC collision would execute a broadcast). */
    TEST_ASSERT_NOT_EQUAL_UINT16(frame_crc(frame, 6U),
                                 frame_crc(&frame[4], 2U));

    feed_bytes(frame, 4U);
    advance_silence();          /* coarse mid-frame gap splits the stream */
    feed_bytes(&frame[4], 4U);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);

    /* First clean command after the split is accepted exactly once. */
    advance_silence();
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
}

void test_waiting_frame_survives_discard_recovery_and_is_executed(void)
{
    /* Distinct commands A/B/C/D. A completes in the buffer, B arrives and
     * is dropped, then C arrives after the recovery silence: A must
     * SURVIVE the discard recovery (not be wiped in favour of C) and be
     * the command that executes. The identity of the executed command is
     * asserted, not just the call count. */
    feed_command_value(1U, 0x0A0AU);          /* A completes            */
    advance_silence();
    feed_command_value(1U, 0x0B0BU);          /* B dropped, discard     */
    advance_silence();                        /* recovery silence       */
    feed_command_value(1U, 0x0C0CU);          /* C arrives: A pending   */
    advance_silence();
    drain_poll();

    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(0x0A0AU, last_written_value);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
    TEST_ASSERT_EQUAL_UINT8(1U, response[0]);

    /* After the poll consumed A, reception resumes and D is accepted. */
    advance_silence();
    feed_command_value(1U, 0x0D0DU);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(2U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(0x0D0DU, last_written_value);
}

void test_baud_derived_silence_thresholds_and_tighter_boundary(void)
{
    libmodbusrtu_modbus_set_frame_silence_from_baud(&guarded.context,
                                                    115200U);
    TEST_ASSERT_EQUAL_UINT16(3U, guarded.context.frame_silence_ms);
    libmodbusrtu_modbus_set_frame_silence_from_baud(&guarded.context,
                                                    19200U);
    TEST_ASSERT_EQUAL_UINT16(5U, guarded.context.frame_silence_ms);
    libmodbusrtu_modbus_set_frame_silence_from_baud(&guarded.context,
                                                    9600U);
    TEST_ASSERT_EQUAL_UINT16(7U, guarded.context.frame_silence_ms);

    /* With the 115200 threshold, a 4 ms inter-frame gap already
     * separates the streams: ours arriving behind a foreign frame is
     * dropped instead of merging; the retry is accepted. */
    libmodbusrtu_modbus_set_frame_silence_from_baud(&guarded.context,
                                                    115200U);
    feed_command(5U, true);      /* foreign first */
    fake_tick += 4U;
    feed_command(1U, true);
    fake_tick += 4U;
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);

    fake_tick += 4U;             /* recovery gap at the tight threshold */
    feed_command(1U, true);
    fake_tick += 4U;
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
}

void test_discard_exit_handles_tick_wrap(void)
{
    uint8_t garbage[16];

    fake_tick = UINT32_MAX - 2U;
    memset(garbage, 0xFF, sizeof(garbage));
    for (uint32_t rounds = 0U; 18U > rounds; rounds++)
    {
        feed_bytes(garbage, sizeof(garbage));
    }
    TEST_ASSERT_TRUE(guarded.context.discard);

    /* Silence across the wrap (11 ms) releases the discard state. */
    fake_tick = 8U;
    feed_command(1U, true);
    fake_tick += (uint32_t)guarded.context.frame_silence_ms + 1U;
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
}

void test_null_guards_leave_state_untouched(void)
{
    libmodbusrtu_modbus_rx_error(NULL);
    libmodbusrtu_modbus_set_frame_silence_from_baud(NULL, 115200U);
    libmodbusrtu_modbus_reset_rx(NULL);

    TEST_ASSERT_EQUAL_UINT16(MODBUS_TIMEOUT_MS,
                             guarded.context.frame_silence_ms);
}

static void race_enter(void)
{
    /* Simulates the UART RX interrupt firing between the poll's completion
     * checks and the critical section (reviewer R1): the checks must be
     * repeated inside the guard with the fresh state. */
    if (inject_byte_during_enter)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, 0xABU);
    }
    if (inject_error_during_enter)
    {
        libmodbusrtu_modbus_rx_error(&guarded.context);
    }
}

static void race_exit(void)
{
}

static void masked_noop_enter(void)
{
}

static void deferred_error_exit(void)
{
    /* Whole-vector NVIC mask model: an error notification that
     * arrives DURING the guarded section (e.g. a TX-complete IRQ
     * reporting a pending ORE) is deferred by the masked interrupt
     * and delivered only after the take, when busy == true. The
     * core must let the taken frame execute exactly once and arm
     * the discard afterwards instead of wiping the frame. */
    libmodbusrtu_modbus_rx_error(&guarded.context);
}

void test_byte_racing_into_guard_blocks_publish_of_stale_decision(void)
{
    libmodbusrtu_register_critical_section(&guarded.context,
                                           race_enter, race_exit);
    feed_command(1U, true);
    advance_silence();

    /* The racing byte arrives right before the take decision: the
     * fresh checks inside the guard must see it - the frame is NOT
     * taken this call (its silence restarted at the dropped byte),
     * nothing executes, and the buffered frame survives intact for
     * the next poll after the quiet gap. */
    inject_byte_during_enter = true;
    const modbus_poll_result_t result =
        libmodbusrtu_modbus_process(&guarded.context);
    inject_byte_during_enter = false;

    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_RECEIVING, result);
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, response_count);
    TEST_ASSERT_EQUAL_UINT16(8U, guarded.context.rx_count);
    TEST_ASSERT_TRUE(guarded.context.discard);

    advance_silence();
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
        libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
}

void test_rx_error_racing_into_guard_blocks_publish(void)
{
    libmodbusrtu_register_critical_section(&guarded.context,
                                           race_enter, race_exit);
    feed_command(1U, true);
    advance_silence();

    inject_error_during_enter = true;
    const modbus_poll_result_t result =
        libmodbusrtu_modbus_process(&guarded.context);
    inject_error_during_enter = false;

    /* The error raced in before the take: the frame was voided and
     * discarded inside the guard, so the fresh checks must not take or
     * execute anything. */
    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_IDLE, result);
    TEST_ASSERT_EQUAL_UINT16(0U, guarded.context.rx_count);
    TEST_ASSERT_TRUE(guarded.context.discard);
    TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
}

void test_error_deferred_by_whole_vector_mask_executes_frame_once(void)
{
    libmodbusrtu_register_critical_section(&guarded.context,
                                           masked_noop_enter,
                                           deferred_error_exit);
    feed_command(1U, true);
    advance_silence();

    TEST_ASSERT_EQUAL_INT(MODBUS_POLL_HANDLED,
        libmodbusrtu_modbus_process(&guarded.context));
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, response_count);
    TEST_ASSERT_EQUAL_UINT8(1U, response[0]);
    TEST_ASSERT_TRUE(guarded.context.discard);
    TEST_ASSERT_EQUAL_UINT16(0U, guarded.context.rx_count);
    TEST_ASSERT_FALSE(guarded.context.busy);
}

void test_line_error_after_long_idle_rejects_following_burst(void)
{
    /* One clean exchange leaves the line quiet with an old last_rx_time.
     * The next byte arrives with NE/ORE and the ISR reports the error
     * BEFORE the byte: recovery silence must start at the error instant,
     * so the corrupted burst must not be accepted as a fresh frame
     * (reviewer R3). */
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);

    fake_tick += 5000U;
    libmodbusrtu_modbus_rx_error(&guarded.context);
    feed_command(1U, true);
    advance_silence();
    drain_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    TEST_ASSERT_TRUE(guarded.context.discard);
}
