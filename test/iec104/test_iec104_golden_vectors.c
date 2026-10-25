/*
 * test_iec104_golden_vectors.c
 *
 *  Created on: Oct 25, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Full wire-frame regression tests through production IEC104 entry points.
 * Expected octets are literal IEC104/IEEE754 vectors, not serializer output.
 * Sources: IEC60870-5-104_Referans.md sections 2, 5, 8, 9 and 10.
 * Single-object-only procedures stay single-object; unsupported 45/46
 * commands are tested as rejections, without enabling their handlers.
 */

#include "unity.h"

#include "iec104.h"
#include "iec104_config.h"
#include "iec104_platform_fake.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

TEST_SOURCE_FILE("iec104_platform_fake.c")
TEST_SOURCE_FILE("iec104_config.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("cp56time2a.c")

/* CA=0x1234, configured OA=0x5A, request OA=0x3C.
 * Floats: 1.0=3F800000, -2.5=C0200000, 0.15625=3E200000.
 * Sample time: 2026-10-25 Sunday 23:57:58.987, SU=1.
 * The existing RTC fake returns 2026-03-11 Wednesday 12:23:47.000. */

static const uint8_t start_act[] =
{
    0x68U, 0x04U, 0x07U, 0x00U, 0x00U, 0x00U
};

static const uint8_t start_con[] =
{
    0x68U, 0x04U, 0x0BU, 0x00U, 0x00U, 0x00U
};

static const uint8_t initialization[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x46U, 0x01U, 0x04U, 0x5AU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x00U
};

static const uint8_t ack_init[] =
{
    0x68U, 0x04U, 0x01U, 0x00U, 0x02U, 0x00U
};

static const uint8_t test_act[] =
{
    0x68U, 0x04U, 0x43U, 0x00U, 0x00U, 0x00U
};

static const uint8_t test_con[] =
{
    0x68U, 0x04U, 0x83U, 0x00U, 0x00U, 0x00U
};

static const uint8_t stop_act[] =
{
    0x68U, 0x04U, 0x13U, 0x00U, 0x00U, 0x00U
};

static const uint8_t stop_con[] =
{
    0x68U, 0x04U, 0x23U, 0x00U, 0x00U, 0x00U
};

static const uint8_t s_ack[] =
{
    0x68U, 0x04U, 0x01U, 0x00U, 0x68U, 0x24U
};

static const uint8_t sp_single[] =
{
    0x68U, 0x15U, 0x02U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x01U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0xF1U, 0x98U, 0xB7U, 0x17U, 0x0CU,
    0x6BU, 0x03U, 0x1AU
};

static const uint8_t dp_single[] =
{
    0x68U, 0x15U, 0x02U, 0x00U, 0x00U, 0x00U, 0x1FU, 0x01U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0xA2U, 0x98U, 0xB7U, 0x17U, 0x0CU,
    0x6BU, 0x03U, 0x1AU
};

static const uint8_t me_single[] =
{
    0x68U, 0x19U, 0x02U, 0x00U, 0x00U, 0x00U, 0x24U, 0x01U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x00U, 0x00U, 0x20U, 0xC0U, 0x41U,
    0x98U, 0xB7U, 0x17U, 0x0CU, 0x6BU, 0x03U, 0x1AU
};

static const uint8_t me_wrap[] =
{
    0x68U, 0x19U, 0xFEU, 0xFFU, 0xFEU, 0xFFU, 0x24U, 0x01U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x00U, 0x00U, 0x20U, 0xC0U, 0x41U,
    0x98U, 0xB7U, 0x17U, 0x0CU, 0x6BU, 0x03U, 0x1AU
};

static const uint8_t gi20_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x14U
};

static const uint8_t gi20_con[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x07U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x14U
};

static const uint8_t gi20_me[] =
{
    0x68U, 0x22U, 0x04U, 0x00U, 0x02U, 0x00U, 0x0DU, 0x03U, 0x14U, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x00U, 0x00U, 0x80U, 0x3FU, 0x01U,
    0x57U, 0x34U, 0x12U, 0x00U, 0x00U, 0x20U, 0xC0U, 0x30U, 0x58U, 0x34U,
    0x12U, 0x00U, 0x00U, 0x20U, 0x3EU, 0xC0U
};

