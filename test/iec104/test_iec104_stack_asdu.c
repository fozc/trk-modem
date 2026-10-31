/**
 * @file test_iec104_stack_asdu.c
 * @brief Typed host values through real ASDU/core encoding against literal full frames.
 * Created on: Oct 30, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "iec104_core.h"

#include <string.h>

TEST_SOURCE_FILE("iec104_asdu.c")
TEST_SOURCE_FILE("iec104_core.c")

static iec104_core_t core;
static uint8_t captured[IEC104_CORE_FRAME_MAX];
static size_t captured_length;
static const iec104_time_t source_time = {
    .milliseconds = 58987U, .minute = 57U, .hour = 23U, .day = 25U, .day_of_week = 7U, .month = 10U, .year = 26U
};

static bool capture(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(captured), length);
    memcpy(captured, data, length);
    captured_length = length;
    return true;
}

static bool receive(void *context, const iec104_asdu_t *asdu)
{
    (void)context;
    (void)asdu;
    return true;
}

/** The golden bytes below include APCI, ASDU header, IOAs and all values. */
static void send_points(const iec104_point_t *points, size_t count, bool sequence)
{
    iec104_asdu_t header = { .type       = points[0].type,
                             .cause      = IEC104_COT_SPONTANEOUS,
                             .address    = 0x1234U,
                             .originator = 0x5AU,
                             .sequence   = sequence };
    iec104_builder_t builder;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &header, points[0].value_size));
    for (size_t index = 0U; index < count; index++)
    {
        TEST_ASSERT_TRUE(iec104_builder_add(&builder, points[index].ioa, points[index].value));
    }
    TEST_ASSERT_TRUE(iec104_core_send(&core, builder.data, builder.length, 0U));
}

void setUp(void)
{
    const iec104_core_config_t config = { 12U, 8U, 15000U, 10000U, 20000U };
    const iec104_core_io_t io         = { .write = capture, .receive = receive };
    const uint8_t start[]             = { 0x68U, 4U, 7U, 0U, 0U, 0U };
    TEST_ASSERT_TRUE(iec104_core_init(&core, &config, &io));
    iec104_core_open(&core, 0U);
    TEST_ASSERT_EQUAL_size_t(sizeof(start), iec104_core_process_data(&core, start, sizeof(start), 0U));
    captured_length = 0U;
}

void tearDown(void)
{
}

