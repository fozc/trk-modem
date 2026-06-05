/*
 * gsm_signal_led.c
 *
 * Classifies AT+CESQ readings into (technology, level) and delegates
 * the actual LED output to the weak BSP hook gsm_signal_led_apply().
 *
 * CESQ field mapping:
 *   rxlev (field 0) → 2G GSM    0-63,   99  = not available
 *   rscp  (field 2) → 3G WCDMA  0-96,   255 = not available
 *   rsrp  (field 5) → 4G LTE    0-97,   255 = not available
 *
 * Strength thresholds (3GPP TS 27.007 §8.69):
 *
 *   rxlev  [0-63]  →  0: WEAK  / 15: FAIR  / 25: GOOD  / 36: EXCELLENT
 *   rscp   [0-96]  →  0: WEAK  / 25: FAIR  / 49: GOOD  / 72: EXCELLENT
 *   rsrp   [0-97]  →  0: WEAK  / 25: FAIR  / 49: GOOD  / 73: EXCELLENT
 */

#include "gsm_signal_led.h"
#include "gsm_info.h"

/* CESQ "not available" sentinels */
#define RXLEV_UNAVAIL  99U
#define RSCP_UNAVAIL  255U
#define RSRP_UNAVAIL  255U

/* Thresholds for field-to-level mapping */
#define RXLEV_WEAK_MAX  14U
#define RXLEV_FAIR_MAX  24U
#define RXLEV_GOOD_MAX  35U

#define RSCP_WEAK_MAX  24U
#define RSCP_FAIR_MAX  48U
#define RSCP_GOOD_MAX  71U

#define RSRP_WEAK_MAX  24U
#define RSRP_FAIR_MAX  48U
#define RSRP_GOOD_MAX  72U

/* ======================================================================
 * Module state
 * ====================================================================== */

static gsm_signal_tech_t  s_tech  = GSM_SIGNAL_TECH_NONE;
static gsm_signal_level_t s_level = GSM_SIGNAL_LEVEL_NONE;

/* ======================================================================
 * Internal — raw value → level
 * ====================================================================== */

static gsm_signal_level_t level_from_rxlev(uint8_t rxlev)
{
    if (rxlev >= RXLEV_UNAVAIL)  { return GSM_SIGNAL_LEVEL_NONE;      }
    if (rxlev <= RXLEV_WEAK_MAX) { return GSM_SIGNAL_LEVEL_WEAK;      }
    if (rxlev <= RXLEV_FAIR_MAX) { return GSM_SIGNAL_LEVEL_FAIR;      }
    if (rxlev <= RXLEV_GOOD_MAX) { return GSM_SIGNAL_LEVEL_GOOD;      }
    return                                GSM_SIGNAL_LEVEL_EXCELLENT;
}

static gsm_signal_level_t level_from_rscp(uint8_t rscp)
{
    if (rscp >= RSCP_UNAVAIL)  { return GSM_SIGNAL_LEVEL_NONE;      }
    if (rscp <= RSCP_WEAK_MAX) { return GSM_SIGNAL_LEVEL_WEAK;      }
    if (rscp <= RSCP_FAIR_MAX) { return GSM_SIGNAL_LEVEL_FAIR;      }
    if (rscp <= RSCP_GOOD_MAX) { return GSM_SIGNAL_LEVEL_GOOD;      }
    return                              GSM_SIGNAL_LEVEL_EXCELLENT;
}

static gsm_signal_level_t level_from_rsrp(uint8_t rsrp)
{
    if (rsrp >= RSRP_UNAVAIL)  { return GSM_SIGNAL_LEVEL_NONE;      }
    if (rsrp <= RSRP_WEAK_MAX) { return GSM_SIGNAL_LEVEL_WEAK;      }
    if (rsrp <= RSRP_FAIR_MAX) { return GSM_SIGNAL_LEVEL_FAIR;      }
    if (rsrp <= RSRP_GOOD_MAX) { return GSM_SIGNAL_LEVEL_GOOD;      }
    return                              GSM_SIGNAL_LEVEL_EXCELLENT;
}

/* ======================================================================
 * Weak BSP hook
 *
 * Override this in bsp.c once the RGB LED pins are added to gpio_defs.h.
 *
 * Example implementation:
 *
 *   void gsm_signal_led_apply(gsm_signal_tech_t tech, gsm_signal_level_t level)
 *   {
 *       uint8_t r = 0U, g = 0U, b = 0U;
 *       switch (tech) {
 *           case GSM_SIGNAL_TECH_2G: r = 1U; break;
 *           case GSM_SIGNAL_TECH_3G: b = 1U; break;
 *           case GSM_SIGNAL_TECH_4G: g = 1U; break;
 *           default: break;                          // all off
 *       }
 *       // Dim for WEAK — blink handled by caller's periodic tick
 *       gpio_set_pin(RGB_R_GPIO, RGB_R_PIN, r);
 *       gpio_set_pin(RGB_G_GPIO, RGB_G_PIN, g);
 *       gpio_set_pin(RGB_B_GPIO, RGB_B_PIN, b);
 *   }
 * ====================================================================== */

__attribute__((weak)) void gsm_signal_led_apply(gsm_signal_tech_t  tech,
                                                 gsm_signal_level_t level)
{
    (void)tech;
    (void)level;
    /* No-op until BSP layer implements this. */
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void gsm_signal_led_update(void)
{
    const uint8_t rxlev = gsm_info_get_signal_quality_2G();
    const uint8_t rscp  = gsm_info_get_signal_quality_3G();
    const uint8_t rsrp  = gsm_info_get_signal_quality_4G();

    /* Determine the highest-priority technology that is currently available.
     * Priority: 4G LTE (rsrp) > 3G WCDMA (rscp) > 2G GSM (rxlev). */
    if (rsrp < RSRP_UNAVAIL)
    {
        s_tech  = GSM_SIGNAL_TECH_4G;
        s_level = level_from_rsrp(rsrp);
    }
    else if (rscp < RSCP_UNAVAIL)
    {
        s_tech  = GSM_SIGNAL_TECH_3G;
        s_level = level_from_rscp(rscp);
    }
    else if (rxlev < RXLEV_UNAVAIL)
    {
        s_tech  = GSM_SIGNAL_TECH_2G;
        s_level = level_from_rxlev(rxlev);
    }
    else
    {
        s_tech  = GSM_SIGNAL_TECH_NONE;
        s_level = GSM_SIGNAL_LEVEL_NONE;
    }

    gsm_signal_led_apply(s_tech, s_level);
}

gsm_signal_tech_t gsm_signal_led_get_tech(void)
{
    return s_tech;
}

gsm_signal_level_t gsm_signal_led_get_level(void)
{
    return s_level;
}