static const uint8_t gi20_energy[] =
{
    0x68U, 0x16U, 0x06U, 0x00U, 0x02U, 0x00U, 0x01U, 0x03U, 0x14U, 0x3CU,
    0x34U, 0x12U, 0x67U, 0x45U, 0x23U, 0x01U, 0x68U, 0x45U, 0x23U, 0x30U,
    0x69U, 0x45U, 0x23U, 0xC1U
};

static const uint8_t gi20_load[] =
{
    0x68U, 0x16U, 0x08U, 0x00U, 0x02U, 0x00U, 0x01U, 0x03U, 0x14U, 0x3CU,
    0x34U, 0x12U, 0x78U, 0x56U, 0x34U, 0x01U, 0x79U, 0x56U, 0x34U, 0x30U,
    0x7AU, 0x56U, 0x34U, 0xC1U
};

static const uint8_t gi20_rf[] =
{
    0x68U, 0x16U, 0x0AU, 0x00U, 0x02U, 0x00U, 0x01U, 0x03U, 0x14U, 0x3CU,
    0x34U, 0x12U, 0x89U, 0x67U, 0x45U, 0x01U, 0x8AU, 0x67U, 0x45U, 0x30U,
    0x8BU, 0x67U, 0x45U, 0xC1U
};

static const uint8_t gi20_term[] =
{
    0x68U, 0x0EU, 0x0CU, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x0AU, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x14U
};

static const uint8_t gi21_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x15U
};

static const uint8_t gi21_con[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x07U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x15U
};

static const uint8_t gi21_me[] =
{
    0x68U, 0x37U, 0x04U, 0x00U, 0x02U, 0x00U, 0x24U, 0x03U, 0x15U, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x00U, 0x00U, 0x80U, 0x3FU, 0x01U,
    0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU, 0x57U, 0x34U, 0x12U,
    0x00U, 0x00U, 0x20U, 0xC0U, 0x30U, 0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U,
    0x0AU, 0x1AU, 0x58U, 0x34U, 0x12U, 0x00U, 0x00U, 0x20U, 0x3EU, 0xC0U,
    0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU
};

static const uint8_t gi21_term[] =
{
    0x68U, 0x0EU, 0x06U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x0AU, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x15U
};

static const uint8_t gi22_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x16U
};

static const uint8_t gi22_con[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x07U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x16U
};

static const uint8_t gi22_energy[] =
{
    0x68U, 0x2BU, 0x04U, 0x00U, 0x02U, 0x00U, 0x1EU, 0x03U, 0x16U, 0x3CU,
    0x34U, 0x12U, 0x67U, 0x45U, 0x23U, 0x01U, 0x6BU, 0xE6U, 0x39U, 0x97U,
    0xF9U, 0x0AU, 0x1AU, 0x68U, 0x45U, 0x23U, 0x30U, 0x6BU, 0xE6U, 0x39U,
    0x97U, 0xF9U, 0x0AU, 0x1AU, 0x69U, 0x45U, 0x23U, 0xC1U, 0x6BU, 0xE6U,
    0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU
};

static const uint8_t gi22_load[] =
{
    0x68U, 0x2BU, 0x06U, 0x00U, 0x02U, 0x00U, 0x1EU, 0x03U, 0x16U, 0x3CU,
    0x34U, 0x12U, 0x78U, 0x56U, 0x34U, 0x01U, 0x6BU, 0xE6U, 0x39U, 0x97U,
    0xF9U, 0x0AU, 0x1AU, 0x79U, 0x56U, 0x34U, 0x30U, 0x6BU, 0xE6U, 0x39U,
    0x97U, 0xF9U, 0x0AU, 0x1AU, 0x7AU, 0x56U, 0x34U, 0xC1U, 0x6BU, 0xE6U,
    0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU
};

