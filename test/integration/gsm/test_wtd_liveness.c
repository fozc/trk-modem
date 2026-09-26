/*
 * test_wtd_liveness.c
 *
 *  Created on: Sep 7, 2026
 *      Author: fatih
 *
 * GSM yazilim watchdog'unun stuck-FREE (liveness) katmaninin host
 * testleri. Uretim dosyasi #include edilir; damga ve kurtarma
 * sayacina statik erisimle senaryolar kurulur.
 *
 * Kapsam:
 *   - Taze canlilik damgasi: tetikleme yok
 *   - Sessizlik esigi: surec restart istegi + elog kaydi (1/2, 2/2)
 *   - Hak bitince: bilincl hard reset (bsp_system_reset)
 *   - Ping geri gelince kurtarma sayaci sifirlanir
 *   - Motor BUSY iken liveness katmani tetiklenmez (busy yolu ayri)
 *
 * Kullanim: make run_wtd_liveness
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ================================================================== */
/* Stub'lar                                                           */
/* ================================================================== */

#include "gsm_wtd.c"

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

gsm_t gsm;

static uint32_t tick_now        = 1000U;  /* sifir degil: acilis penceresi */
static uint8_t  busy_state      = 0U;
static uint8_t  restart_flag    = 0U;
static unsigned restart_sets    = 0U;
static unsigned bsp_reset_calls = 0U;
static unsigned elog_liveness_calls = 0U;
static uint8_t  elog_last_recovery_count = 0U;

uint32_t gsm_get_tick(void)              { return tick_now; }
bool     gsm_is_busy(void)               { return busy_state != 0U; }
void     gsm_set_busy(void)              { busy_state = 1U; }
void     gsm_set_free(void)              { busy_state = 0U; }

void gsm_request_module_restart(void)    { restart_flag = 1U; restart_sets++; }

void bsp_system_reset(void)              { bsp_reset_calls++; }

void gsm_elog_modem_event(elog_code_t code) { (void)code; }

void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg,
                                   uint8_t arg_len)
{
    (void)arg_len;
    if (code == ELOG_GSM_WTD_LIVENESS)
    {
        elog_liveness_calls++;
        elog_last_recovery_count = ((const uint8_t *)arg)[2];
    }
}

void at_engine_clear_buff(void)          { }
void at_engine_reset(void)               { }
const char *at_engine_get_state_str(void){ return "IDLE"; }

/* gsm_log.h makrolarinin runtime cagrilari (CSLOG kapali olsa da
 * seviye fonksiyonlari link edilir) */
void gsm_log_set_level(gsm_log_level_t level) { (void)level; }
gsm_log_level_t gsm_log_get_level(void)  { return 0; }

/* ================================================================== */
/* Testler                                                            */
/* ================================================================== */

static void reset_state(void)
{
    memset(&gsm, 0, sizeof(gsm));
    tick_now        = 1000U;
    busy_state      = 0U;
    restart_flag    = 0U;
    restart_sets    = 0U;
    bsp_reset_calls = 0U;
    elog_liveness_calls = 0U;
    elog_last_recovery_count = 0U;
    s_liveness_last_activity  = 0U;
    s_liveness_recovery_count = 0U;
}

int main(void)
{
    /* T1: taze damga - tetikleme yok */
    reset_state();
    gsm_wtd_liveness_ping();
    tick_now += 9U * 60U * 1000U;  /* 9 dk: esik alti */
    gsm_wtd_check();
    TEST_CHECK(restart_sets == 0U, "fresh ping below threshold: no trigger");

    /* T2: esik asimi - 1. kurtarma: surec restart + elog */
    tick_now += 2U * 60U * 1000U;  /* toplam 11 dk sessiz */
    gsm_wtd_check();
    TEST_CHECK(restart_sets == 1U, "silence: process restart requested");
    TEST_CHECK(elog_liveness_calls == 1U, "silence: elog LIVENESS recorded");
    TEST_CHECK(elog_last_recovery_count == 1U, "elog carries recovery count 1");
    TEST_CHECK(bsp_reset_calls == 0U, "first recovery: no hard reset");

    /* T3: ikinci pencere - 2. restart */
    tick_now += 10U * 60U * 1000U + 100U;
    gsm_wtd_check();
    TEST_CHECK(restart_sets == 2U, "second window: second restart");
    TEST_CHECK(elog_last_recovery_count == 2U, "recovery count 2");
    TEST_CHECK(bsp_reset_calls == 0U, "second recovery: still no hard reset");

    /* T4: ucuncu pencere - haklar bitti: bilincl hard reset */
    tick_now += 10U * 60U * 1000U + 100U;
    gsm_wtd_check();
    TEST_CHECK(bsp_reset_calls == 1U, "exhausted: deliberate hard reset");
    TEST_CHECK(restart_sets == 2U, "exhausted: no further restart request");
    TEST_CHECK(s_liveness_recovery_count == 0U,
               "hard reset path clears recovery counter");

    /* T5: ping canliligi geri getirir - sayac sifirlanir */
    reset_state();
    tick_now += 11U * 60U * 1000U;
    gsm_wtd_check();
    TEST_CHECK(restart_sets == 1U, "pre-ping window triggers once");
    gsm_wtd_liveness_ping();                     /* canlilik geri geldi */
    tick_now += 11U * 60U * 1000U;               /* yeniden sessizlik */
    gsm_wtd_check();
    TEST_CHECK(elog_last_recovery_count == 1U,
               "ping resets recovery counter (count starts from 1)");

    /* T6: motor BUSY iken liveness katmani devre disi */
    reset_state();
    tick_now += 20U * 60U * 1000U;  /* cok eski damga */
    busy_state = 1U;
    gsm_wtd_check();
    TEST_CHECK(restart_sets == 0U,
               "busy engine: liveness does not trigger (busy path owns it)");

    printf("\r\n=== wtd liveness tests: %u passed, %u failed ===\r\n",
           test_pass, test_fail);

    return (test_fail == 0U) ? 0 : 1;
}
