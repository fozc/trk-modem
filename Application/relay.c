/**
 * @file  relay.c
 * @brief Peak-and-hold relay driver - implementation.
 *
 * A single Contiki process owns the timing. Public setters latch a
 * per-channel command and poll the process; the process applies the PWM
 * changes and drives a single shared timer, re-armed to the nearest
 * pending peak deadline. Each peak->hold transition is decided from the
 * per-channel state and deadline (Contiki etimers must be armed from the
 * owning process, hence the single timer lives here).
 */

#include <string.h>
#include "relay.h"
#include "relay_pwm.h"
#include "contiki.h"
#include "console_logger.h"
#include "shell.h"
#include "utils.h"

/* ======================================================================
 *  Derived constants
 * ====================================================================== */

/** Peak phase duration in Contiki clock ticks. */
#define RELAY_PEAK_TICKS \
    ((clock_time_t)(((uint32_t)RELAY_PEAK_TIME_MS * (uint32_t)CLOCK_SECOND) \
                    / 1000U))

/* ======================================================================
 *  Types
 * ====================================================================== */

/** Energisation phase of a single relay channel. */
typedef enum
{
    RELAY_STATE_OFF = 0,  /**< De-energised (0 % duty). */
    RELAY_STATE_PEAK,     /**< Pull-in phase (100 % duty). */
    RELAY_STATE_HOLD      /**< Steady hold phase (hold duty). */
} relay_state_t;

/** Pending command latched by relay_set() for the process to consume. */
typedef enum
{
    RELAY_CMD_NONE = 0,  /**< No pending command. */
    RELAY_CMD_ON,        /**< Energise request. */
    RELAY_CMD_OFF        /**< Release request. */
} relay_cmd_t;

/* ======================================================================
 *  Module state
 *
 *  Commands are written by relay_set() (any context) and consumed by the
 *  process. A single-word store is atomic on Cortex-M; the volatile
 *  qualifier prevents the compiler from caching the value.
 * ====================================================================== */

static volatile relay_cmd_t s_cmd[RELAY_CH_COUNT];
static relay_state_t        s_state[RELAY_CH_COUNT];
static clock_time_t         s_peak_deadline[RELAY_CH_COUNT];
static struct etimer        s_timer;

PROCESS(relay_process, "relay");

/* ======================================================================
 *  Internal helpers
 * ====================================================================== */

/**
 * @brief Apply a latched command for one channel (process context).
 */
static void relay_apply_cmd(uint8_t ch)
{
    relay_cmd_t cmd = s_cmd[ch];
    if (cmd == RELAY_CMD_NONE)
    {
        return;
    }

    s_cmd[ch] = RELAY_CMD_NONE;

    if (cmd == RELAY_CMD_ON)
    {
        relay_pwm_set_duty((relay_pwm_channel_t)ch, RELAY_PEAK_DUTY_PERMILLE);
        s_state[ch]         = RELAY_STATE_PEAK;
        s_peak_deadline[ch] = (clock_time_t)(clock_time() + RELAY_PEAK_TICKS);
        CSLOG("[RELAY] CH%u ON -> PEAK\r\n", (unsigned)(ch + 1U));
    }
    else
    {
        relay_pwm_set_duty((relay_pwm_channel_t)ch, 0U);
        s_state[ch] = RELAY_STATE_OFF;
        CSLOG("[RELAY] CH%u OFF\r\n", (unsigned)(ch + 1U));
    }
}

/**
 * @brief Drop every channel whose peak deadline has elapsed to hold.
 */
static void relay_service_peaks(void)
{
    clock_time_t now = clock_time();

    for (uint8_t ch = 0U; ch < (uint8_t)RELAY_CH_COUNT; ch++)
    {
        if (s_state[ch] != RELAY_STATE_PEAK)
        {
            continue;
        }

        /* Signed difference tolerates clock_time_t wrap-around. */
        int32_t remaining = (int32_t)(s_peak_deadline[ch] - now);
        if (remaining <= 0)
        {
            relay_pwm_set_duty((relay_pwm_channel_t)ch,
                               RELAY_HOLD_DUTY_PERMILLE);
            s_state[ch] = RELAY_STATE_HOLD;
            CSLOG("[RELAY] CH%u PEAK -> HOLD\r\n", (unsigned)(ch + 1U));
        }
    }
}

/**
 * @brief Arm the shared timer for the nearest pending peak deadline.
 *
 * Stops the timer when no channel is in the peak phase.
 */