static const uint8_t gi22_rf[] =
{
    0x68U, 0x2BU, 0x08U, 0x00U, 0x02U, 0x00U, 0x1EU, 0x03U, 0x16U, 0x3CU,
    0x34U, 0x12U, 0x89U, 0x67U, 0x45U, 0x01U, 0x6BU, 0xE6U, 0x39U, 0x97U,
    0xF9U, 0x0AU, 0x1AU, 0x8AU, 0x67U, 0x45U, 0x30U, 0x6BU, 0xE6U, 0x39U,
    0x97U, 0xF9U, 0x0AU, 0x1AU, 0x8BU, 0x67U, 0x45U, 0xC1U, 0x6BU, 0xE6U,
    0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU
};

static const uint8_t gi22_term[] =
{
    0x68U, 0x0EU, 0x0AU, 0x00U, 0x02U, 0x00U, 0x64U, 0x01U, 0x0AU, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x16U
};

static const uint8_t fault_me[] =
{
    0x68U, 0x28U, 0x02U, 0x00U, 0x00U, 0x00U, 0x24U, 0x02U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x3CU, 0x00U, 0x01U, 0x00U, 0x00U, 0x48U, 0x41U, 0x00U,
    0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU, 0x3DU, 0x00U, 0x01U,
    0x00U, 0x00U, 0x7AU, 0x44U, 0x00U, 0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U,
    0x0AU, 0x1AU
};

static const uint8_t fault_sp[] =
{
    0x68U, 0x20U, 0x04U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x02U, 0x03U, 0x5AU,
    0x34U, 0x12U, 0x3EU, 0x00U, 0x01U, 0x01U, 0x6BU, 0xE6U, 0x39U, 0x97U,
    0xF9U, 0x0AU, 0x1AU, 0x3FU, 0x00U, 0x01U, 0x00U, 0x6BU, 0xE6U, 0x39U,
    0x97U, 0xF9U, 0x0AU, 0x1AU
};

static const uint8_t clock_request[] =
{
    0x68U, 0x14U, 0x00U, 0x00U, 0x02U, 0x00U, 0x67U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U,
    0x0AU, 0x1AU
};

static const uint8_t clock_con[] =
{
    0x68U, 0x14U, 0x02U, 0x00U, 0x02U, 0x00U, 0x67U, 0x01U, 0x07U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x6BU, 0xE6U, 0x39U, 0x97U, 0xF9U,
    0x0AU, 0x1AU
};

static const uint8_t clock_invalid_request[] =
{
    0x68U, 0x14U, 0x00U, 0x00U, 0x02U, 0x00U, 0x67U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x6BU, 0xE6U, 0xB9U, 0x97U, 0xF9U,
    0x0AU, 0x1AU
};

static const uint8_t clock_negative[] =
{
    0x68U, 0x14U, 0x02U, 0x00U, 0x02U, 0x00U, 0x67U, 0x01U, 0x47U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x6BU, 0xE6U, 0xB9U, 0x97U, 0xF9U,
    0x0AU, 0x1AU
};

static const uint8_t reset_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t reset_con[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U, 0x07U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t reset_pending_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x02U
};

static const uint8_t reset_negative[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U, 0x47U, 0x3CU,
    0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x02U
};

static const uint8_t sc_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x2DU, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x81U
};

static const uint8_t sc_negative[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x2DU, 0x01U, 0x6CU, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x81U
};

static const uint8_t dc_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x2EU, 0x01U, 0x06U, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x82U
};

static const uint8_t dc_negative[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x2EU, 0x01U, 0x6CU, 0x3CU,
    0x34U, 0x12U, 0x56U, 0x34U, 0x12U, 0x82U
};


