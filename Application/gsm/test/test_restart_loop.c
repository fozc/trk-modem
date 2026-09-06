/*
 * test_restart_loop.c
 *
 *  Created on: Sep 6, 2026
 *      Author: fatih
 *
 * GSM surec yeniden baslatma mekanizmasinin (gsm_process_contiki.c)
 * host testleri - GERCEK Contiki cekirdegi uzerinde, sanal saatle.
 *
 * Zaman: clock_tick() test tarafindan surulur; run_for(ms) her ms icin
 * bir tick atip olay kuyrugunu bosaltir. Gercek suret yok - 4.5 s'lik
 * ONOFF darbesi bile aninda biter.
 *
 * Kapsam:
 *   1) Acilis: guc dizisi -> SW_RDY -> init zinciri -> poll dongusu
 *   2) Kurtarma (SW_RDY basarili): bayrak -> RX kesmesi kapanir ->
 *      ikinci guc dizisi -> tam yeniden init -> poll devam
 *   3) Kurtarma (SW_RDY HIC gelmez): TEK atimlik guc dizisi (acilis
 *      modundaki sinirsiz retry YOK) -> init yine de yeniden kosar
 *   4) Acilis modunda sinirsiz retry korundugu (2. dizinin baslamasi)
 *
 * Kullanim: make run_restart_loop
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "gpio.h"
#include "uart.h"
#include "led_driver.h"

/* ================================================================== */
/* Stub'lar once (uretim dosyasi bunlara referans veriyor)            */
/* ================================================================== */

/* ---- GPIO mock: guc dizisi sayaclari + senaryolu SW_RDY ---------- */

static unsigned power_low_to_high = 0U;   /* GSM_POWER yukselen ken  */
static unsigned onoff_pulses      = 0U;   /* GSM_ONOFF yukselen ken  */
static bool     sw_rdy_scenario_assert = true;  /* SW_RDY assert edecek mi */
static bool     sw_rdy_scenario_used  = false; /* ilk poll'da tuketilir */

void gpio_set_pin(gpio_port_t port, gpio_pin_t pin, uint8_t val)
{
    static uint8_t power_last = 1U;
    static uint8_t onoff_last = 0U;

    if ((port == GSM_POWER_GPIO) && (pin == GSM_POWER_PIN))
    {
        if ((power_last == 0U) && (val != 0U))
        {
            power_low_to_high++;
        }
        power_last = val;
    }
    else if ((port == GSM_ONOFF_BSP_GPIO) && (pin == GSM_ONOFF_BSP_PIN))
    {
        if ((onoff_last == 0U) && (val != 0U))
        {
            onoff_pulses++;
        }
        onoff_last = val;
    }
    else
    {
        /* Diger pinler - ilgi disi */
    }
}

uint8_t gpio_read_pin(gpio_port_t port, gpio_pin_t pin)
{
    if ((port == GSM_SW_RDY_BSP_GPIO) && (pin == GSM_SW_RDY_BSP_PIN))
    {
        /* Senaryo tek kullanislik: ilk okumada karar verilir, sonraki
         * poll'lar ayni karari surdurur. */
        bool assert_now = sw_rdy_scenario_assert;
        sw_rdy_scenario_used = true;
        return (assert_now ? 0U : 1U);  /* 0 = assert (aktif low) */
    }
    return 1U;
}

/* ---- UART mock: RX kesmesi eslesme kaydi -------------------------- */

static unsigned rx_int_disables = 0U;
static unsigned rx_int_enables  = 0U;

void uart_set_rx_interrupt(uart_port_t port, uart_rx_interrupt_state_t state)
{
    (void)port;
    if (state == UART_RX_INT_DISABLE) { rx_int_disables++; }
    else                              { rx_int_enables++;  }
}

/* ---- Alt modul init/loop mock'lari: cagrı sayaclari --------------- */

static unsigned at_engine_init_calls    = 0U;
static unsigned at_engine_reset_calls   = 0U;
static unsigned init_old_calls          = 0U;  /* gsm_process_init_old */
static unsigned http_server_init_calls  = 0U;
static unsigned fw_update_init_calls    = 0U;
static unsigned rfwu_init_calls         = 0U;
static unsigned shell_init_calls        = 0U;
static unsigned gsm_process_old_calls   = 0U;

void at_engine_init(void)          { at_engine_init_calls++; }
void at_engine_reset(void)         { at_engine_reset_calls++; }
int  at_engine_process(void)       { return 0; }
void gsm_process_init_old(void)    { init_old_calls++; }
void gsm_http_server_init(void)    { http_server_init_calls++; }
void gsm_firmware_update_init(void){ fw_update_init_calls++; }
void gsm_firmware_update_rfwu_init(void) { rfwu_init_calls++; }
void gsm_shell_init(void)          { shell_init_calls++; }
void gsm_process_old(void)         { gsm_process_old_calls++; }
void gsm_http_server_process(void) { }

/* ---- LED mock ------------------------------------------------------ */

void led_driver_set_modem_mode(led_modem_mode_t mode) { (void)mode; }

/* ---- Bayrak API'si: gsm_process.c'deki gercek davranis ------------ */

static uint8_t restart_flag = 0U;

void gsm_request_module_restart(void)   { restart_flag = 1U; }
bool gsm_module_restart_requested(void) { return restart_flag != 0U; }
void gsm_clear_module_restart(void)     { restart_flag = 0U; }

/* ================================================================== */
/* Uretim dosyasi + gercek cekirdek                                    */
/* ================================================================== */

