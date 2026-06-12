/**
 * @file  digital_input.c
 * @brief Debounced digital input reader — implementation.
 *
 * A Contiki process samples the 4 DIN GPIO pins and 2 DIP switches
 * every 30 ms and feeds each sample into an integrator-based debouncer.
 * The debounced state is inverted (active-LOW → logical 1) and exposed
 * through the public getter API.
 */

#include "digital_input.h"
#include "gpio.h"
#include "gpio_defs.h"
#include "debouncer.h"
#include "contiki.h"
#include "console_logger.h"
#include "bsp.h"
#include "nvram.h"

/* ======================================================================
 *  Configuration
 * ====================================================================== */

/** Sampling period in Contiki clock ticks (30 ms). */
#define DIN_SAMPLE_TICKS    ((clock_time_t)(CLOCK_SECOND * 30U / 1000U))

/** Number of consistent samples required to change state (~150 ms). */
#define DIN_DEBOUNCE_COUNT  5U

/* ----------------------------------------------------------------------
 *  USER_RESET_SW press-duration thresholds (decided on release)
 *  - held <  RESET           : too short    -> ignored
 *  - RESET <= held <  NVRAM  : MCU reset
 *  - NVRAM <= held <  OTHER  : NVRAM factory reset + reset
 *  - held >= OTHER           : reserved     -> log only
 * -------------------------------------------------------------------- */

/** MCU-reset threshold: 3 s. */
#define USER_RESET_RESET_TICKS  ((clock_time_t)((uint32_t)CLOCK_SECOND * 3U))

/** NVRAM factory-reset threshold: 15 s. */
#define USER_RESET_NVRAM_TICKS  ((clock_time_t)((uint32_t)CLOCK_SECOND * 15U))

/** Reserved-action threshold: 10 s. */
#define USER_RESET_OTHER_TICKS  ((clock_time_t)((uint32_t)CLOCK_SECOND * 10U))

/* ======================================================================
 *  Pin map — indexed by din_channel_t / dip_sw_channel_t
 * ====================================================================== */

typedef struct
{
    gpio_port_t port;
    gpio_pin_t  pin;
} din_pin_map_t;

static const din_pin_map_t s_din_map[DIN_CH_COUNT] =
{
    [DIN_CH_1] = { DIN1_BSP_GPIO, DIN1_BSP_PIN },
    [DIN_CH_2] = { DIN2_BSP_GPIO, DIN2_BSP_PIN },
    [DIN_CH_3] = { DIN3_BSP_GPIO, DIN3_BSP_PIN },
    [DIN_CH_4] = { DIN4_BSP_GPIO, DIN4_BSP_PIN },
};

static const din_pin_map_t s_dip_map[DIP_SW_COUNT] =
{
    [DIP_SW_1] = { DIP_SW1_GPIO, DIP_SW1_PIN },
    [DIP_SW_2] = { DIP_SW2_GPIO, DIP_SW2_PIN },
};

static const din_pin_map_t s_btn_map =
{
    USER_RESET_SW_GPIO, USER_RESET_SW_PIN
};

/* ======================================================================
 *  Module state
 * ====================================================================== */

static debouncer_t s_din_db[DIN_CH_COUNT];
static debouncer_t s_dip_db[DIP_SW_COUNT];
static debouncer_t s_btn_db;

/** Tick captured on the USER_RESET_SW press edge. */
static clock_time_t s_btn_press_start;

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

/**
 * @brief Seed a debouncer array from the current pin levels.
 */
static void seed_debouncers(debouncer_t *p_db,
                            const din_pin_map_t *p_map,
                            uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        uint8_t raw = gpio_read_pin(p_map[i].port, p_map[i].pin);

        p_db[i].debounce_time = DIN_DEBOUNCE_COUNT;
        p_db[i].integrator    = (raw != 0U) ? DIN_DEBOUNCE_COUNT : 0U;
        p_db[i].stable_state  = raw;
        p_db[i].flag          = 0U;
    }
}

/**
 * @brief Sample and debounce a pin array.
 */
static void sample_pins(debouncer_t *p_db,
                        const din_pin_map_t *p_map,
                        uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        uint8_t raw = gpio_read_pin(p_map[i].port, p_map[i].pin);
        (void)debouncer(&p_db[i], raw);
    }
}

/**
 * @brief Execute the USER_RESET_SW action selected by press duration.
 *
 * @param[in] held_ticks  Debounced press duration in Contiki ticks.
 *
 * @note The MCU-reset and NVRAM-reset actions do not return (they reset
 *       the MCU).
 */