static const uint8_t wrong_cot_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U,
    0x14U, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t wrong_cot_negative[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U,
    0x6DU, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t wrong_ca_request[] =
{
    0x68U, 0x0EU, 0x00U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U,
    0x06U, 0x3CU, 0x35U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t wrong_ca_negative[] =
{
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U,
    0x6EU, 0x3CU, 0x35U, 0x12U, 0x00U, 0x00U, 0x00U, 0x01U
};

static const uint8_t combined_requests[] =
{
    0x68U, 0x14U, 0x00U, 0x00U, 0x02U, 0x00U, 0x67U, 0x01U,
    0x06U, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x6BU,
    0xE6U, 0x39U, 0x97U, 0xF9U, 0x0AU, 0x1AU,
    0x68U, 0x0EU, 0x02U, 0x00U, 0x02U, 0x00U, 0x69U, 0x01U,
    0x06U, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x02U
};

static const uint8_t combined_reset_negative[] =
{
    0x68U, 0x0EU, 0x04U, 0x00U, 0x04U, 0x00U, 0x69U, 0x01U,
    0x47U, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x02U
};

#define CAPTURE_COUNT 8U
#define CAPTURE_SIZE 255U

static uint8_t captured[CAPTURE_COUNT][CAPTURE_SIZE];
static size_t captured_len[CAPTURE_COUNT];
static size_t captured_count;
static iec104_event_t last_event;

static const cp56time2a_t sample_time =
{
    .milliseconds = 58987U, .minute = 57U, .hour = 23U,
    .day = 25U, .dow = 7U, .month = 10U, .year = 26U, .su_bit = 1U
};

static int capture_frame(const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_LESS_THAN_UINT32(CAPTURE_COUNT, captured_count);
    TEST_ASSERT_TRUE(length <= CAPTURE_SIZE);
    (void)memcpy(captured[captured_count], data, length);
    captured_len[captured_count] = length;
    captured_count++;
    return 0;
}

static void capture_event(iec104_event_t event)
{
    last_event = event;
}

static int read_measured(uint32_t line_index, uint8_t phase, float *value,
                         qds_t *quality, cp56time2a_t *timestamp)
{
    static const float values[] = {1.0F, -2.5F, 0.15625F};
    static const qds_t qualities[] =
    {
        {.overflow = 1U},
        {.blocked = 1U, .substituted = 1U},
        {.not_topical = 1U, .invalid = 1U}
    };
    (void)line_index;
    if (phase >= 3U)
    {
        return -1;
    }
    *value = values[phase];
    *quality = qualities[phase];
    *timestamp = sample_time;
    return 0;
}

static int read_state(uint32_t line_index, uint8_t phase, siq_t *value,
                      cp56time2a_t *timestamp)
{
    static const siq_t values[] =
    {
        {.spi = 1U},
        {.blocked = 1U, .substituted = 1U},
        {.spi = 1U, .not_topical = 1U, .invalid = 1U}
    };
    (void)line_index;
    if (phase >= 3U)
    {
        return -1;
    }
    *value = values[phase];
    *timestamp = sample_time;
    return 0;
}

static void configure_line(void)
{
    const iec104_line_config_t line =
    {
        .in_use = 1U,
        .anlik_akim = {{0x56U, 0x34U, 0x12U}, {0x57U, 0x34U, 0x12U},
                       {0x58U, 0x34U, 0x12U}},
        .enerji_varyok = {{0x67U, 0x45U, 0x23U}, {0x68U, 0x45U, 0x23U},
                          {0x69U, 0x45U, 0x23U}},
        .yuk_akimi_varyok = {{0x78U, 0x56U, 0x34U},
                             {0x79U, 0x56U, 0x34U},
                             {0x7AU, 0x56U, 0x34U}},
        .rf_haberlesme_varyok = {{0x89U, 0x67U, 0x45U},
                                 {0x8AU, 0x67U, 0x45U},
                                 {0x8BU, 0x67U, 0x45U}},
        .temporary_fault = {0U, 0U, 1U}
    };
    TEST_ASSERT_TRUE(iec104_set_line_config(0U, &line));
}

static void initialize_stack(void)
{
    const iec104_config_t config =
    {
        .common_address = 0x1234U, .originator_address = 0x5AU,
        .k_max = 64U, .w_max = 24U, .t1_max = 15U, .t2_max = 10U,
        .t3_max = 60U
    };
    const iec104_io_t io =
    {
        .send = capture_frame, .on_event = capture_event,
        .get_anlik_akim = read_measured,
        .get_enerji_varyok = read_state,
        .get_yuk_akimi_varyok = read_state,
        .get_rf_haberlesme_varyok = read_state
    };
    mock_platform_reset();
    captured_count = 0U;
    last_event = (iec104_event_t)0;
    iec104_init(&io, &config);
    configure_line();
}

static void feed_frame(const uint8_t *data, size_t length)
{
    TEST_ASSERT_TRUE(length <= CAPTURE_SIZE);
    iec104_data_received(data, (uint16_t)length);
    libiec104_poll();
}

static void assert_frame(size_t index, const uint8_t *expected, size_t length)
{
    TEST_ASSERT_TRUE(index < captured_count);
    TEST_ASSERT_EQUAL_UINT32(length, captured_len[index]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, captured[index], length);
}

void setUp(void)
{
    initialize_stack();
    feed_frame(start_act, sizeof(start_act));
    feed_frame(ack_init, sizeof(ack_init));
    captured_count = 0U;
}

void tearDown(void)
{
}

void test_golden_startdt_and_initialization_full_frames(void)
{
    initialize_stack();
    feed_frame(start_act, sizeof(start_act));
    TEST_ASSERT_EQUAL_UINT32(2U, captured_count);
    assert_frame(0U, start_con, sizeof(start_con));
    assert_frame(1U, initialization, sizeof(initialization));
}

void test_golden_testfr_full_frame(void)
{
    feed_frame(test_act, sizeof(test_act));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, test_con, sizeof(test_con));
}

void test_golden_stopdt_full_frame(void)
{
    feed_frame(stop_act, sizeof(stop_act));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, stop_con, sizeof(stop_con));
}

