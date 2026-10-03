/*
 * bsp_random.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Main-context RNG reads and availability fallback.
 */
#include "bsp_random.h"

#include "console_logger.h"

#include "stm32u3xx_hal.h"
#include "stm32u3xx_hal_rng.h"

#include <stddef.h>

extern RNG_HandleTypeDef hrng;

/* Fixed startup value, not a secret. Unsigned sum wraps modulo 2^32. */
static uint32_t random_accumulator = 0xE6B3E419U;
static uint32_t fallback_state = 1U;
static bool fallback_active;

uint32_t bsp_get_random_accumulator(void)
{
    return random_accumulator;
}

/** Hardware entropy is consumed only by the cooperative main context. */
static bool read_hardware_word(uint32_t *value)
{
    uint32_t word = 0U;

    if (NULL == value)
    {
        return false;
    }
    *value = 0U;
    if ((RESET != __HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_CECS)) ||
        (RESET != __HAL_RNG_GET_IT(&hrng, RNG_IT_CEI)))
    {
        return false;
    }
    /* Let HAL recover a current seed error before reading entropy. */
    if (HAL_OK != HAL_RNG_GenerateRandomNumber(&hrng, &word))
    {
        return false;
    }
    if ((RESET != __HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_CECS)) ||
        (RESET != __HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_SECS)) ||
        (RESET != __HAL_RNG_GET_IT(&hrng, RNG_IT_CEI)) ||
        (RESET != __HAL_RNG_GET_IT(&hrng, RNG_IT_SEI)))
    {
        return false;
    }
    random_accumulator += word;
    *value = word;
    return true;
}

bool bsp_random_secure_word(uint32_t *value)
{
    return read_hardware_word(value);
}

void bsp_random_fallback_seed(uint32_t tick, uint32_t tx, uint32_t rx)
{
    fallback_state += random_accumulator + tick;
    fallback_state ^= rx;
    fallback_state += tx;
    if (0U == fallback_state)
    {
        fallback_state = 1U;
    }
}

static bool generate_fallback_word(uint32_t *value)
{
    if (NULL == value)
    {
        return false;
    }
    fallback_state ^= fallback_state << 13U;
    fallback_state ^= fallback_state >> 17U;
    fallback_state ^= fallback_state << 5U;
    *value = fallback_state;
    return true;
}

bool bsp_random_word(uint32_t *value)
{
    if (NULL == value)
    {
        return false;
    }
    if (read_hardware_word(value))
    {
        fallback_active = false;
        return true;
    }
    if (!fallback_active)
    {
        CSLOG_WARN("[RNG] Hardware unavailable; "
                   "availability fallback active\r\n");
        fallback_active = true;
    }
    return generate_fallback_word(value);
}

/*** end of file ***/
