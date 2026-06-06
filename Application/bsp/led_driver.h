/**
 * @file  led_driver.h
 * @brief Pattern-driven LED driver for single-color and RGB LEDs.
 *
 * Provides a table-driven blink pattern engine that supports multi-phase
 * patterns (single pulse, double pulse, triple pulse, solid, off).
 * Each logical LED channel runs its own state machine, all driven from
 * a single periodic tick call.
 *
 * Hardware mapping (active-high, accent LEDs accent accent accent):
 *
 * | Channel              | GPIO              | Function          |
 * |----------------------|-------------------|-------------------|
 * | LED_CHANNEL_MODEM_R  | RGB1 Red   PE11   | Modem status red  |
 * | LED_CHANNEL_MODEM_G  | RGB1 Green PE12   | Modem status grn  |
 * | LED_CHANNEL_MODEM_B  | RGB1 Blue  PE13   | Modem status blu  |
 * | LED_CHANNEL_GSM_R    | RGB2 Red   PE8    | GSM network red   |
 * | LED_CHANNEL_GSM_G    | RGB2 Green PE9    | GSM network grn   |
 * | LED_CHANNEL_GSM_B    | RGB2 Blue  PE10   | GSM network blu   |
 * | LED_CHANNEL_IEC104   | LED2       PB2    | IEC104 listener   |
 * | LED_CHANNEL_WEB      | LED1       PE7    | Web listener      |
 */

#ifndef BSP_LED_DRIVER_H
#define BSP_LED_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ======================================================================
 *  LED channels — one entry per physical pin driven by this module.
 * ====================================================================== */

typedef enum
{
    LED_CHANNEL_MODEM_R = 0, /**< RGB1 Red   — modem status.   */
    LED_CHANNEL_MODEM_G,     /**< RGB1 Green — modem status.   */
    LED_CHANNEL_MODEM_B,     /**< RGB1 Blue  — modem status.   */
    LED_CHANNEL_GSM_R,       /**< RGB2 Red   — GSM network.    */
    LED_CHANNEL_GSM_G,       /**< RGB2 Green — GSM network.    */
    LED_CHANNEL_GSM_B,       /**< RGB2 Blue  — GSM network.    */
    LED_CHANNEL_IEC104,      /**< LED2       — IEC104 listener. */
    LED_CHANNEL_WEB,         /**< LED3       — Web listener.    */
    LED_CHANNEL_COUNT        /**< Total channel count.          */
} led_channel_t;

/* ======================================================================
 *  Blink pattern definitions
 *
 *  A pattern consists of up to LED_PATTERN_MAX_PHASES on/off phase
 *  pairs followed by a gap (off period) before the pattern repeats.
 *
 *  Special cases:
 *    - Solid ON:  single phase {on=1, off=0}, gap=0 — never turns off.
 *    - Off:       phase_count=0 — LED stays off permanently.
 * ====================================================================== */

#define LED_PATTERN_MAX_PHASES  3U  /**< Max on/off pairs per pattern. */

/** Single on/off phase within a blink pattern. */
typedef struct
{
    uint16_t on_ms;   /**< Duration LED is ON  (milliseconds). */
    uint16_t off_ms;  /**< Duration LED is OFF (milliseconds). */
} led_phase_t;

/** Complete blink pattern descriptor (stored in flash). */
typedef struct
{
    led_phase_t phases[LED_PATTERN_MAX_PHASES]; /**< Phase table.        */
    uint8_t     phase_count;  /**< Number of active phases (0 = off).    */
    uint16_t    repeat_gap_ms; /**< Extra off-time after last phase.     */
} led_pattern_t;

/* ======================================================================
 *  Pre-defined patterns (declared in led_driver.c)
 * ====================================================================== */

/** LED off — no blinking. */
extern const led_pattern_t LED_PATTERN_OFF;

/** Solid ON — continuously lit. */
extern const led_pattern_t LED_PATTERN_SOLID;

/** 250 ms ON / 250 ms OFF — fast symmetric blink. */
extern const led_pattern_t LED_PATTERN_BLINK_250_250;

/** 500 ms ON / 500 ms OFF — medium blink (weak signal). */
extern const led_pattern_t LED_PATTERN_BLINK_500_500;

/** 1000 ms ON / 1000 ms OFF — slow symmetric blink. */
extern const led_pattern_t LED_PATTERN_BLINK_1000_1000;