#include "../gsm_process_contiki.c"

PROCESS_NAME(gsm_process_contiki);

/* ---- Sanal zaman ---------------------------------------------------- */

static void run_for(uint32_t ms)
{
    for (uint32_t i = 0U; i < ms; i++)
    {
        clock_tick();
        etimer_request_poll();
        for (int guard = 0; guard < 100; guard++)
        {
            if (process_run() == 0)
            {
                break;
            }
        }
    }
}

/* ---- Test gozlem kuyrugu ------------------------------------------- */

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

static void arm_sw_rdy(bool assert_it)
{
    sw_rdy_scenario_assert = assert_it;
    sw_rdy_scenario_used  = false;
}

/* ================================================================== */
/* Senaryolar                                                          */
/* ================================================================== */

static void scenario_boot_happy_path(void)
{
    arm_sw_rdy(true);   /* modem ilk yoklamada hazir */

    gsm_process_contiki_init();
    run_for(8000U);     /* guc dizisi (5.2 s) + SW_RDY + init zinciri */

    TEST_CHECK(onoff_pulses == 1U, "boot: one ONOFF pulse");
    TEST_CHECK(sw_rdy_scenario_used, "boot: SW_RDY polled");
    TEST_CHECK(at_engine_init_calls == 1U, "boot: at_engine_init ran");
    TEST_CHECK(init_old_calls == 1U, "boot: full FSM init ran");
    TEST_CHECK(rx_int_enables == 1U, "boot: RX interrupt enabled");
    TEST_CHECK(rx_int_disables == 1U,
               "boot: RX re-disable at loop head (idempotent, harmless)");

    const unsigned before = gsm_process_old_calls;
    run_for(200U);      /* 4 x 50 ms poll */
    TEST_CHECK(gsm_process_old_calls > before,
               "boot: poll loop running");
    TEST_CHECK(gsm_process_old_calls - before >= 3U,
               "boot: ~50 ms poll cadence");
}

static void scenario_restart_with_sw_rdy(void)
{
    arm_sw_rdy(true);   /* kurtarmada modem geri gelecek */

    const unsigned inits_before  = init_old_calls;
    const unsigned pulses_before = onoff_pulses;

    const unsigned cycles_before = power_low_to_high;
    gsm_request_module_restart();  /* tukeme noktasinin yaptigi sey */
    run_for(8000U);                /* guc dizisi (5.2 s) + init */

    TEST_CHECK(onoff_pulses == pulses_before + 1U,
               "restart: second ONOFF pulse (cold boot ran)");
    TEST_CHECK(rx_int_disables == 2U,
               "restart: RX interrupt disabled before power sequence");
    TEST_CHECK(power_low_to_high == cycles_before + 1U,
               "restart: supply cycled (GSM_POWER low->high)");
    TEST_CHECK(init_old_calls == inits_before + 1U,
               "restart: full FSM re-init ran");
    TEST_CHECK(at_engine_reset_calls >= 1U,
               "restart: AT engine state reset (DMA abort path)");

    const unsigned before = gsm_process_old_calls;
    run_for(200U);
    TEST_CHECK(gsm_process_old_calls > before,
               "restart: poll loop resumed after recovery");
    TEST_CHECK(!gsm_module_restart_requested(),
               "restart: request flag cleared by loop");
}

static void scenario_restart_sw_rdy_never(void)
{
    arm_sw_rdy(false);  /* modemin donanimi olmus: SW_RDY hic gelmez */

    const unsigned inits_before  = init_old_calls;
    const unsigned pulses_before = onoff_pulses;

    gsm_request_module_restart();
    /* 5.2 s dizi + 15 s SW_RDY beklemesi + init: ~21 s sanal zaman.
     * TEK atimlik olmali - 2. diz baslamamali. */
    run_for(25000U);

    TEST_CHECK(onoff_pulses == pulses_before + 1U,
               "recovery-fail: exactly ONE power sequence (single-shot)");
    TEST_CHECK(init_old_calls == inits_before + 1U,
               "recovery-fail: init chain still re-ran");
    TEST_CHECK(rx_int_enables == 3U,
               "recovery-fail: RX re-enabled after init chain");
}

static void scenario_boot_retry_preserved(void)
{
    /* Acilis modunda (process_start data=NULL) SW_RDY gelmezse sinirsiz
     * retry korunmali. Parent surec devre disi birakilir; gsm_power_on
     * dogrudan acilis parametresiyle baslatilir. */
    process_exit(&gsm_process_contiki);
    process_exit(&gsm_power_on);

    onoff_pulses = 0U;
    arm_sw_rdy(false);

    process_start(&gsm_power_on, NULL);   /* data = NULL: acilis modu */
    run_for(40000U);  /* 1. dizi (~20.2 s) biter, 2. dizi baslar */

    TEST_CHECK(onoff_pulses >= 2U,
               "boot-mode: unbounded retry preserved (2nd sequence started)");
}

int main(void)
{
    clock_init();
    process_init();
    /* etimer sureci etimer_set icin gerekli */
    process_start(&etimer_process, NULL);

    scenario_boot_happy_path();
    scenario_restart_with_sw_rdy();
    scenario_restart_sw_rdy_never();
    scenario_boot_retry_preserved();

    printf("\r\n=== restart loop tests: %u passed, %u failed ===\r\n",
           test_pass, test_fail);

    return (test_fail == 0U) ? 0 : 1;
}