void test_stack_asdu_single_untimed_full_frame(void)
{
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single(&point, 0x010203U, true, IEC104_QUALITY_INVALID, NULL));
    send_points(&point, 1U, false);
    const uint8_t expected[] = { 0x68U, 14U, 0U, 0U, 0U, 0U, 1U, 1U, 3U, 0x5AU, 0x34U, 0x12U, 3U, 2U, 1U, 0x81U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_float_untimed_full_frame(void)
{
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_float(&point, 0x010203U, -2.5F, 0x41U, NULL));
    send_points(&point, 1U, false);
    const uint8_t expected[] = { 0x68U, 18U,   0U, 0U, 0U, 0U, 13U, 1U,    3U,    0x5AU,
                                 0x34U, 0x12U, 3U, 2U, 1U, 0U, 0U,  0x20U, 0xC0U, 0x41U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_timed_single_invalid_and_summer_time_full_frame(void)
{
    iec104_time_t time = source_time;
    time.invalid       = true;
    time.summer_time   = true;
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single(&point, 0x010203U, false, 0x50U, &time));
    send_points(&point, 1U, false);
    const uint8_t expected[] = { 0x68U, 21U, 0U, 0U,    0U,    0U,    30U,   1U,    3U,    0x5AU, 0x34U, 0x12U,
                                 3U,    2U,  1U, 0x50U, 0x6BU, 0xE6U, 0xB9U, 0x97U, 0xF9U, 10U,   26U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_double_point_all_states_full_frame(void)
{
    iec104_point_t points[4];
    for (uint8_t value = 0U; value < 4U; value++)
    {
        TEST_ASSERT_TRUE(iec104_point_set_double(&points[value], (uint32_t)value + 1U, value, 0x80U, &source_time));
    }
    send_points(points, 4U, false);
    const uint8_t expected[] = { 0x68U, 54U,   0U,    0U,    0U,    0U,    31U,   4U,    3U,    0x5AU, 0x34U, 0x12U,
                                 1U,    0U,    0U,    0x80U, 0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U,   26U,   2U,
                                 0U,    0U,    0x81U, 0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U,   26U,   3U,    0U,
                                 0U,    0x82U, 0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U,   26U,   4U,    0U,    0U,
                                 0x83U, 0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U,   26U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_timed_float_two_objects_full_frame(void)
{
    iec104_point_t points[2];
    TEST_ASSERT_TRUE(iec104_point_set_float(&points[0], 0x010203U, -2.5F, 0U, &source_time));
    TEST_ASSERT_TRUE(iec104_point_set_float(&points[1], 0x010205U, 1.0F, 0x81U, &source_time));
    send_points(points, 2U, false);
    const uint8_t expected[] = {
        0x68U, 40U, 0U, 0U,    0U,    0U,    36U,   2U,    3U,    0x5AU, 0x34U, 0x12U, 3U,  2U,
        1U,    0U,  0U, 0x20U, 0xC0U, 0U,    0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U,   26U, 5U,
        2U,    1U,  0U, 0U,    0x80U, 0x3FU, 0x81U, 0x6BU, 0xE6U, 57U,   23U,   0xF9U, 10U, 26U
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_sequence_omits_later_ioas_full_frame(void)
{
    iec104_point_t points[2];
    TEST_ASSERT_TRUE(iec104_point_set_single(&points[0], 0xFFFFFEU, true, 0U, NULL));
    TEST_ASSERT_TRUE(iec104_point_set_single(&points[1], 0xFFFFFFU, false, 0x80U, NULL));
    send_points(points, 2U, true);
    const uint8_t expected[] = { 0x68U, 15U,   0U,    0U,    0U,    0U,    1U, 0x82U, 3U,
                                 0x5AU, 0x34U, 0x12U, 0xFEU, 0xFFU, 0xFFU, 1U, 0x80U };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
}

void test_stack_asdu_invalid_setter_arguments_preserve_valid_point(void)
{
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single(&point, 123U, true, 0U, NULL));
    iec104_point_t before = point;
    TEST_ASSERT_FALSE(iec104_point_set_single(&point, 0x1000000U, false, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_single(&point, 1U, false, 1U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_float(&point, 1U, 1.0F, 2U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_float(&point, 0x1000000U, 1.0F, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_double(&point, 1U, 4U, 0U, &source_time));
    TEST_ASSERT_FALSE(iec104_point_set_double(&point, 1U, 1U, 1U, &source_time));
    TEST_ASSERT_FALSE(iec104_point_set_double(&point, 0x1000000U, 1U, 0U, &source_time));
    TEST_ASSERT_FALSE(iec104_point_set_double(&point, 1U, 1U, 0U, NULL));
    TEST_ASSERT_EQUAL_MEMORY(&before, &point, sizeof(point));
    TEST_ASSERT_FALSE(iec104_point_set_single(NULL, 1U, true, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_float(NULL, 1U, 0.0F, 0U, NULL));
    TEST_ASSERT_FALSE(iec104_point_set_double(NULL, 1U, 0U, 0U, &source_time));
}

void test_stack_asdu_clock_parses_host_fields_and_preserves_output_on_invalid_input(void)
{
    uint8_t wire[] = { 0x6BU, 0xE6U, 57U, 0x97U, 0xF9U, 10U, 26U };
    iec104_time_t time;
    TEST_ASSERT_TRUE(iec104_time_parse(&time, wire));
    TEST_ASSERT_EQUAL_UINT16(58987U, time.milliseconds);
    TEST_ASSERT_EQUAL_UINT8(57U, time.minute);
    TEST_ASSERT_EQUAL_UINT8(23U, time.hour);
    TEST_ASSERT_EQUAL_UINT8(25U, time.day);
    TEST_ASSERT_EQUAL_UINT8(7U, time.day_of_week);
    TEST_ASSERT_EQUAL_UINT8(10U, time.month);
    TEST_ASSERT_EQUAL_UINT8(26U, time.year);
    TEST_ASSERT_TRUE(time.summer_time);
    TEST_ASSERT_FALSE(time.invalid);
    iec104_time_t before = time;
    wire[2]              = 0x80U;
    TEST_ASSERT_FALSE(iec104_time_parse(&time, wire));
    TEST_ASSERT_FALSE(iec104_time_parse(&time, NULL));
    TEST_ASSERT_FALSE(iec104_time_parse(NULL, wire));
    TEST_ASSERT_EQUAL_MEMORY(&before, &time, sizeof(time));
}

void test_stack_asdu_encoded_extension_bounds_and_alias_preserve_previous_data(void)
{
    iec104_point_t point;
    const uint8_t value[] = { 0x78U, 0x56U, 0x34U, 0x12U, 0x80U };
    TEST_ASSERT_TRUE(iec104_point_set_encoded(&point, 5U, 7U, value, sizeof(value)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(value, point.value, sizeof(value));
    TEST_ASSERT_EQUAL_size_t(sizeof(value), point.value_size);
    TEST_ASSERT_TRUE(iec104_point_set_encoded(&point, 9U, 200U, point.value, point.value_size));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(value, point.value, sizeof(value));
    TEST_ASSERT_EQUAL_UINT32(9U, point.ioa);
    TEST_ASSERT_EQUAL_UINT8(200U, point.type);
    iec104_point_t before = point;
    TEST_ASSERT_FALSE(iec104_point_set_encoded(&point, 1U, 7U, value, IEC104_POINT_VALUE_MAX + 1U));
    TEST_ASSERT_FALSE(iec104_point_set_encoded(&point, 1U, 7U, value, 0U));
    TEST_ASSERT_FALSE(iec104_point_set_encoded(&point, 0x1000000U, 7U, value, sizeof(value)));
    TEST_ASSERT_FALSE(iec104_point_set_encoded(&point, 1U, 0U, value, sizeof(value)));
    TEST_ASSERT_FALSE(iec104_point_set_encoded(&point, 1U, 7U, NULL, sizeof(value)));
    TEST_ASSERT_FALSE(iec104_point_set_encoded(NULL, 1U, 7U, value, sizeof(value)));
    TEST_ASSERT_EQUAL_MEMORY(&before, &point, sizeof(point));
}

void test_stack_asdu_single_command_select_wire_and_host_fields(void)
{
    iec104_point_t point;
    TEST_ASSERT_TRUE(iec104_point_set_single_command(&point, 10001U, true, true, 0U));
    iec104_asdu_t header = { .type = point.type, .cause = 6U, .address = 0x1234U, .originator = 0x5AU };
    iec104_builder_t builder;
    TEST_ASSERT_TRUE(iec104_builder_begin(&builder, &header, point.value_size));
    TEST_ASSERT_TRUE(iec104_builder_add(&builder, point.ioa, point.value));
    TEST_ASSERT_TRUE(iec104_core_send(&core, builder.data, builder.length, 0U));
    const uint8_t expected[] = {0x68U, 14U, 0U, 0U, 0U, 0U, 45U, 1U,
        6U, 0x5AU, 0x34U, 0x12U, 0x11U, 0x27U, 0U, 0x81U};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), captured_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, captured, sizeof(expected));
    iec104_single_command_t command;
    TEST_ASSERT_TRUE(iec104_single_command_parse(&command, 0xFDU));
    TEST_ASSERT_TRUE(command.state);
    TEST_ASSERT_TRUE(command.select);
    TEST_ASSERT_EQUAL_UINT8(31U, command.qualifier);
    iec104_single_command_t before = command;
    TEST_ASSERT_FALSE(iec104_single_command_parse(&command, 0x83U));
    TEST_ASSERT_FALSE(iec104_single_command_parse(NULL, 0U));
    TEST_ASSERT_EQUAL_MEMORY(&before, &command, sizeof(command));
    iec104_point_t previous = point;
    TEST_ASSERT_FALSE(iec104_point_set_single_command(&point, 10001U, true, false, 32U));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &point, sizeof(point));
}

/*** end of file ***/
