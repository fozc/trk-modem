/**
 * @file  led_driver.c
 * @brief Pattern-driven LED driver implementation.
 *
 * Each LED channel has an independent state machine that walks through
 * the assigned pattern's phase table and repeats. Hardware output uses
 * the generic gpio_set_pin() HAL function.
 */

#include "led_driver.h"
#include "gpio.h"
#include "gpio_defs.h"
#include "console_logger.h"

#include <string.h>
#include <stdbool.h>

/* ======================================================================
 *  Pre-defined pattern constants (stored in flash)
 * ====================================================================== */

const led_pattern_t LED_PATTERN_OFF =
{
    .phases       = {{0, 0}, {0, 0}, {0, 0}},
    .phase_count  = 0U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_SOLID =
{
    .phases       = {{1U, 0U}, {0, 0}, {0, 0}},
    .phase_count  = 1U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_BLINK_250_250 =
{
    .phases       = {{250U, 250U}, {0, 0}, {0, 0}},
    .phase_count  = 1U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_BLINK_500_500 =
{
    .phases       = {{500U, 500U}, {0, 0}, {0, 0}},
    .phase_count  = 1U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_BLINK_1000_1000 =
{
    .phases       = {{1000U, 1000U}, {0, 0}, {0, 0}},
    .phase_count  = 1U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_PULSE_250_2750 =
{
    .phases       = {{250U, 2750U}, {0, 0}, {0, 0}},
    .phase_count  = 1U,
    .repeat_gap_ms = 0U
};

const led_pattern_t LED_PATTERN_DOUBLE_PULSE =
{
    .phases       = {{150U, 150U}, {150U, 0U}, {0, 0}},
    .phase_count  = 2U,
    .repeat_gap_ms = 1500U
};

const led_pattern_t LED_PATTERN_TRIPLE_PULSE =
{
    .phases       = {{150U, 150U}, {150U, 150U}, {150U, 0U}},
    .phase_count  = 3U,
    .repeat_gap_ms = 1500U
};

/* ======================================================================
 *  Hardware pin map — indexed by led_channel_t
 * ====================================================================== */

typedef struct
{
    gpio_port_t port;
    gpio_pin_t  pin;
    uint8_t     active_low;  /**< 1 = active-low, 0 = active-high. */
} led_pin_map_t;

static const led_pin_map_t s_pin_map[LED_CHANNEL_COUNT] =
{
    [LED_CHANNEL_MODEM_R] = { LED_RGB1_R_GPIO, LED_RGB1_R_PIN, 1U },
    [LED_CHANNEL_MODEM_G] = { LED_RGB1_G_GPIO, LED_RGB1_G_PIN, 1U },
    [LED_CHANNEL_MODEM_B] = { LED_RGB1_B_GPIO, LED_RGB1_B_PIN, 1U },
    [LED_CHANNEL_GSM_R]   = { LED_RGB2_R_GPIO, LED_RGB2_R_PIN, 1U },
    [LED_CHANNEL_GSM_G]   = { LED_RGB2_G_GPIO, LED_RGB2_G_PIN, 1U },
    [LED_CHANNEL_GSM_B]   = { LED_RGB2_B_GPIO, LED_RGB2_B_PIN, 1U },
    [LED_CHANNEL_IEC104]  = { LED2_GPIO,       LED2_PIN,       1U },
    [LED_CHANNEL_WEB]     = { LED3_GPIO,       LED3_PIN,       1U },
};

/* ======================================================================
 *  Per-channel runtime state
 * ====================================================================== */

typedef enum
{
    LED_STATE_OFF = 0,   /**< LED is off (pattern has 0 phases).   */
    LED_STATE_ON,        /**< LED is in the ON portion of a phase. */
    LED_STATE_PHASE_GAP, /**< LED is in the OFF portion of phase.  */
    LED_STATE_REPEAT_GAP /**< LED is in the post-pattern gap.      */
} led_run_state_t;

typedef struct
{
    const led_pattern_t *p_pattern;  /**< Active pattern (flash).     */
    uint32_t             timestamp;  /**< Tick when current leg began. */
    uint8_t              phase_idx;  /**< Current phase index.         */
    led_run_state_t      state;      /**< Sub-state within pattern.    */
} led_channel_state_t;

static led_channel_state_t s_channels[LED_CHANNEL_COUNT];

/* ======================================================================
 *  Forward declaration — clock access
 * ====================================================================== */

extern uint32_t clock_time(void);

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

static inline void led_hw_set(led_channel_t channel, uint8_t value)
{
    /* Invert for active-low LEDs. */
    uint8_t hw_val = s_pin_map[channel].active_low
                   ? (value == 0U ? 1U : 0U)
                   : value;

    gpio_set_pin(s_pin_map[channel].port,
                 s_pin_map[channel].pin,
                 hw_val);
}

static void led_channel_reset(led_channel_t channel)
{
    led_channel_state_t *p_ch = &s_channels[channel];

    if ((p_ch->p_pattern == NULL) ||
        (p_ch->p_pattern->phase_count == 0U))
    {
        /* Pattern is OFF. */
        p_ch->state     = LED_STATE_OFF;
        p_ch->phase_idx = 0U;
        p_ch->timestamp = clock_time();
        led_hw_set(channel, 0U);
        return;
    }

    /* Start first phase — ON leg. */
    p_ch->state     = LED_STATE_ON;
    p_ch->phase_idx = 0U;
    p_ch->timestamp = clock_time();
    led_hw_set(channel, 1U);
}

/**
 * @brief Advance a single channel's state machine.
 *
 * Called from led_driver_tick() for each channel.
 */
static void led_channel_tick(led_channel_t channel)
{
    led_channel_state_t *p_ch = &s_channels[channel];
    const led_pattern_t *p_pat = p_ch->p_pattern;

    if ((p_pat == NULL) || (p_pat->phase_count == 0U))
    {
        return; /* Nothing to do — LED stays off. */
    }

    const uint32_t now     = clock_time();
    const uint32_t elapsed = now - p_ch->timestamp;

    switch (p_ch->state)
    {
        case LED_STATE_ON:
        {
            const led_phase_t *p_phase = &p_pat->phases[p_ch->phase_idx];

            /* Solid ON: on_ms > 0 && off_ms == 0 && single phase. */
            if ((p_phase->off_ms == 0U) &&
                (p_ch->phase_idx == (p_pat->phase_count - 1U)) &&
                (p_pat->repeat_gap_ms == 0U))
            {
                return; /* Stay on forever. */
            }

            if (elapsed >= p_phase->on_ms)
            {
                /* Transition: ON -> phase gap (OFF portion). */
                if (p_phase->off_ms > 0U)
                {
                    p_ch->state     = LED_STATE_PHASE_GAP;
                    p_ch->timestamp = now;
                    led_hw_set(channel, 0U);
                }
                else
                {
                    /* off_ms == 0: skip gap, advance to next phase
                     * or repeat gap. */
                    if (p_ch->phase_idx < (p_pat->phase_count - 1U))
                    {
                        p_ch->phase_idx++;
                        p_ch->state     = LED_STATE_ON;
                        p_ch->timestamp = now;
                        led_hw_set(channel, 1U);
                    }
                    else
                    {
                        /* Last phase done — enter repeat gap. */
                        p_ch->state     = LED_STATE_REPEAT_GAP;
                        p_ch->timestamp = now;
                        led_hw_set(channel, 0U);
                    }
                }
            }
            break;
        }

        case LED_STATE_PHASE_GAP:
        {
            const led_phase_t *p_phase = &p_pat->phases[p_ch->phase_idx];

            if (elapsed >= p_phase->off_ms)
            {
                /* Move to next phase or repeat gap. */
                if (p_ch->phase_idx < (p_pat->phase_count - 1U))
                {
                    p_ch->phase_idx++;
                    p_ch->state     = LED_STATE_ON;
                    p_ch->timestamp = now;
                    led_hw_set(channel, 1U);
                }
                else
                {
                    /* Last phase done. */
                    if (p_pat->repeat_gap_ms > 0U)
                    {
                        p_ch->state     = LED_STATE_REPEAT_GAP;
                        p_ch->timestamp = now;
                        /* LED already off from phase gap. */
                    }
                    else
                    {
                        /* No repeat gap — restart immediately. */
                        p_ch->phase_idx = 0U;
                        p_ch->state     = LED_STATE_ON;
                        p_ch->timestamp = now;
                        led_hw_set(channel, 1U);
                    }
                }
            }
            break;
        }

        case LED_STATE_REPEAT_GAP:
        {
            if (elapsed >= p_pat->repeat_gap_ms)
            {
                /* Restart the pattern from phase 0. */
                p_ch->phase_idx = 0U;
                p_ch->state     = LED_STATE_ON;
                p_ch->timestamp = now;
                led_hw_set(channel, 1U);
            }
            break;
        }

        case LED_STATE_OFF:
        default:
            break;
    }
}

/* ======================================================================
 *  RGB group helpers
 * ====================================================================== */

/**
 * @brief Turn off all three channels of an RGB group, then set
 *        one active channel's pattern.
 *
 * @param[in] base      First channel of the RGB group (R).
 * @param[in] active    Which sub-channel to activate (0=R, 1=G, 2=B).
 *                      Pass a value >= 3 to leave all off.
 * @param[in] p_pattern Pattern for the active channel.
 */
static void led_rgb_set(led_channel_t base,
                         uint8_t active,
                         const led_pattern_t *p_pattern)
{
    /* Turn off all three channels first. */
    for (uint8_t i = 0U; i < 3U; i++)
    {
        led_channel_t ch = (led_channel_t)((uint8_t)base + i);
        s_channels[ch].p_pattern = &LED_PATTERN_OFF;
        led_channel_reset(ch);
    }

    /* Activate the selected channel. */
    if (active < 3U)
    {
        led_channel_t ch = (led_channel_t)((uint8_t)base + active);
        s_channels[ch].p_pattern = p_pattern;
        led_channel_reset(ch);
    }
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

void led_driver_init(void)
{
    (void)memset(s_channels, 0, sizeof(s_channels));

    /* Drive all LED GPIO pins LOW unconditionally. */
    for (uint8_t i = 0U; i < (uint8_t)LED_CHANNEL_COUNT; i++)
    {
        gpio_set_pin(s_pin_map[i].port, s_pin_map[i].pin, 0U);
    }

    /* Set pattern state to OFF and apply correct polarity. */
    for (uint8_t i = 0U; i < (uint8_t)LED_CHANNEL_COUNT; i++)
    {
        s_channels[i].p_pattern = &LED_PATTERN_OFF;
        led_hw_set((led_channel_t)i, 0U);
    }
}

void led_driver_tick(void)
{
    for (uint8_t i = 0U; i < (uint8_t)LED_CHANNEL_COUNT; i++)
    {
        led_channel_tick((led_channel_t)i);
    }
}

void led_driver_set_pattern(led_channel_t channel,
                            const led_pattern_t *p_pattern)
{
    if ((uint8_t)channel >= (uint8_t)LED_CHANNEL_COUNT)
    {
        return;
    }

    if (p_pattern == NULL)
    {
        p_pattern = &LED_PATTERN_OFF;
    }

    /* Skip if the same pattern is already active. */
    if (s_channels[channel].p_pattern == p_pattern)
    {
        return;
    }

    s_channels[channel].p_pattern = p_pattern;
    led_channel_reset(channel);
}

void led_driver_set_modem_mode(led_modem_mode_t mode)
{
    switch (mode)
    {
        case LED_MODEM_OFF:
            led_rgb_set(LED_CHANNEL_MODEM_R, 3U, &LED_PATTERN_OFF);
            break;

        case LED_MODEM_POWER_ON:
            /* Yellow = Red + Green, 1000/1000 slow blink — HW booting. */
            s_channels[LED_CHANNEL_MODEM_B].p_pattern = &LED_PATTERN_OFF;
            led_channel_reset(LED_CHANNEL_MODEM_B);
            s_channels[LED_CHANNEL_MODEM_R].p_pattern =
                &LED_PATTERN_BLINK_1000_1000;
            led_channel_reset(LED_CHANNEL_MODEM_R);
            s_channels[LED_CHANNEL_MODEM_G].p_pattern =
                &LED_PATTERN_BLINK_1000_1000;
            led_channel_reset(LED_CHANNEL_MODEM_G);
            break;

        case LED_MODEM_INIT:
            /* Yellow = Red + Green, 250/250 blink. */
            s_channels[LED_CHANNEL_MODEM_B].p_pattern = &LED_PATTERN_OFF;
            led_channel_reset(LED_CHANNEL_MODEM_B);
            s_channels[LED_CHANNEL_MODEM_R].p_pattern =
                &LED_PATTERN_BLINK_250_250;
            led_channel_reset(LED_CHANNEL_MODEM_R);
            s_channels[LED_CHANNEL_MODEM_G].p_pattern =
                &LED_PATTERN_BLINK_250_250;
            led_channel_reset(LED_CHANNEL_MODEM_G);
            break;

        case LED_MODEM_SEARCHING:
            /* Blue, 1000/1000 blink. */
            led_rgb_set(LED_CHANNEL_MODEM_R, 2U,
                        &LED_PATTERN_BLINK_1000_1000);
            break;

        case LED_MODEM_READY:
            /* Green, 250/2750 pulse. */
            led_rgb_set(LED_CHANNEL_MODEM_R, 1U,
                        &LED_PATTERN_PULSE_250_2750);
            break;

        case LED_MODEM_NO_SIM:
            /* Red, double pulse. */
            led_rgb_set(LED_CHANNEL_MODEM_R, 0U,
                        &LED_PATTERN_DOUBLE_PULSE);
            break;

        case LED_MODEM_ERROR:
            /* Red, triple pulse. */
            led_rgb_set(LED_CHANNEL_MODEM_R, 0U,
                        &LED_PATTERN_TRIPLE_PULSE);
            break;

        default:
            break;
    }
}

void led_driver_set_gsm_mode(led_gsm_mode_t mode)
{
    switch (mode)
    {
        case LED_GSM_OFF:
            led_rgb_set(LED_CHANNEL_GSM_R, 3U, &LED_PATTERN_OFF);
            break;

        case LED_GSM_2G:
            /* Red, 250/2750 pulse. */
            led_rgb_set(LED_CHANNEL_GSM_R, 0U,
                        &LED_PATTERN_PULSE_250_2750);
            break;

        case LED_GSM_2G_WEAK:
            /* Red, 500/500 blink. */
            led_rgb_set(LED_CHANNEL_GSM_R, 0U,
                        &LED_PATTERN_BLINK_500_500);
            break;

        case LED_GSM_3G:
            /* Blue, 250/2750 pulse. */
            led_rgb_set(LED_CHANNEL_GSM_R, 2U,
                        &LED_PATTERN_PULSE_250_2750);
            break;

        case LED_GSM_3G_WEAK:
            /* Blue, 500/500 blink. */
            led_rgb_set(LED_CHANNEL_GSM_R, 2U,
                        &LED_PATTERN_BLINK_500_500);
            break;

        case LED_GSM_4G:
            /* Green, solid ON. */
            led_rgb_set(LED_CHANNEL_GSM_R, 1U, &LED_PATTERN_SOLID);
            break;

        case LED_GSM_4G_WEAK:
            /* Green, 500/500 blink. */
            led_rgb_set(LED_CHANNEL_GSM_R, 1U,
                        &LED_PATTERN_BLINK_500_500);
            break;

        default:
            break;
    }
}

static void led_listener_set(led_channel_t channel,
                             led_listener_mode_t mode)
{
    switch (mode)
    {
        case LED_LISTENER_OFF:
            led_driver_set_pattern(channel, &LED_PATTERN_OFF);
            break;

        case LED_LISTENER_LISTENING:
            led_driver_set_pattern(channel, &LED_PATTERN_SOLID);
            break;

        case LED_LISTENER_CONNECTED:
            led_driver_set_pattern(channel, &LED_PATTERN_BLINK_250_250);
            break;

        default:
            break;
    }
}

void led_driver_set_iec104_mode(led_listener_mode_t mode)
{
    led_listener_set(LED_CHANNEL_IEC104, mode);
}

void led_driver_set_web_mode(led_listener_mode_t mode)
{
    led_listener_set(LED_CHANNEL_WEB, mode);
}

/*** end of file ***/
