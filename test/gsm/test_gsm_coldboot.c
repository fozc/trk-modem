/*
 * test_gsm_coldboot.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real init decisions, recovery budget and supervisor restart requests.
 */
#include "unity.h"
#include "../support/gsm_coldboot_fixture.inc"
#define TEST_CHECK(condition, message) TEST_ASSERT_TRUE_MESSAGE((condition), (message))

static void ladder_lower_steps(void)
{
    /* Ilk iki cagri: ENHRST (GSM_REBOOT) yolu */
    gsm_init_step_reset_module();
    TEST_CHECK(gsm.init_state == (uint8_t)GSM_REBOOT, "lower ladder 1st call -> GSM_REBOOT");
    TEST_CHECK(gsm.reboot_counter == 1U, "first ENHRST uses one attempt");

    gsm_init_step_reset_module();
    TEST_CHECK(gsm.init_state == (uint8_t)GSM_REBOOT, "lower ladder 2nd call -> GSM_REBOOT");

    /* Third failure: board has no separate reset pin */
    gsm_init_step_reset_module();
    TEST_CHECK(gsm_module_restart_requested(), "third failure requests power cycle after two ENHRST attempts");
    TEST_CHECK(gsm.reboot_counter == 2U, "cold boot preserves failed AT budget");
}

static void cold_boot_request_tests(void)
{
    /* After two unsuccessful AT resets: 1. soguk baslatma istegi */
    gsm_init_step_reset_module();

    TEST_CHECK(cold_boot_count == 1U, "exhaustion: attempt counter -> 1");
    TEST_CHECK(restart_flag_sets == 1U, "exhaustion: restart flag set once");
    TEST_CHECK(gsm_module_restart_requested(), "flag visible to supervisor");
    TEST_CHECK(elog_cold_boot_calls == 1U, "exhaustion: ELOG_GSM_COLD_BOOT recorded");
    TEST_CHECK(elog_last_arg == 1U, "elog arg carries attempt number 1");

    /* Ikinci istek */
    gsm_clear_module_restart();
    gsm_init_step_reset_module();

    TEST_CHECK(cold_boot_count == 2U, "exhaustion: attempt counter -> 2");
    TEST_CHECK(elog_cold_boot_calls == 2U, "second cold boot logged");
    TEST_CHECK(elog_last_arg == 2U, "elog arg carries attempt number 2");

    /* Hak bitti: bu cagridan sonra dongu (while(1)) beklenir - normal
     * test modunda cagirmayiz; hang modu ayri dogrulanir. */
    TEST_CHECK(cold_boot_count >= GSM_COLD_BOOT_MAX_ATTEMPTS, "attempts exhausted after 2");
}

static void init_success_resets_tests(void)
{
    /* Tukeme sonrasi init basarilirsa merdiven tazelenmelidir */
    gsm_set_main_state((uint8_t)GSM_MODULE_INIT_MODE);
    gsm_init_step_done();

    TEST_CHECK(cold_boot_count == 0U, "init success clears cold boot count");
    TEST_CHECK(gsm.reboot_counter == 0U, "init success clears reboot counter");

    /* Tazelendikten sonra tukeme yeniden 2 hak ister */
    gsm.reboot_counter = 2U;
    gsm_init_step_reset_module();
    TEST_CHECK(cold_boot_count == 1U, "after reset, exhaustion grants fresh attempt");
    TEST_CHECK(elog_last_arg == 1U, "fresh attempt numbered from 1");
}

static void partial_init_keeps_recovery_budget(void)
{
    reset_ladder_state();
    cold_boot_count = 1U;
    gsm.reboot_counter = 2U;
    gsm_load_common_init_vector();
    gsm_set_main_state(GSM_COMMON_INIT_MODE);
    gsm_set_init_state(GSM_SOFT_INIT);
    gsm_init_old();
    TEST_CHECK(gsm.reboot_counter == 2U, "soft init preserves unsuccessful reboot count");
    gsm_set_init_state(GSM_INIT_DONE);
    gsm_init_old();
    TEST_CHECK(cold_boot_count == 1U, "common init does not refill cold boot budget");
    TEST_CHECK(gsm.reboot_counter == 2U, "common init preserves unsuccessful reboot count");
    gsm_set_init_state(GSM_RESET_MODULE);
    gsm_init_old();
    TEST_CHECK(cold_boot_count == 2U, "failure after common init advances to second cold boot");
    TEST_CHECK(restart_flag_sets == 1U, "failure after partial init requests supervisor recovery");
}

static void dispatch_integration_test(void)
{
    /* gsm_init_old() dispatcher uzerinden erisim: tukeme durumunda
     * FSM donmayan bir sekilde bayrak birakip cikmali. */
    reset_ladder_state();
    gsm.reboot_counter = 2U;
    gsm.init_state = (uint8_t)GSM_RESET_MODULE;

    gsm_init_old();

    TEST_CHECK(gsm_module_restart_requested(), "dispatcher path sets restart flag");
    TEST_CHECK(cold_boot_count == 1U, "dispatcher path counts attempt");
}

void setUp(void)
{
    reset_ladder_state();
}

void tearDown(void)
{
}

void test_cold_boot_lower_recovery_steps(void)
{
    ladder_lower_steps();
}

void test_exhausted_at_attempts_request_cold_boot(void)
{
    gsm.reboot_counter = 2U;
    cold_boot_request_tests();
}

void test_success_restores_cold_boot_budget(void)
{
    gsm.reboot_counter = 2U;
    cold_boot_count = 1U;
    init_success_resets_tests();
}

void test_dispatcher_requests_supervisor_recovery(void)
{
    dispatch_integration_test();
}

void test_partial_init_cannot_refill_recovery_budget(void)
{
    partial_init_keeps_recovery_budget();
}

/*** end of file ***/