/** 250 ms ON / 2750 ms OFF — short pulse, long pause. */
extern const led_pattern_t LED_PATTERN_PULSE_250_2750;

/** Double pulse: (150 on / 150 off) x2, then 1500 ms gap. */
extern const led_pattern_t LED_PATTERN_DOUBLE_PULSE;

/** Triple pulse: (150 on / 150 off) x3, then 1500 ms gap. */
extern const led_pattern_t LED_PATTERN_TRIPLE_PULSE;

/* ======================================================================
 *  Composite LED mode IDs — high-level states for RGB groups.
 *
 *  These combine a color (which channels are active) with a blink
 *  pattern. The led_driver_set_mode() function translates a mode ID
 *  into per-channel pattern assignments.
 * ====================================================================== */

/* --- Modem status LED modes (RGB1) --- */
typedef enum
{
    LED_MODEM_OFF = 0,           /**< All off.                           */
    LED_MODEM_POWER_ON,          /**< Yellow 1000/1000 — hardware boot.  */
    LED_MODEM_INIT,              /**< Yellow 250/250  — initializing.    */
    LED_MODEM_SEARCHING,         /**< Blue   1000/1000 — searching net.  */
    LED_MODEM_READY,             /**< Green  250/2750  — connected.      */
    LED_MODEM_NO_SIM,            /**< Red    double pulse — SIM error.   */
    LED_MODEM_ERROR,             /**< Red    triple pulse — generic err. */
} led_modem_mode_t;

/* --- GSM network LED modes (RGB2) --- */
typedef enum
{
    LED_GSM_OFF = 0,             /**< All off — no network.              */
    LED_GSM_2G,                  /**< Red   250/2750  — 2G registered.   */
    LED_GSM_2G_WEAK,             /**< Red   500/500   — 2G weak signal.  */
    LED_GSM_3G,                  /**< Blue  250/2750  — 3G registered.   */
    LED_GSM_3G_WEAK,             /**< Blue  500/500   — 3G weak signal.  */
    LED_GSM_4G,                  /**< Green solid     — 4G registered.   */
    LED_GSM_4G_WEAK,             /**< Green 500/500   — 4G weak signal.  */
} led_gsm_mode_t;

/* --- Listener socket LED modes (LED2, LED3) --- */
typedef enum
{
    LED_LISTENER_OFF = 0,        /**< Off — socket closed, no connection. */
    LED_LISTENER_LISTENING,      /**< Solid ON  — listening for conn.    */
    LED_LISTENER_CONNECTED,      /**< 250/250   — active connection.     */
} led_listener_mode_t;

/* ======================================================================
 *  Public API
 * ====================================================================== */

/**
 * @brief Initialize the LED driver.
 *
 * Configures all LED channels to OFF and sets up the internal
 * state machines. Must be called once at startup before any other
 * led_driver function.
 */
void led_driver_init(void);

/**
 * @brief Periodic tick — call from the main loop at 10 Hz or faster.
 *
 * Advances all channel state machines based on elapsed time.
 * Uses clock_time() internally; safe to call at any rate.
 */
void led_driver_tick(void);

/**
 * @brief Set a raw blink pattern on a single channel.
 *
 * @param[in] channel  LED channel to update.
 * @param[in] p_pattern Pointer to a pattern descriptor (must remain
 *                      valid — typically a flash-resident constant).
 */
void led_driver_set_pattern(led_channel_t channel,
                            const led_pattern_t *p_pattern);

/**
 * @brief Set the modem status LED mode (RGB1 group).
 *
 * Translates the high-level mode into per-channel (R/G/B) patterns.
 *
 * @param[in] mode  Desired modem status indication.
 */
void led_driver_set_modem_mode(led_modem_mode_t mode);

/**
 * @brief Set the GSM network LED mode (RGB2 group).
 *
 * Translates the high-level mode into per-channel (R/G/B) patterns.
 *
 * @param[in] mode  Desired GSM network indication.
 */
void led_driver_set_gsm_mode(led_gsm_mode_t mode);

/**
 * @brief Set the IEC104 listener LED mode (LED2).
 *
 * @param[in] mode  Desired IEC104 listener indication.
 */
void led_driver_set_iec104_mode(led_listener_mode_t mode);

/**
 * @brief Set the web listener LED mode (LED3).
 *
 * @param[in] mode  Desired web listener indication.
 */
void led_driver_set_web_mode(led_listener_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_DRIVER_H */

/*** end of file ***/
