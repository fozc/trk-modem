/*
 * test_coldboot_ladder.c
 *
 *  Created on: Sep 6, 2026
 *      Author: fatih
 *
 * GSM kurtarma merdiveninin karar mantigi (gsm_init.c) host testleri.
 * Uretim dosyasi #include edilir; statik durumlara (s_pin_reset_state,
 * cold_boot_count) dogrudan erisilir, tum dis bagimliliklar stub'lanir.
 *
 * Kapsam:
 *   - ENHRST/pin-reset merdiveni: reboot_counter akisi (REBOOT x2 -> PIN_RESET)
 *   - Tukeme: soguk baslatma istegi (bayrak + sayaç + elog arg'lari),
 *     maksimum deneme siniri ve init-basarisi sifirlamasi
 *   - "hang" modu: haklar bitince adim while(1)'e dusmeli (disaridan
 *     timeout ile oldurulerek dogrulanir - Makefile hedefi)
 *
 * Kullanim: make run_coldboot          (normal testler)
 *           make run_coldboot_hang     (while(1) dogrulamasi, timeout)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ================================================================== */
/* Stub'lar: gsm_init.c'nin dis bagimliliklari                        */
/* ================================================================== */

/* ---- Uretim dosyasi (statiklere erisim icin include) -------------- */
#include "../gsm_init.c"

/* ---- Test gozlem kuyrugu ------------------------------------------ */

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                        \
    do                                                                \
    {                                                                 \
        if ((cond) != 0)                                              \
        {                                                             \
            test_pass++;                                              \
            printf("PASS: %s\r\n", (name));                           \
        }                                                             \
        else                                                          \
        {                                                             \
            test_fail++;                                              \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__); \
        }                                                             \
    } while (0)

/* ---- Stub durumlari ----------------------------------------------- */

gsm_t gsm;

static uint32_t tick_now          = 0U;

static uint8_t  restart_flag      = 0U;
static unsigned restart_flag_sets = 0U;

static elog_code_t elog_last_code = ELOG_CODE_OK;
static uint8_t     elog_last_arg  = 0U;
static unsigned    elog_cold_boot_calls = 0U;
static unsigned    elog_exhausted_calls = 0U;

/* Not: gsm_set/get_init_state gsm_init.c icinde tanimli geliyor ve
 * gsm.init_state uzerinde calisiyor - stub'lanmaz. Ana durum ve
 * gecikme/bayraklar burada stub'lanir. */

/* ---- gsm_init.c'nin cagirdigi dis semboller ----------------------- */

void gsm_set_main_state(uint8_t state)      { gsm.main_state = state; }
uint8_t gsm_get_main_state(void)            { return gsm.main_state; }

uint32_t gsm_get_tick(void)            { return tick_now; }
void gsm_set_delay(uint32_t d_time)    { (void)d_time; }
void gsm_set_busy(void)                { }
void gsm_set_free(void)                { }

/* Bayrak API'si: gsm_process.c'deki gercek davranisin birebir kopyasi */
void gsm_request_module_restart(void)  { restart_flag = 1U; restart_flag_sets++; }
bool gsm_module_restart_requested(void){ return restart_flag != 0U; }
void gsm_clear_module_restart(void)    { restart_flag = 0U; }

void led_driver_set_modem_mode(led_modem_mode_t mode) { (void)mode; }
void led_driver_set_gsm_mode(led_gsm_mode_t mode)     { (void)mode; }

void gsm_elog_modem_event(elog_code_t code)
{
    elog_last_code = code;
    if (code == ELOG_GSM_INIT_EXHAUSTED) { elog_exhausted_calls++; }
}

void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg,
                                   uint8_t arg_len)
{
    (void)arg_len;
    elog_last_code = code;
    if (code == ELOG_GSM_COLD_BOOT)
    {
        elog_cold_boot_calls++;
        elog_last_arg = *(const uint8_t *)arg;
    }
}

uint32_t gsm_engine_send_query(uint8_t query) { (void)query; return 0U; }
uint32_t gsm_engine_get_query_res(void)       { return 0U; }
uint32_t gsm_pin_reset_module(void)           { return 0U; }
void gsm_info_init(void)                      { }
void at_engine_clear_buff(void)               { }
void at_engine_reset(void)                    { }
void gsm_listener_set_no_carrier(gsm_listener_id_t id, uint8_t v) { (void)id; (void)v; }

/* modem_config.h kullanimlari (SET_APN vb. adimlar linklenmesin diye) */
const char *modem_config_get_apn(void)        { return "test"; }
const char *modem_config_get_apn_user(void)   { return "u"; }
const char *modem_config_get_apn_pass(void)   { return "p"; }
bool modem_config_is_ntp_active(void)         { return false; }

/* ================================================================== */
/* Testler                                                            */
/* ================================================================== */

static void reset_ladder_state(void)
{
    memset(&gsm, 0, sizeof(gsm));
    tick_now           = 0U;
    restart_flag       = 0U;
    restart_flag_sets  = 0U;
    elog_last_code     = ELOG_CODE_OK;
    elog_last_arg      = 0U;
    elog_cold_boot_calls = 0U;
    elog_exhausted_calls = 0U;
    s_pin_reset_state  = 0U;
    cold_boot_count    = 0U;
}