static void relay_rearm_timer(void)
{
    clock_time_t now         = clock_time();
    clock_time_t nearest     = 0U;
    bool         has_pending = false;

    for (uint8_t ch = 0U; ch < (uint8_t)RELAY_CH_COUNT; ch++)
    {
        if (s_state[ch] != RELAY_STATE_PEAK)
        {
            continue;
        }

        int32_t      diff      = (int32_t)(s_peak_deadline[ch] - now);
        clock_time_t remaining = (diff > 0) ? (clock_time_t)diff : 0U;

        if ((!has_pending) || (remaining < nearest))
        {
            nearest     = remaining;
            has_pending = true;
        }
    }

    if (has_pending)
    {
        etimer_set(&s_timer, nearest);
    }
    else
    {
        etimer_stop(&s_timer);
    }
}

/* ======================================================================
 *  Contiki process
 * ====================================================================== */

PROCESS_THREAD(relay_process, ev, data)
{
    (void)data;

    PROCESS_BEGIN();

    while (1)
    {
        /* Wake only on the two triggers that need work:
         *   - PROCESS_EVENT_POLL : a command latched by relay_set().
         *   - PROCESS_EVENT_TIMER: the shared peak timer expired. */
        PROCESS_WAIT_EVENT_UNTIL((ev == PROCESS_EVENT_POLL)
                                 || (ev == PROCESS_EVENT_TIMER));

        if (ev == PROCESS_EVENT_POLL)
        {
            for (uint8_t ch = 0U; ch < (uint8_t)RELAY_CH_COUNT; ch++)
            {
                relay_apply_cmd(ch);
            }
        }

        /* Decide peak->hold transitions from state, then re-arm the
         * shared timer for whatever peak deadline is still pending. */
        relay_service_peaks();
        relay_rearm_timer();
    }

    PROCESS_END();
}

/* ======================================================================
 *  Shell command
 * ====================================================================== */

/**
 * @brief Shell handler for manual relay control and status.
 *
 *   relay status            - show the state of all channels
 *   relay <1..N> on|off     - energise/release one channel
 */
static int relay_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        CSLOG("Usage: relay status\r\n");
        CSLOG("       relay <1..%u> <on|off>\r\n",
              (unsigned)RELAY_CH_COUNT);
        return -1;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        for (uint8_t ch = 0U; ch < (uint8_t)RELAY_CH_COUNT; ch++)
        {
            CSLOG("[RELAY] CH%u: %s\r\n", (unsigned)(ch + 1U),
                  relay_is_on((relay_channel_t)ch) ? "ON" : "OFF");
        }
        return 0;
    }

    if (argc < 3)
    {
        CSLOG("Usage: relay <1..%u> <on|off>\r\n",
              (unsigned)RELAY_CH_COUNT);
        return -1;
    }

    int ch_no = xstrtoi(argv[1]);
    if ((ch_no < 1) || (ch_no > (int)RELAY_CH_COUNT))
    {
        CSLOG("[RELAY] Invalid channel %d (valid: 1..%u)\r\n",
              ch_no, (unsigned)RELAY_CH_COUNT);
        return -1;
    }

    bool on;
    if (strcmp(argv[2], "on") == 0)
    {
        on = true;
    }
    else if (strcmp(argv[2], "off") == 0)
    {
        on = false;
    }
    else
    {
        CSLOG("[RELAY] Invalid action '%s' (valid: on|off)\r\n", argv[2]);
        return -1;
    }

    relay_set((relay_channel_t)(ch_no - 1), on);
    return 0;
}

/* ======================================================================
 *  Public API
 * ====================================================================== */

void relay_init(void)
{
    relay_pwm_init();

    for (uint8_t ch = 0U; ch < (uint8_t)RELAY_CH_COUNT; ch++)
    {
        s_cmd[ch]   = RELAY_CMD_NONE;
        s_state[ch] = RELAY_STATE_OFF;
        relay_pwm_set_duty((relay_pwm_channel_t)ch, 0U);
    }

    shell_register_command(&(shell_cmd_t){
        .cmd   = "relay",
        .desc  = "Manual relay control\r\n"
                 "\trelay status        - show all channel states\r\n"
                 "\trelay <1..2> on|off - energise/release a channel",
        .level = SHELL_LVL_USER,
        .func  = relay_shell_handler
    });

    process_start(&relay_process, NULL);
}

void relay_set(relay_channel_t channel, bool on)
{
    if ((uint8_t)channel >= (uint8_t)RELAY_CH_COUNT)
    {
        return;
    }

    s_cmd[channel] = on ? RELAY_CMD_ON : RELAY_CMD_OFF;
    process_poll(&relay_process);
}

bool relay_is_on(relay_channel_t channel)
{
    if ((uint8_t)channel >= (uint8_t)RELAY_CH_COUNT)
    {
        return false;
    }

    /* Reflect a pending command so the getter is consistent before the
     * process has run. */
    if (s_cmd[channel] == RELAY_CMD_ON)
    {
        return true;
    }
    if (s_cmd[channel] == RELAY_CMD_OFF)
    {
        return false;
    }

    return (s_state[channel] != RELAY_STATE_OFF);
}

