/*
 * mock_platform.h
 *
 * Host-test doubles for the platform services nvram.c reaches into.
 * The w25qxx driver is replaced by RAM images of the two NVRAM slots
 * with per-slot failure injection and write counters; everything else
 * (console logging, elog, bsp) is silenced.
 */
#ifndef NVRAM_TEST_MOCK_PLATFORM_H_
#define NVRAM_TEST_MOCK_PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>

/* Wipe both slot images to 0xFF and clear all knobs/counters. */
void mock_reset(void);

/* Force the next n write attempts on the given slot to fail. */
void mock_fail_main(int n);
void mock_fail_backup(int n);

/* Cumulative write attempts per slot (failed attempts included). */
uint32_t mock_writes_main(void);
uint32_t mock_writes_backup(void);

/* Raw slot images (sizeof(nvram_t) bytes), for content assertions. */
const uint8_t *mock_slot_image(bool backup);

#endif /* NVRAM_TEST_MOCK_PLATFORM_H_ */
