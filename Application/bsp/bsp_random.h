/*
 * bsp_random.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Main-context RNG reads and availability fallback.
 */
#ifndef BSP_RANDOM_H
#define BSP_RANDOM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Read RNG or availability fallback. Main context; NULL returns false. */
bool bsp_random_word(uint32_t *value);
/** Sum of accepted RNG words and the fixed non-secret startup value. */
uint32_t bsp_get_random_accumulator(void);
/** Mix tick and existing TX/RX counters once before fallback generation. */
void bsp_random_fallback_seed(uint32_t tick, uint32_t tx, uint32_t rx);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RANDOM_H */
/*** end of file ***/
