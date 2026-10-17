/*
 * test_modbus_fuzz.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Deterministic fuzz of the Modbus RTU core with fixed seeds and
 * bounded iterations. Verifies memory bounds, no command execution
 * from garbage tails, recovery after silence, command identity
 * preservation, and tick/handoff edge cases. Known-good scenarios
 * are interleaved with random input to verify the receiver is never
 * permanently stuck.
 */
#include "unity.h"
#include "modbus_rtu_slave.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

TEST_SOURCE_FILE("modbus_rtu_slave.c")

/* -- Fuzz configuration (bounded, deterministic) -- */
#define FUZZ_SEED           0xBEEF2026U
#define FUZZ_GARBAGE_ROUNDS 200U
#define FUZZ_INTERLEAVE     8U
#define FUZZ_TICK_WRAP_VAL  UINT32_MAX

static struct
{
    uint8_t before[16];
    modbus_slave_t context;
    uint8_t after[16];
} guarded;
static uint32_t fake_tick;
static uint32_t read_calls;
static uint32_t write_calls;
static uint16_t last_written_value;
static uint8_t response[MODBUS_BUFFER_SIZE];
static uint16_t response_len;
static uint32_t response_count;

/* -- Deterministic PRNG (xorshift32, fixed seed) -- */
static uint32_t prng_state;

static void prng_seed(uint32_t seed)
{
    prng_state = seed ? seed : 1U;
}