static void user_reset_dispatch(clock_time_t held_ticks)
{
    if (held_ticks >= USER_RESET_OTHER_TICKS)
    {
        /* >= 15 s: reserved for a future action. */
        CSLOG("[USER_RST] >=15s press -> log only\r\n");
    }
    else if (held_ticks >= USER_RESET_NVRAM_TICKS)
    {
        /* >= 10 s: restore factory defaults, then reboot. */
        CSLOG("[USER_RST] >=10s press -> NVRAM factory reset\r\n");
        nvram_set_defaults();
        (void)nvram_sync(true);
        bsp_system_reset();
        /* Never reached. */
    }
    else if (held_ticks >= USER_RESET_RESET_TICKS)
    {
        /* >= 3 s: plain MCU reset. */
        CSLOG("[USER_RST] >=3s press -> MCU reset\r\n");
        bsp_system_reset();
        /* Never reached. */
    }
    else
    {
        /* < 3 s: too short, ignore. */
        CSLOG("[USER_RST] press too short -> ignored\r\n");
    }
}

/* ======================================================================
 *  Contiki process
 * ====================================================================== */

PROCESS(digital_input_process, "digital-input");

PROCESS_THREAD(digital_input_process, ev, data)
{
    static struct etimer timer;

    (void)ev;
    (void)data;

    PROCESS_BEGIN();

    etimer_set(&timer, DIN_SAMPLE_TICKS);

    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        etimer_restart(&timer);

        /* Sample DIN channels and log state changes. */
        for (uint8_t i = 0U; i < (uint8_t)DIN_CH_COUNT; i++)
        {
            uint8_t raw = gpio_read_pin(s_din_map[i].port,
                                        s_din_map[i].pin);
            if (debouncer(&s_din_db[i], raw) != 0)
            {
                /* Active-LOW: stable 0 = active. */
                uint8_t active = (s_din_db[i].stable_state == 0U)
                               ? 1U : 0U;
                CSLOG("[DIN] CH%u -> %u\r\n",
                      (unsigned)(i + 1U), (unsigned)active);
            }
        }

        /* Sample DIP switches and log state changes. */
        for (uint8_t i = 0U; i < (uint8_t)DIP_SW_COUNT; i++)
        {
            uint8_t raw = gpio_read_pin(s_dip_map[i].port,
                                        s_dip_map[i].pin);
            if (debouncer(&s_dip_db[i], raw) != 0)
            {
                uint8_t on = (s_dip_db[i].stable_state == 0U)
                           ? 1U : 0U;
                CSLOG("[DIP] SW%u -> %s\r\n",
                      (unsigned)(i + 1U),
                      (on != 0U) ? "ON" : "OFF");
            }
        }

        /* Sample the user reset button and act on its press duration. */
        {
            uint8_t raw = gpio_read_pin(s_btn_map.port, s_btn_map.pin);
            if (debouncer(&s_btn_db, raw) != 0)
            {
                /* Active-LOW: stable 0 = pressed. */
                if (s_btn_db.stable_state == 0U)
                {
                    s_btn_press_start = clock_time();
                }
                else
                {
                    clock_time_t held =
                        (clock_time_t)(clock_time() - s_btn_press_start);
                    user_reset_dispatch(held);
                }
            }
        }
    }

    PROCESS_END();
}

/* ======================================================================
 *  Public API — DIN
 * ====================================================================== */

void digital_input_init(void)
{
    seed_debouncers(s_din_db, s_din_map, (uint8_t)DIN_CH_COUNT);
    seed_debouncers(s_dip_db, s_dip_map, (uint8_t)DIP_SW_COUNT);
    seed_debouncers(&s_btn_db, &s_btn_map, 1U);

    process_start(&digital_input_process, NULL);
}

uint8_t digital_input_get(din_channel_t channel)
{
    if ((uint8_t)channel >= (uint8_t)DIN_CH_COUNT)
    {
        return 0U;
    }

    /* Active-LOW: pin LOW (stable_state == 0) → logical 1. */
    return (s_din_db[channel].stable_state == 0U) ? 1U : 0U;
}

uint8_t digital_input_get_all(void)
{
    uint8_t mask = 0U;

    for (uint8_t i = 0U; i < (uint8_t)DIN_CH_COUNT; i++)
    {
        if (s_din_db[i].stable_state == 0U)
        {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

/* ======================================================================
 *  Public API — DIP switches
 * ====================================================================== */

uint8_t dip_switch_get(dip_sw_channel_t sw)
{
    if ((uint8_t)sw >= (uint8_t)DIP_SW_COUNT)
    {
        return 0U;
    }

    /* Active-LOW: pin LOW (stable_state == 0) → switch ON (1). */
    return (s_dip_db[sw].stable_state == 0U) ? 1U : 0U;
}

uint8_t dip_switch_get_all(void)
{
    uint8_t mask = 0U;

    for (uint8_t i = 0U; i < (uint8_t)DIP_SW_COUNT; i++)
    {
        if (s_dip_db[i].stable_state == 0U)
        {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

/*** end of file ***/
