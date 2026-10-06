/*
 * test_gsm_signal_led_edges.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 * LED signal classification boundaries: reserved CESQ values must not
 * become a technology or a level; sentinels and valid fallbacks stay.
 */
#include "unity.h"
#include "gsm_signal_led.h"
#include "gsm_info.h"

void setUp(void)
{
    gsm_info_init();
    (void)gsm_signal_led_get_tech();  /* keep module linked */
}
void tearDown(void)
{
}

void test_reserved_values_do_not_become_tech_or_level(void)
{
    /* Ayrimis LTE gucu gecerli 3G olcumunun onune gecmez. */
    gsm_info_set_signal_quality_3G(40U);
    gsm_info_set_signal_quality_4G(98U);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_3G, gsm_signal_led_get_tech());
    TEST_ASSERT_NOT_EQUAL(GSM_SIGNAL_LEVEL_NONE, gsm_signal_led_get_level());

    /* Ayrimis 3G gucu gecerli 2G olcumunun onune gecmez. */
    gsm_info_set_signal_quality_4G(255U);
    gsm_info_set_signal_quality_3G(97U);
    gsm_info_set_signal_quality_2G(63U);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_2G, gsm_signal_led_get_tech());

    /* Butun guc alanlari ayrimis/sentinel ise sinyal yok sayilir. */
    gsm_info_set_signal_quality_2G(64U);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_NONE, gsm_signal_led_get_tech());
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_LEVEL_NONE, gsm_signal_led_get_level());
}

void test_sentinels_still_classify_as_no_signal(void)
{
    gsm_info_set_signal_quality_2G(99U);
    gsm_info_set_signal_quality_3G(255U);
    gsm_info_set_signal_quality_4G(255U);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_NONE, gsm_signal_led_get_tech());
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_LEVEL_NONE, gsm_signal_led_get_level());
}

void test_valid_endpoints_map_to_a_level(void)
{
    gsm_info_set_signal_quality_4G(0U);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_4G, gsm_signal_led_get_tech());
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_LEVEL_WEAK, gsm_signal_led_get_level());

    gsm_info_set_signal_quality_4G(GSM_CESQ_RSRP_MAX);
    gsm_signal_led_update();
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_TECH_4G, gsm_signal_led_get_tech());
    TEST_ASSERT_EQUAL_INT(GSM_SIGNAL_LEVEL_EXCELLENT,
                          gsm_signal_led_get_level());
}

/*** end of file ***/
