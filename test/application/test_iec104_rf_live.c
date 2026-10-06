/*
 * test_iec104_rf_live.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify real RF cache and registered IEC104 current/link readers.
 */
#include "unity.h"
#include "rf.h"
#include "mock_rf_inventory.h"
#include "mock_bsp.h"
#include "mock_nvram.h"
#include "mock_breaker.h"
#include "mock_gsm_engine.h"
#include "mock_gsm_socket.h"
#include "mock_iec104_application.h"
#include "mock_iec104_elog.h"
#include <string.h>

TEST_SOURCE_FILE("rf.c")

static uint32_t tick;
static rf_feeder_t configured_feeder;
uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return tick;
}

#include "../../Application/iec104_process.c"

const rf_feeder_t *rf_store_get(feeder_id_t line)
{
    return (0U == line) ? &configured_feeder : NULL;
}

bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN])
{
    uint8_t combined = 0U;

    for (size_t index = 0U; index < RF_EUI64_LEN; index++)
    {
        combined |= eui[index];
    }
    return 0U == combined;
}

static bool binding(uint8_t source, rf_inventory_entry_t *out, int count)
{
    (void)count;
    if (5U != source)
    {
        return false;
    }
    *out = (rf_inventory_entry_t){.feeder = 1U, .phase = 1U, .zone = 1U};
    (void)memcpy(out->eui64, configured_feeder.r_eui64, 8U);
    return true;
}

static cp56time2a_t receipt(void)
{
    return (cp56time2a_t){.milliseconds = 59123U, .minute = 59U,
        .hour = 23U, .day = 5U, .month = 10U, .year = 26U};
}

static void live(float amps, const cp56time2a_t *time)
{
    const rf_scp_live_t sample = {.source = 5U, .seq = 1U,
        .uptime_sec = 10U, .current_amps = amps};

    TEST_ASSERT_TRUE(rf_handle_live(&sample, tick, time));
}

void setUp(void)
{
    tick = 0U;
    (void)memset(&configured_feeder, 0, sizeof(configured_feeder));
    configured_feeder.in_use = true;
    configured_feeder.config.fider_id = 1U;
    configured_feeder.r_eui64[0] = 0x11U;
    rf_init();
    rf_inventory_get_binding_StubWithCallback(binding);
}

void tearDown(void)
{
}

void test_current_uses_saved_receive_time_even_when_read_much_later(void)
{
    const cp56time2a_t received = receipt();
    cp56time2a_t time;
    float value;
    qds_t quality;

    live(1.25F, &received);
    tick = 5000U;
    TEST_ASSERT_EQUAL_INT(0, read_anlik_akim(0U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_FLOAT(1.25F, value);
    TEST_ASSERT_EQUAL_UINT8(0U, quality.invalid);
    TEST_ASSERT_EQUAL_UINT8(0U, quality.not_topical);
    TEST_ASSERT_EQUAL_MEMORY(&received, &time, sizeof(time));
    TEST_ASSERT_EQUAL_INT(0, read_rf_haberlesme_varyok(0U, 0U,
        &(siq_t){0}, &time));
    TEST_ASSERT_EQUAL_MEMORY(&received, &time, sizeof(time));
}

void test_stale_current_and_rf_link_keep_receive_time_and_quality(void)
{
    const cp56time2a_t received = receipt();
    cp56time2a_t time;
    float value;
    qds_t quality;
    siq_t link;

    live(1.25F, &received);
    tick = 30000U;
    TEST_ASSERT_EQUAL_INT(0, read_anlik_akim(0U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_FLOAT(1.25F, value);
    TEST_ASSERT_EQUAL_UINT8(1U, quality.invalid);
    TEST_ASSERT_EQUAL_UINT8(1U, quality.not_topical);
    TEST_ASSERT_EQUAL_MEMORY(&received, &time, sizeof(time));
    TEST_ASSERT_EQUAL_INT(0, read_rf_haberlesme_varyok(0U, 0U, &link, &time));
    TEST_ASSERT_EQUAL_UINT8(0U, link.spi);
    TEST_ASSERT_EQUAL_UINT8(0U, link.invalid);
    TEST_ASSERT_EQUAL_UINT8(1U, link.not_topical);
}

void test_unknown_receive_clock_does_not_invalidate_a_good_measurement(void)
{
    cp56time2a_t time;
    float value;
    qds_t quality;

    live(1.25F, NULL);
    TEST_ASSERT_EQUAL_INT(0, read_anlik_akim(0U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_UINT8(1U, time.iv_bit);
    TEST_ASSERT_EQUAL_UINT8(0U, quality.invalid);
    TEST_ASSERT_EQUAL_FLOAT(1.25F, value);
}

void test_invalid_measurement_keeps_fresh_rf_link_valid(void)
{
    const cp56time2a_t received = receipt();
    cp56time2a_t time;
    float value;
    qds_t quality;
    siq_t link;

    live(-1.0F, &received);
    TEST_ASSERT_EQUAL_INT(0, read_anlik_akim(0U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_UINT8(1U, quality.invalid);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, value);
    TEST_ASSERT_EQUAL_INT(0, read_rf_haberlesme_varyok(0U, 0U, &link, &time));
    TEST_ASSERT_EQUAL_UINT8(1U, link.spi);
    TEST_ASSERT_EQUAL_UINT8(0U, link.invalid);
}

void test_missing_source_and_invalid_arguments_never_report_dummy_data(void)
{
    cp56time2a_t time;
    float value;
    qds_t quality;
    siq_t link;

    TEST_ASSERT_EQUAL_INT(0, read_anlik_akim(0U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_UINT8(1U, quality.invalid);
    TEST_ASSERT_EQUAL_UINT8(1U, time.iv_bit);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, value);
    TEST_ASSERT_EQUAL_INT(0, read_rf_haberlesme_varyok(0U, 0U, &link, &time));
    TEST_ASSERT_EQUAL_UINT8(1U, link.invalid);
    TEST_ASSERT_EQUAL_INT(-1, read_anlik_akim(7U, 0U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_INT(-1, read_anlik_akim(0U, 3U, &value, &quality, &time));
    TEST_ASSERT_EQUAL_INT(-1, read_anlik_akim(0U, 0U, NULL, &quality, &time));
    TEST_ASSERT_EQUAL_INT(-1, read_rf_haberlesme_varyok(0U, 0U, NULL, &time));
}

/*** end of file ***/