static uint32_t prng_next(void)
{
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

static uint8_t prng_byte(void)
{
    return (uint8_t)(prng_next() & 0xFFU);
}

/* -- Helpers -- */
static uint16_t frame_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; length > index; index++)
    {
        crc = (uint16_t)(crc ^ data[index]);
        for (uint8_t bit = 0U; 8U > bit; bit++)
        {
            crc = (uint16_t)((crc >> 1) ^
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

static modbus_reg_status_t write_register(uint16_t address, uint16_t value)
{
    (void)address;
    last_written_value = value;
    write_calls++;
    return MODBUS_REG_OK;
}

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

static void feed_command_value(uint8_t slave, uint16_t value)
{
    uint8_t frame[8];
    build_command(frame, slave, value);
    for (size_t i = 0U; 8U > i; i++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, frame[i]);
    }
}

static void advance_silence(void)
{
    fake_tick += (uint32_t)guarded.context.frame_silence_ms + 1U;
}

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

static void feed_garbage(size_t length)
{
    for (size_t i = 0U; length > i; i++)
    {
        libmodbusrtu_modbus_rx_byte(&guarded.context, prng_byte());
    }
}

/* Reset observations between sub-scenarios within a test. */
static void reset_observations(void)
{
    read_calls = 0U;
    write_calls = 0U;
    last_written_value = 0U;
    response_count = 0U;
    response_len = 0U;
}

/* Full recovery: discard exit + valid command + drain. */
static bool recover_and_execute(uint16_t value)
{
    advance_silence();
    feed_command_value(1U, value);
    advance_silence();
    drain_poll();
    return (write_calls > 0U) && (last_written_value == value);
}

void setUp(void)
{
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0xA5, sizeof(guarded.before));
    memset(guarded.after, 0xA5, sizeof(guarded.after));
    fake_tick = 0U;
    /* PRNG: seeded by each test function, NOT here
     * (looped setUp calls would reset to the same seed). */
    reset_observations();
    libmodbusrtu_slave_init(&guarded.context, 1U, get_tick,
                            capture_response);
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

/* -- Fuzz test 1: garbage bursts never execute, recovery always works -- */
void test_fuzz_garbage_bursts_never_execute_and_recovery_works(void)
{
    prng_seed(FUZZ_SEED);
    for (uint32_t round = 0U; FUZZ_GARBAGE_ROUNDS > round; round++)
    {
        /* Random garbage burst (1..64 bytes) at the current tick. */
        reset_observations();
        const size_t burst_len = (size_t)(prng_next() % 64U) + 1U;
        feed_garbage(burst_len);
        advance_silence();
        drain_poll();

        /* No valid command may execute from a garbage burst. */
        if (write_calls > 0U)
        {
            TEST_FAIL_MESSAGE("Command executed from garbage burst");
        }

        /* Recovery: a known command must always work after silence. */
        if (!recover_and_execute((uint16_t)(round + 0x100U)))
        {
            TEST_FAIL_MESSAGE("Recovery failed after garbage burst");
        }
    }
}

/* -- Fuzz test 2: command identity through noise interleave --
 * NOTE: This tests A -> poll -> noise -> poll -> B flow (commands are
 * processed between noise bursts). The 'multiple commands during delayed
 * poll' scenario (A/B/C stacked without polling) is covered by the
 * deterministic regression in test_modbus_frame_resync.c, NOT here. */
void test_fuzz_command_identity_through_noise_interleave(void)
{
    prng_seed(FUZZ_SEED + 4U);
    for (uint32_t round = 0U; FUZZ_INTERLEAVE > round; round++)
    {
        reset_observations();
        const uint16_t val_a = (uint16_t)(0x0A00U + round);
        const uint16_t val_b = (uint16_t)(0x0B00U + round);

        /* Command A completes. */
        feed_command_value(1U, val_a);
        advance_silence();
        drain_poll();
        TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
        TEST_ASSERT_EQUAL_UINT16(val_a, last_written_value);

        /* Garbage noise arrives before the poll (discard state). */
        reset_observations();
        feed_garbage(32U);
        advance_silence();
        drain_poll();
        TEST_ASSERT_EQUAL_UINT32(0U, write_calls);

        /* Command B after recovery: identity preserved. */
        if (!recover_and_execute(val_b))
        {
            TEST_FAIL_MESSAGE("Command B identity lost");
        }
        TEST_ASSERT_EQUAL_UINT16(val_b, last_written_value);
        TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
    }
}

/* -- Fuzz test 3: tick wrap during discard + recovery -- */
void test_fuzz_tick_wrap_discard_and_recovery(void)
{
    prng_seed(FUZZ_SEED + 1U);  /* different data from garbage test */
    for (uint32_t round = 0U; FUZZ_INTERLEAVE > round; round++)
    {
        /* Reset context WITHOUT re-seeding the PRNG (varied garbage). */
        memset(&guarded, 0, sizeof(guarded));
        memset(guarded.before, 0xA5, sizeof(guarded.before));
        memset(guarded.after, 0xA5, sizeof(guarded.after));
        fake_tick = 0U;
        reset_observations();
        libmodbusrtu_slave_init(&guarded.context, 1U, get_tick,
                                capture_response);
        libmodbusrtu_register_read_callback(&guarded.context, read_register);
        libmodbusrtu_register_write_callback(&guarded.context,
                                             write_register);

        /* Start near wrap; garbage pushes past the boundary. */
        fake_tick = FUZZ_TICK_WRAP_VAL - 4U;
        feed_garbage(260U);  /* > buffer size -> discard state */
        TEST_ASSERT_TRUE(guarded.context.discard);

        /* Silence crosses the wrap boundary. */
        /* Post-wrap silence: gap must exceed frame_silence_ms (10ms). */
        fake_tick = 16U;     /* gap from ~MAX to 16 ≈ 20 >= 10 */
        feed_command_value(1U, 0x0C0CU);
        fake_tick += (uint32_t)guarded.context.frame_silence_ms + 1U;
        drain_poll();

        /* Command accepted despite the wrap. */
        TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
        TEST_ASSERT_EQUAL_UINT16(0x0C0CU, last_written_value);
    }
}

/* -- Fuzz test 4: overflow at exact boundary + error injection -- */
void test_fuzz_overflow_boundary_and_error_injection_recovery(void)
{
    prng_seed(FUZZ_SEED + 2U);
    for (uint32_t round = 0U; FUZZ_INTERLEAVE > round; round++)
    {
        memset(&guarded, 0, sizeof(guarded));
        memset(guarded.before, 0xA5, sizeof(guarded.before));
        memset(guarded.after, 0xA5, sizeof(guarded.after));
        fake_tick = 0U;
        reset_observations();
        libmodbusrtu_slave_init(&guarded.context, 1U, get_tick,
                                capture_response);
        libmodbusrtu_register_read_callback(&guarded.context, read_register);
        libmodbusrtu_register_write_callback(&guarded.context,
                                             write_register);

        /* Fill buffer exactly to overflow boundary. */
        reset_observations();
        for (uint32_t i = 0U; (MODBUS_BUFFER_SIZE + 1U) > i; i++)
        {
            libmodbusrtu_modbus_rx_byte(&guarded.context, prng_byte());
        }
        TEST_ASSERT_TRUE(guarded.context.discard);

        /* Inject a line error mid-discard (double hit). */
        libmodbusrtu_modbus_rx_error(&guarded.context);
        TEST_ASSERT_TRUE(guarded.context.discard);
        advance_silence();
        drain_poll();
        TEST_ASSERT_EQUAL_UINT32(0U, write_calls);

        /* Recovery still works after the double hit. */
        if (!recover_and_execute(0x0D0DU))
        {
            TEST_FAIL_MESSAGE("Recovery failed after overflow + error");
        }
    }
}

/* -- Fuzz test 5: mixed valid/invalid frames in rapid succession -- */
void test_fuzz_mixed_valid_invalid_alternating_frames(void)
{
    prng_seed(FUZZ_SEED + 3U);
    for (uint32_t round = 0U; FUZZ_GARBAGE_ROUNDS / 4U > round; round++)
    {
        memset(&guarded, 0, sizeof(guarded));
        memset(guarded.before, 0xA5, sizeof(guarded.before));
        memset(guarded.after, 0xA5, sizeof(guarded.after));
        fake_tick = 0U;
        reset_observations();
        libmodbusrtu_slave_init(&guarded.context, 1U, get_tick,
                                capture_response);
        libmodbusrtu_register_read_callback(&guarded.context, read_register);
        libmodbusrtu_register_write_callback(&guarded.context,
                                             write_register);

        /* Alternating: valid, garbage tail, valid, error, valid. */
        const uint16_t v1 = (uint16_t)(0x1100U + round);

        /* 1. Valid command. */
        feed_command_value(1U, v1);
        advance_silence();
        drain_poll();
        TEST_ASSERT_EQUAL_UINT32(1U, write_calls);
        TEST_ASSERT_EQUAL_UINT16(v1, last_written_value);

        /* 2. Garbage overflow + valid FC06 appended in SAME burst
         * (no silence gap): the valid tail must NOT execute. */
        reset_observations();
        feed_garbage(MODBUS_BUFFER_SIZE + 4U);
        feed_command_value(1U, 0xDEADU);  /* appended, no gap */
        advance_silence();
        drain_poll();
        TEST_ASSERT_EQUAL_UINT32(0U, write_calls);
        TEST_ASSERT_NOT_EQUAL_UINT16(0xDEADU, last_written_value);

        /* 3. Error + recovery. */
        libmodbusrtu_modbus_rx_error(&guarded.context);
        advance_silence();
        if (!recover_and_execute(0x0E0EU))
        {
            TEST_FAIL_MESSAGE("Recovery after error failed in mixed test");
        }

        /* 4. Buffer canaries intact (tearDown also checks). */
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.before[0]);
        TEST_ASSERT_EQUAL_HEX8(0xA5U, guarded.after[15]);
    }
}