static void ladder_lower_steps(void)
{
    /* Ilk iki cagri: ENHRST (GSM_REBOOT) yolu */
    gsm_init_step_reset_module();
    TEST_CHECK(gsm.init_state == (uint8_t)GSM_REBOOT,
               "lower ladder 1st call -> GSM_REBOOT");
    TEST_CHECK(s_pin_reset_state == 0U, "1st call leaves pin-reset untried");

    gsm_init_step_reset_module();
    TEST_CHECK(gsm.init_state == (uint8_t)GSM_REBOOT,
               "lower ladder 2nd call -> GSM_REBOOT");

    /* Ucuncu cagri: pin reset kademnesi isaretlenir */
    gsm_init_step_reset_module();
    TEST_CHECK(gsm.init_state == (uint8_t)GSM_PIN_RESET,
               "lower ladder 3rd call -> GSM_PIN_RESET");
    TEST_CHECK(s_pin_reset_state == 1U, "3rd call marks pin-reset tried");
}

static void cold_boot_request_tests(void)
{
    /* Pin reset sonrasi modem olmezse tukeme: 1. soguk baslatma istegi */
    gsm_init_step_reset_module();

    TEST_CHECK(cold_boot_count == 1U, "exhaustion: attempt counter -> 1");
    TEST_CHECK(restart_flag_sets == 1U, "exhaustion: restart flag set once");
    TEST_CHECK(gsm_module_restart_requested(), "flag visible to supervisor");
    TEST_CHECK(elog_cold_boot_calls == 1U,
               "exhaustion: ELOG_GSM_COLD_BOOT recorded");
    TEST_CHECK(elog_last_arg == 1U, "elog arg carries attempt number 1");

    /* Ikinci istek */
    gsm_clear_module_restart();
    gsm_init_step_reset_module();

    TEST_CHECK(cold_boot_count == 2U, "exhaustion: attempt counter -> 2");
    TEST_CHECK(elog_cold_boot_calls == 2U, "second cold boot logged");
    TEST_CHECK(elog_last_arg == 2U, "elog arg carries attempt number 2");

    /* Hak bitti: bu cagridan sonra dongu (while(1)) beklenir - normal
     * test modunda cagirmayiz; hang modu ayri dogrulanir. */
    TEST_CHECK(cold_boot_count >= GSM_COLD_BOOT_MAX_ATTEMPTS,
               "attempts exhausted after 2");
}

static void init_success_resets_tests(void)
{
    /* Tukeme sonrasi init basarilirsa merdiven tazelenmelidir */
    gsm_set_main_state((uint8_t)GSM_MODULE_INIT_MODE);
    gsm_init_step_done();

    TEST_CHECK(cold_boot_count == 0U, "init success clears cold boot count");
    TEST_CHECK(s_pin_reset_state == 0U, "init success clears pin-reset mark");
    TEST_CHECK(gsm.reboot_counter == 0U, "init success clears reboot counter");

    /* Tazelendikten sonra tukeme yeniden 2 hak ister */
    s_pin_reset_state = 1U;
    gsm_init_step_reset_module();
    TEST_CHECK(cold_boot_count == 1U,
               "after reset, exhaustion grants fresh attempt");
    TEST_CHECK(elog_last_arg == 1U, "fresh attempt numbered from 1");
}

static void dispatch_integration_test(void)
{
    /* gsm_init_old() dispatcher uzerinden erisim: tukeme durumunda
     * FSM donmayan bir sekilde bayrak birakip cikmali. */
    reset_ladder_state();
    s_pin_reset_state = 1U;
    gsm.init_state = (uint8_t)GSM_RESET_MODULE;

    gsm_init_old();

    TEST_CHECK(gsm_module_restart_requested(),
               "dispatcher path sets restart flag");
    TEST_CHECK(cold_boot_count == 1U, "dispatcher path counts attempt");
}

int main(int argc, char *argv[])
{
    /* Hang modu: haklar bitirilip adim cagrilir; while(1)'e dusmeli.
     * Disaridan timeout ile oldurulur (Makefile: run_coldboot_hang). */
    if ((argc > 1) && (strcmp(argv[1], "hang") == 0))
    {
        reset_ladder_state();
        s_pin_reset_state = 1U;
        cold_boot_count   = GSM_COLD_BOOT_MAX_ATTEMPTS;
        gsm_init_step_reset_module();  /* beklenen: donmadan cikmaz */
        printf("ERROR: exhaustion returned - while(1) not reached\r\n");
        return 1;
    }

    reset_ladder_state();
    ladder_lower_steps();

    reset_ladder_state();
    s_pin_reset_state = 1U;  /* pin reset zaten denendigi durumu */
    cold_boot_request_tests();

    reset_ladder_state();
    s_pin_reset_state = 1U;
    cold_boot_count   = 1U;  /* bir hak kullanilmis baslangic */
    init_success_resets_tests();

    dispatch_integration_test();

    printf("\r\n=== cold boot ladder tests: %u passed, %u failed ===\r\n",
           test_pass, test_fail);

    return (test_fail == 0U) ? 0 : 1;
}
