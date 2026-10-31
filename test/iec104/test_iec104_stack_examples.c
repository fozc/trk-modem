/**
 * @file test_iec104_stack_examples.c
 * @brief Run documented application callbacks through the real server/core entry points.
 * Created on: Oct 30, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "iec104_example.h"

#include <string.h>

TEST_SOURCE_FILE("iec104_asdu.c")
TEST_SOURCE_FILE("iec104_core.c")
TEST_SOURCE_FILE("iec104_server.c")
TEST_SOURCE_FILE("iec104_example.c")

static iec104_server_t server;
static iec104_example_data_t values;
static uint8_t frames[8][255];
static size_t lengths[8];
static size_t frame_count;

static bool capture(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    TEST_ASSERT_LESS_THAN(8U, frame_count);
    memcpy(frames[frame_count], data, length);
    lengths[frame_count++] = length;
    return true;
}

static void query(uint8_t qoi)
{
    uint8_t command[] = { 0x68U, 14U, 0U, 0U, 0U, 0U, 100U, 1U, 6U, 0x3CU, 1U, 0U, 0U, 0U, 0U, qoi };
    iec104_write_u16(&command[2], (uint16_t)(server.core.receive_sequence << 1U));
    iec104_write_u16(&command[4], (uint16_t)(server.core.send_sequence << 1U));
    TEST_ASSERT_EQUAL_size_t(sizeof(command), iec104_server_process_data(&server, command, sizeof(command), 2U));
    for (uint32_t time = 3U; time < 10U; time++)
    {
        iec104_server_poll(&server, time);
    }
}

void setUp(void)
{
    frame_count                         = 0U;
    values                              = (iec104_example_data_t){ .status       = true,
                                                                   .double_state = 2U,
                                                                   .measurement  = -2.5F,
                                                                   .quality      = 0x80U,
                                                                   .bits         = 0x12345678U,
                                                                   .time         = { .milliseconds = 58987U,
                                                                                     .minute       = 57U,
                                                                                     .hour         = 23U,
                                                                                     .day          = 25U,
                                                                                     .day_of_week  = 7U,
                                                                                     .month        = 10U,
                                                                                     .year         = 26U } };
    const iec104_core_config_t link     = { 12U, 8U, 15000U, 10000U, 20000U };
    const iec104_server_config_t config = { .common_address = 1U };
    const iec104_server_io_t io         = { .context       = &values,
                                            .write         = capture,
                                            .handle_request = iec104_example_handle_request };
    TEST_ASSERT_TRUE(iec104_server_init(&server, &link, &config, &io));
    iec104_server_open(&server, 0U);
    const uint8_t start[] = { 0x68U, 4U, 7U, 0U, 0U, 0U };
    (void)iec104_server_process_data(&server, start, sizeof(start), 0U);
    frame_count = 0U;
}

void tearDown(void)
{
}

void test_stack_examples_station_query_reads_ioas_and_splits_types_with_extension_golden_frame(void)
{
    query(20U);
    TEST_ASSERT_EQUAL_size_t(5U, frame_count);
    TEST_ASSERT_EQUAL_UINT8(7U, frames[0][8]);
    TEST_ASSERT_EQUAL_UINT8(1U, frames[1][6]);
    TEST_ASSERT_EQUAL_UINT32(100U, iec104_read_ioa(&frames[1][12]));
    TEST_ASSERT_EQUAL_UINT8(13U, frames[2][6]);
    TEST_ASSERT_EQUAL_UINT32(101U, iec104_read_ioa(&frames[2][12]));
    const uint8_t expected[] = { 0x68U, 18U, 8U,   0U, 2U, 0U,    7U,    1U,    20U,   0x3CU,
                                 1U,    0U,  105U, 0U, 0U, 0x78U, 0x56U, 0x34U, 0x12U, 0x80U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[3], sizeof(expected));
    TEST_ASSERT_EQUAL_UINT8(10U, frames[4][8]);
}

void test_stack_examples_group_query_uses_timed_single_double_and_float_encoders(void)
{
    query(21U);
    const uint8_t types[] = { 30U, 31U, 36U };
    TEST_ASSERT_EQUAL_size_t(5U, frame_count);
    for (size_t index = 0U; index < 3U; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(types[index], frames[index + 1U][6]);
        TEST_ASSERT_EQUAL_UINT8(21U, frames[index + 1U][8]);
        TEST_ASSERT_EQUAL_UINT32(102U + index, iec104_read_ioa(&frames[index + 1U][12]));
    }
    TEST_ASSERT_EQUAL_UINT8(0x82U, frames[2][15]);
    const uint8_t time[] = { 0x6BU, 0xE6U, 57U, 23U, 0xF9U, 10U, 26U };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(time, &frames[3][20], sizeof(time));
}

void test_stack_examples_unknown_group_gets_negative_confirmation_without_data(void)
{
    query(23U);
    const uint8_t expected[] = { 0x68U, 14U, 2U, 0U, 2U, 0U, 100U, 1U, 0x47U, 0x3CU, 1U, 0U, 0U, 0U, 0U, 23U };
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
}

void test_stack_examples_invalid_extension_preserves_previous_point(void)
{
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_example_set_bitstring(&point, 105U, 0x12345678U, 0U));
    iec104_point_t before = point;
    TEST_ASSERT_FALSE(iec104_example_set_bitstring(&point, 105U, 0U, 2U));
    TEST_ASSERT_EQUAL_MEMORY(&before, &point, sizeof(point));
}

void test_stack_examples_periodic_entry_emits_selected_measurement_with_cot_one(void)
{
    TEST_ASSERT_TRUE(iec104_example_publish_periodic(&server, &values));
    const uint8_t expected[] = {0x68U, 18U, 2U, 0U, 0U, 0U, 13U, 1U,
        1U, 0U, 1U, 0U, 101U, 0U, 0U, 0U, 0U, 0x20U, 0xC0U, 0x80U};
    TEST_ASSERT_EQUAL_size_t(1U, frame_count);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), lengths[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frames[0], sizeof(expected));
    TEST_ASSERT_FALSE(iec104_example_publish_periodic(&server, NULL));
}

/*** end of file ***/
