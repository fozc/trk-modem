/**
 * @file  relay_pwm.c
 * @brief Low-level PWM abstraction for the two relay outputs - impl.
 */

#include "relay_pwm.h"
#include "main.h"

/* TIM3 handle is created by the CubeMX-generated code in main.c. */
extern TIM_HandleTypeDef htim3;

/* ======================================================================
 *  Konfigurasyon (Configuration)
 *
 *  PWM frekansi ve duty cozunurlugu, TIM3 giris saati ile asagidaki iki
 *  degere baglidir. Her ikisi de init aninda uygulanir; boylece CubeMX
 *  (.ioc) dosyasina dokunmadan bring-up sirasinda ayarlanabilir.
 *  relay_pwm_init() bu degerleri runtime'da yazarak MX_TIM3_Init()
 *  icindeki CubeMX degerlerini ezer.
 *
 *  Frekans formulu:
 *    freq_pwm = f_tim3 / ((RELAY_PWM_PRESCALER + 1) * (RELAY_PWM_PERIOD + 1))
 *
 *  Bu projede TIM3 APB1 timer saatinden beslenir:
 *    f_tim3 = APB1TimFreq = 96 MHz  (SYSCLK = HCLK = 96 MHz)
 *
 *  Secilen degerler ile:
 *    PSC = 0, ARR = 4799
 *    freq_pwm = 96 000 000 / ((0 + 1) * (4799 + 1))
 *             = 96 000 000 / 4800
 *             = 20 000 Hz = 20 kHz
 *
 *  Neden 20 kHz?
 *    - 20 kHz duyulabilir bandin (>20 kHz) hemen ustundedir; bobin
 *      otmesini/viziltiyi onler.
 *    - Cok yuksek frekans (orn. 96 kHz) gereksiz MOSFET anahtarlama
 *      kaybi, gate surme yuku ve EMI getirir.
 *
 *  Cozunurluk:
 *    - Timer adim sayisi = ARR + 1 = 4800 adim (~12-bit).
 *    - Ancak duty girisi per-mille (0..1000) oldugundan efektif duty
 *      cozunurlugu 1000 adim (~10-bit) ile sinirlidir. ARR >= 1000 olan
 *      her deger tam per-mille cozunurlugu saglar.
 *    - Duty -> compare donusumu relay_pwm_set_duty() icinde canli ARR'ye
 *      gore yapilir, bu yuzden ARR degisse bile duty dogru kalir.
 *
 *  Alternatif frekanslar (PSC = 0 icin):
 *    24 kHz  -> ARR = 3999   (96e6 / 4000)
 *    25 kHz  -> ARR = 3839   (96e6 / 3840)
 *
 * ====================================================================== */

/** Timer prescaler applied at init (PSC register value). */
#define RELAY_PWM_PRESCALER   ((uint32_t)0U)

/**
 * Timer auto-reload applied at init (ARR register value).
 * ARR = 4799 -> freq_pwm = 96 MHz / ((0+1) * (4799+1)) = 20 kHz.
 */
#define RELAY_PWM_PERIOD      ((uint32_t)4799U)

/* ======================================================================
 *  Channel map
 * ====================================================================== */

static const uint32_t s_tim_channel[RELAY_PWM_CH_COUNT] =
{
    [RELAY_PWM_CH_1] = TIM_CHANNEL_3,
    [RELAY_PWM_CH_2] = TIM_CHANNEL_4,
};

/* ======================================================================
 *  Public API
 * ====================================================================== */

void relay_pwm_init(void)
{
    /* Apply the configurable period/prescaler and refresh the shadow
     * registers before the outputs are enabled. */
    __HAL_TIM_SET_PRESCALER(&htim3, RELAY_PWM_PRESCALER);
    __HAL_TIM_SET_AUTORELOAD(&htim3, RELAY_PWM_PERIOD);

    for (uint8_t ch = 0U; ch < (uint8_t)RELAY_PWM_CH_COUNT; ch++)
    {
        __HAL_TIM_SET_COMPARE(&htim3, s_tim_channel[ch], 0U);
        (void)HAL_TIM_PWM_Start(&htim3, s_tim_channel[ch]);
    }
}

void relay_pwm_set_duty(relay_pwm_channel_t channel, uint16_t duty_permille)
{
    if ((uint8_t)channel >= (uint8_t)RELAY_PWM_CH_COUNT)
    {
        return;
    }

    uint16_t duty = duty_permille;
    if (duty > RELAY_PWM_DUTY_FULL)
    {
        duty = RELAY_PWM_DUTY_FULL;
    }

    /* Per-mille (binde, 0..1000) degeri canli ARR'ye gore compare
     * degerine cevrilir; boylece PWM periyodu (ARR) degisse bile duty
     * orani dogru kalir.
     *   period  = ARR + 1
     *   compare = (duty_permille * period) / 1000
     * Ornek: duty=500, ARR=4799 -> compare = 500*4800/1000 = 2400 (%50).
     * Ara carpim 64-bit'te yapilir; boylece (1000 * (ARR+1)) tasmasi
     * onlenir. */
    uint32_t period = (uint32_t)__HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
    uint32_t compare = (uint32_t)(((uint64_t)duty * period)
                                  / (uint64_t)RELAY_PWM_DUTY_FULL);

    __HAL_TIM_SET_COMPARE(&htim3, s_tim_channel[channel], compare);
}