void test_golden_s_frame_preserves_both_sequence_octets(void)
{
    TEST_ASSERT_TRUE(iec104_send_s_frame(0x1234U));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, s_ack, sizeof(s_ack));
}

void test_golden_type30_single_point_full_frame(void)
{
    iec104_send_M_SP_TB_1_spontan((ioa_3byte_t){0x56U, 0x34U, 0x12U},
                                 1U, 0xF0U);
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, sp_single, sizeof(sp_single));
}

void test_golden_type31_single_double_point_full_frame(void)
{
    iec104_send_M_DP_TB_1_spontan((ioa_3byte_t){0x56U, 0x34U, 0x12U},
                                 2U, 0xA0U);
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, dp_single, sizeof(dp_single));
}

void test_golden_type36_single_float_full_frame(void)
{
    iec104_send_M_ME_TF_1((cot_t){.cause = COT_SPONTANEOUS},
        (ioa_3byte_t){0x56U, 0x34U, 0x12U}, -2.5F,
        (qds_t){.overflow = 1U, .not_topical = 1U});
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, me_single, sizeof(me_single));
}

void test_golden_type36_sequence_wrap_full_frame(void)
{
    static const uint8_t prepare[] =
    {
        0x68U, 0x0EU, 0xFCU, 0xFFU, 0x02U, 0x00U, 0x69U, 0x01U,
        0x06U, 0x3CU, 0x34U, 0x12U, 0x00U, 0x00U, 0x00U, 0x02U
    };
    /* Reach 32767 through a real exchange; the diagnostic setters
     * only accept values below 32767 in the existing public API. */
    iec_set_send_sn(32766U);
    iec_set_receive_sn(32766U);
    feed_frame(prepare, sizeof(prepare));
    captured_count = 0U;
    iec104_send_M_ME_TF_1((cot_t){.cause = COT_SPONTANEOUS},
        (ioa_3byte_t){0x56U, 0x34U, 0x12U}, -2.5F,
        (qds_t){.overflow = 1U, .not_topical = 1U});
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, me_wrap, sizeof(me_wrap));
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
}

void test_golden_types1_and13_multi_object_station_frames(void)
{
    feed_frame(gi20_request, sizeof(gi20_request));
    TEST_ASSERT_EQUAL_UINT32(6U, captured_count);
    assert_frame(0U, gi20_con, sizeof(gi20_con));
    assert_frame(1U, gi20_me, sizeof(gi20_me));
    assert_frame(2U, gi20_energy, sizeof(gi20_energy));
    assert_frame(3U, gi20_load, sizeof(gi20_load));
    assert_frame(4U, gi20_rf, sizeof(gi20_rf));
    assert_frame(5U, gi20_term, sizeof(gi20_term));
}

void test_golden_type36_multi_object_group_frames(void)
{
    feed_frame(gi21_request, sizeof(gi21_request));
    TEST_ASSERT_EQUAL_UINT32(3U, captured_count);
    assert_frame(0U, gi21_con, sizeof(gi21_con));
    assert_frame(1U, gi21_me, sizeof(gi21_me));
    assert_frame(2U, gi21_term, sizeof(gi21_term));
}

