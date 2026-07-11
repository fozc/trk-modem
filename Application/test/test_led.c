/*
 * test_led.c
 *
 *  Created on: 11 Tem 2026
 *      Author: fatih
 */
#include "test_led.h"
#include "led_driver.h"

/* -----------------------------------------------------------------------
 *  LED hardware test — compile with -DLED_TEST to enable.
 *
 *  Blocking test that runs before Contiki starts.
 *  Cycles through every LED pin and every driver mode so each
 *  state can be visually verified on the board.
 * --------------------------------------------------------------------- */

#ifdef LED_TEST

#define LED_TEST_STEP_DELAY_MS   3000U
#define LED_TEST_BLINK_DELAY_MS  5000U

/**
 * @brief Blocking tick helper — runs led_driver_tick() in a busy loop
 *        for the given duration so blink patterns are visible.
 */
static void led_test_run_ticks(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < duration_ms)
    {
        led_driver_tick();
        HAL_Delay(50);
    }
}

void test_led_all(void)
{
    CSLOG("\r\n========== LED TEST START ==========\r\n");

    /* ----------------------------------------------------------
     *  Phase 1: Raw GPIO — toggle each pin directly (bypass driver)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 1: Raw GPIO direct pin toggle ---\r\n");

    led_driver_init(); /* All OFF */

    CSLOG("[GPIO] LED1 (PE7, active-LOW) ON\r\n");
    gpio_set_pin(LED1_GPIO, LED1_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED1_GPIO, LED1_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED1 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] LED2 (PB2, active-LOW) ON\r\n");
    gpio_set_pin(LED2_GPIO, LED2_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED2_GPIO, LED2_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED2 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] LED3 (PB1, active-LOW) ON\r\n");
    gpio_set_pin(LED3_GPIO, LED3_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED3_GPIO, LED3_PIN, GPIO_HIGH);
    CSLOG("[GPIO] LED3 OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 RED (PE11) ON\r\n");
    gpio_set_pin(LED_RGB1_R_GPIO, LED_RGB1_R_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_R_GPIO, LED_RGB1_R_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 RED OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 GREEN (PE12) ON\r\n");
    gpio_set_pin(LED_RGB1_G_GPIO, LED_RGB1_G_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_G_GPIO, LED_RGB1_G_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 GREEN OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB1 BLUE (PE13) ON\r\n");
    gpio_set_pin(LED_RGB1_B_GPIO, LED_RGB1_B_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB1_B_GPIO, LED_RGB1_B_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB1 BLUE OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 RED (PE8) ON\r\n");
    gpio_set_pin(LED_RGB2_R_GPIO, LED_RGB2_R_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_R_GPIO, LED_RGB2_R_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 RED OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 GREEN (PE9) ON\r\n");
    gpio_set_pin(LED_RGB2_G_GPIO, LED_RGB2_G_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_G_GPIO, LED_RGB2_G_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 GREEN OFF\r\n");
    HAL_Delay(500);

    CSLOG("[GPIO] RGB2 BLUE (PE10) ON\r\n");
    gpio_set_pin(LED_RGB2_B_GPIO, LED_RGB2_B_PIN, GPIO_LOW);
    HAL_Delay(LED_TEST_STEP_DELAY_MS);
    gpio_set_pin(LED_RGB2_B_GPIO, LED_RGB2_B_PIN, GPIO_HIGH);
    CSLOG("[GPIO] RGB2 BLUE OFF\r\n");
    HAL_Delay(500);

    CSLOG("\r\n--- Phase 1 complete ---\r\n\r\n");

    /* ----------------------------------------------------------
     *  Phase 2: LED driver modes — modem status (RGB1)
     * ---------------------------------------------------------- */
    CSLOG("--- Phase 2: Modem Status LED modes (RGB1) ---\r\n");
    led_driver_init();

    CSLOG("[MODEM] LED_MODEM_POWER_ON — Yellow 1000/1000\r\n");
    led_driver_set_modem_mode(LED_MODEM_POWER_ON);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_INIT — Yellow 250/250\r\n");
    led_driver_set_modem_mode(LED_MODEM_INIT);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_SEARCHING — Blue 1000/1000\r\n");
    led_driver_set_modem_mode(LED_MODEM_SEARCHING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_READY — Green 250/2750\r\n");
    led_driver_set_modem_mode(LED_MODEM_READY);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_NO_SIM — Red double pulse\r\n");
    led_driver_set_modem_mode(LED_MODEM_NO_SIM);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_ERROR — Red triple pulse\r\n");
    led_driver_set_modem_mode(LED_MODEM_ERROR);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[MODEM] LED_MODEM_OFF\r\n");
    led_driver_set_modem_mode(LED_MODEM_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 3: LED driver modes — GSM network (RGB2)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 3: GSM Network LED modes (RGB2) ---\r\n");

    CSLOG("[GSM] LED_GSM_2G — Red 250/2750\r\n");
    led_driver_set_gsm_mode(LED_GSM_2G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_2G_WEAK — Red 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_2G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_3G — Blue 250/2750\r\n");
    led_driver_set_gsm_mode(LED_GSM_3G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_3G_WEAK — Blue 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_3G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_4G — Green solid\r\n");
    led_driver_set_gsm_mode(LED_GSM_4G);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_4G_WEAK — Green 500/500\r\n");
    led_driver_set_gsm_mode(LED_GSM_4G_WEAK);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[GSM] LED_GSM_OFF\r\n");
    led_driver_set_gsm_mode(LED_GSM_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 4: LED driver modes — IEC104 listener (LED2)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 4: IEC104 Listener LED (LED2) ---\r\n");

    CSLOG("[IEC104] LED_LISTENER_LISTENING — Solid ON\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_LISTENING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[IEC104] LED_LISTENER_CONNECTED — Blink 250/250\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_CONNECTED);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[IEC104] LED_LISTENER_OFF\r\n");
    led_driver_set_iec104_mode(LED_LISTENER_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Phase 5: LED driver modes — Web listener (LED1)
     * ---------------------------------------------------------- */
    CSLOG("\r\n--- Phase 5: Web Listener LED (LED1) ---\r\n");

    CSLOG("[WEB] LED_LISTENER_LISTENING — Solid ON\r\n");
    led_driver_set_web_mode(LED_LISTENER_LISTENING);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[WEB] LED_LISTENER_CONNECTED — Blink 250/250\r\n");
    led_driver_set_web_mode(LED_LISTENER_CONNECTED);
    led_test_run_ticks(LED_TEST_BLINK_DELAY_MS);

    CSLOG("[WEB] LED_LISTENER_OFF\r\n");
    led_driver_set_web_mode(LED_LISTENER_OFF);
    HAL_Delay(1000);

    /* ----------------------------------------------------------
     *  Done — reset all LEDs and continue boot
     * ---------------------------------------------------------- */
    led_driver_init();
    CSLOG("\r\n========== LED TEST COMPLETE ==========\r\n\r\n");
}

#endif /* LED_TEST */
