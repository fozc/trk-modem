/*
 * bsp_random.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only RFWU v2 dependency double.
 */
#ifndef TEST_RFWU_RANDOM_H
#define TEST_RFWU_RANDOM_H
#include <stdbool.h>
#include <stdint.h>
bool bsp_random_secure_word(uint32_t *value);
#endif
/*** end of file ***/
