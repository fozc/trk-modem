/*
 * rf_dummy.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Explicit synthetic R1 samples for development, never a live data producer.
 */

#include "rf_dummy.h"
#include <string.h>

static uint32_t step;
static bool initialized;

void rf_dummy_init(void)
{
    step = 0U;
    initialized = true;
}

void rf_dummy_tick(void)
{
    if (initialized)
    {
        step++;
    }
}

bool rf_dummy_get_live(uint8_t source, rf_scp_live_t *out)
{
    uint8_t feeder = (source >> 2U) & 0x07U;
    uint8_t phase = source & 0x03U;

    if (!initialized || (NULL == out) || (0U == feeder) ||
        (4U < feeder) || (0U == phase))
    {
        return false;
    }
    rf_scp_live_t sample = {0};

    sample.source = source;
    sample.seq = step;
    sample.uptime_sec = step * 5U;
    sample.state = 1U;
    sample.current_amps = 5.0f + (float)(step % 10U) * 0.1f;
    sample.trip_voltage = 32.0f;
    sample.harvest_voltage = 13.0f;
    sample.temperature = 25;
    sample.rssi = -75;
    sample.flags = 3U;
    *out = sample;
    return true;
}

/*** end of file ***/
