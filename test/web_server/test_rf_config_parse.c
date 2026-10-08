/*
 * test_rf_config_parse.c
 *
 *  Created on: Oct 8, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise the real web parser, RF staging and R1 codec boundaries.
 */
#include "unity.h"
#include "json_config.h"
#include "rf_config.h"
#include "rf_scp_codec.h"
#include "rf_nvram_fake.h"
#include "mock_iec104_config.h"
#include "mock_modbus_config.h"
#include "mock_modbus_process.h"
#include "mock_modem_config.h"
#include <string.h>

TEST_SOURCE_FILE("json_config.c")
TEST_SOURCE_FILE("rf_config.c")
TEST_SOURCE_FILE("rf_scp_codec.c")
TEST_SOURCE_FILE("rf_scp.c")
TEST_SOURCE_FILE("rf_nvram_fake.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("xprintf.c")

void setUp(void)
{
    rf_nvram_fake_reset();
    rf_store_init();
    rf_feeder_t feeder;
    rf_config_defaults(&feeder);
    feeder.in_use = true;
    feeder.config.fider_id = 1U;
    feeder.config.nominal_current = 10.0F;
    feeder.config.ia_threshold = 12.0F;
    TEST_ASSERT_TRUE(rf_store_set(0U, &feeder));
}

void tearDown(void)
{
    rf_store_stage_abort();
}

static bool parse_staged(const char *json)
{
    jayirici_rf_config_t config = {0};
    TEST_ASSERT_TRUE(rf_store_stage_begin());
    if (parse_rf_config(json, &config))
    {
        rf_store_stage_commit();
        return true;
    }
    rf_store_stage_abort();
    return false;
}

void test_web_accepts_r1_didt_boundaries(void)
{
    TEST_ASSERT_TRUE(parse_staged("{\"ArtimliAkimEsigi\":[1]}"));
    TEST_ASSERT_EQUAL_FLOAT(1.0F, rf_store_get(0U)->config.di_dt_threshold);
    TEST_ASSERT_TRUE(parse_staged("{\"ArtimliAkimEsigi\":[2200]}"));
    TEST_ASSERT_EQUAL_FLOAT(2200.0F,
                           rf_store_get(0U)->config.di_dt_threshold);
}

void test_web_rejects_didt_outside_r1_and_preserves_store(void)
{
    const rf_feeder_t before = *rf_store_get(0U);
    TEST_ASSERT_FALSE(parse_staged("{\"ArtimliAkimEsigi\":[0.9]}"));
    TEST_ASSERT_FALSE(parse_staged("{\"ArtimliAkimEsigi\":[2200.1]}"));
    TEST_ASSERT_EQUAL_MEMORY(&before, rf_store_get(0U), sizeof(before));
}

void test_web_accepts_minimum_ia_and_maximum_nominal_boundary(void)
{
    TEST_ASSERT_TRUE(parse_staged("{\"SistemNominalAkimi\":[2],"
        "\"SetEdilebilirActirmaEsikAkimi\":[5]}"));
    TEST_ASSERT_TRUE(parse_staged("{\"SistemNominalAkimi\":[200],"
        "\"SetEdilebilirActirmaEsikAkimi\":[240]}"));
    const rf_feeder_t *feeder = rf_store_get(0U);
    TEST_ASSERT_EQUAL_INT(RF_CMD_OK, rf_scp_validate_config(
        (const uint8_t *)&feeder->config, sizeof(feeder->config)));
}

void test_web_rejects_legacy_minimum_ia(void)
{
    const rf_feeder_t before = *rf_store_get(0U);
    TEST_ASSERT_FALSE(parse_staged("{\"SistemNominalAkimi\":[2],"
        "\"SetEdilebilirActirmaEsikAkimi\":[2.4]}"));
    TEST_ASSERT_EQUAL_MEMORY(&before, rf_store_get(0U), sizeof(before));
}

void test_web_and_codec_use_the_same_nominal_dependency(void)
{
    TEST_ASSERT_FALSE(parse_staged(
        "{\"SetEdilebilirActirmaEsikAkimi\":[11.9999]}"));
    TEST_ASSERT_TRUE(parse_staged("{\"SetEdilebilirActirmaEsikAkimi\":[12],"
        "\"SistemNominalAkimi\":[10]}"));
}

void test_web_rejects_invalid_retained_config_without_changing_store(void)
{
    rf_feeder_t feeder = *rf_store_get(0U);
    feeder.config.trip_mode = 2U;
    TEST_ASSERT_TRUE(rf_store_set(0U, &feeder));
    TEST_ASSERT_FALSE(parse_staged("{}"));
    TEST_ASSERT_EQUAL_MEMORY(&feeder, rf_store_get(0U), sizeof(feeder));
}

/*** end of file ***/
