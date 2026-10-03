/*
 * stm32u3xx_hal_rng.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only HAL RNG declarations for CMock; no hardware access.
 */
#ifndef TEST_HAL_RNG_H
#define TEST_HAL_RNG_H

#include <stdint.h>

typedef struct
{
    uint32_t unused;
} RNG_HandleTypeDef;

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR
} HAL_StatusTypeDef;

#define RESET 0U
#define RNG_FLAG_CECS 1U
#define RNG_FLAG_SECS 2U
#define RNG_IT_CEI 4U
#define RNG_IT_SEI 8U

extern uint32_t test_rng_flags;
#define __HAL_RNG_GET_FLAG(handle, flag) (test_rng_flags & (flag))
#define __HAL_RNG_GET_IT(handle, flag) (test_rng_flags & (flag))

HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *hrng,
                                             uint32_t *random32bit);

#endif /* TEST_HAL_RNG_H */
/*** end of file ***/