void test_golden_type30_multi_object_group_frames(void)
{
    feed_frame(gi22_request, sizeof(gi22_request));
    TEST_ASSERT_EQUAL_UINT32(5U, captured_count);
    assert_frame(0U, gi22_con, sizeof(gi22_con));
    assert_frame(1U, gi22_energy, sizeof(gi22_energy));
    assert_frame(2U, gi22_load, sizeof(gi22_load));
    assert_frame(3U, gi22_rf, sizeof(gi22_rf));
    assert_frame(4U, gi22_term, sizeof(gi22_term));
}

void test_golden_fault_replay_two_complete_multi_object_frames(void)
{
    const fault_log_t record =
    {
        .tm = sample_time, .fault_current = 125.0F,
        .fault_duration_ms = 1000U,
        .info = {.phase = 1U, .power_status = 1U}
    };
    TEST_ASSERT_TRUE(iec104_emit_evtlog_record(&record));
    TEST_ASSERT_EQUAL_UINT32(2U, captured_count);
    assert_frame(0U, fault_me, sizeof(fault_me));
    assert_frame(1U, fault_sp, sizeof(fault_sp));
}

void test_golden_clock_sync_full_response_and_rtc_application(void)
{
    feed_frame(clock_request, sizeof(clock_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, clock_con, sizeof(clock_con));
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    const rtc_t applied = mock_rtc_last_sync();
    TEST_ASSERT_EQUAL_UINT16(987U, applied.millisec);
    TEST_ASSERT_EQUAL_UINT8(58U, applied.second);
    TEST_ASSERT_EQUAL_UINT8(23U, applied.hour);
}

void test_golden_invalid_clock_full_negative_response(void)
{
    feed_frame(clock_invalid_request, sizeof(clock_invalid_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, clock_negative, sizeof(clock_negative));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
}

void test_golden_general_reset_full_confirmation(void)
{
    feed_frame(reset_request, sizeof(reset_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, reset_con, sizeof(reset_con));
    TEST_ASSERT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
}

void test_golden_pending_reset_full_negative_confirmation(void)
{
    feed_frame(reset_pending_request, sizeof(reset_pending_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, reset_negative, sizeof(reset_negative));
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
}

void test_golden_unsupported_single_command_full_negative_response(void)
{
    feed_frame(sc_request, sizeof(sc_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, sc_negative, sizeof(sc_negative));
}

void test_golden_unsupported_double_command_full_negative_response(void)
{
    feed_frame(dc_request, sizeof(dc_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, dc_negative, sizeof(dc_negative));
}

void test_golden_wrong_cot_full_negative_response(void)
{
    feed_frame(wrong_cot_request, sizeof(wrong_cot_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, wrong_cot_negative, sizeof(wrong_cot_negative));
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
}

void test_golden_wrong_common_address_full_negative_response(void)
{
    feed_frame(wrong_ca_request, sizeof(wrong_ca_request));
    TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
    assert_frame(0U, wrong_ca_negative, sizeof(wrong_ca_negative));
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
}

void test_golden_two_commands_in_one_tcp_chunk_preserve_both_frames(void)
{
    feed_frame(combined_requests, sizeof(combined_requests));
    TEST_ASSERT_EQUAL_UINT32(2U, captured_count);
    assert_frame(0U, clock_con, sizeof(clock_con));
    assert_frame(1U, combined_reset_negative,
                  sizeof(combined_reset_negative));
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
}

void test_golden_clock_response_at_every_tcp_split_boundary(void)
{
    for (size_t split = 1U; split < sizeof(clock_request); split++)
    {
        initialize_stack();
        feed_frame(start_act, sizeof(start_act));
        feed_frame(ack_init, sizeof(ack_init));
        captured_count = 0U;
        feed_frame(clock_request, split);
        TEST_ASSERT_EQUAL_UINT32(0U, captured_count);
        feed_frame(&clock_request[split], sizeof(clock_request) - split);
        TEST_ASSERT_EQUAL_UINT32(1U, captured_count);
        assert_frame(0U, clock_con, sizeof(clock_con));
        TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    }
}

/*** end of file ***/
