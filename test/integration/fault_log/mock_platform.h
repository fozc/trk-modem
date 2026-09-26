/*
 * mock_platform.h
 *
 * Host-test doubles for the platform services fault_log.c reaches into.
 * The 7 feeder log slots (primary + backup copies) live as RAM images
 * with per-slot write failure injection and counters.
 */
#ifndef FAULT_LOG_TEST_MOCK_PLATFORM_H_
#define FAULT_LOG_TEST_MOCK_PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>

/* Wipe all slot images to 0xFF and clear all knobs/counters. */
void mock_reset(void);

/* Force the next n write attempts on the given feeder's copy to fail.
 * The plain knobs reject the call WITHOUT touching the flash image
 * (parameter/protection style); the _destructive knobs model the real
 * driver's erase-before-program failure mode and leave the target slot
 * ERASED (0xFF). */
void mock_fail_primary(uint8_t feeder, int n);
void mock_fail_backup(uint8_t feeder, int n);
void mock_fail_primary_destructive(uint8_t feeder, int n);
void mock_fail_backup_destructive(uint8_t feeder, int n);

/* Fail the next n sector ERASE calls on the given feeder's copy without
 * touching the image (models an erase command rejection). These affect
 * the raw repair path (w25qxx_erase_sector). */
void mock_fail_erase(uint8_t feeder, bool backup, int n);

/* Arm a one-shot torn page program on the given feeder's copy: the first
 * `pages` page programs succeed, the NEXT one programs only half of its
 * bytes and then fails (power cut mid-page); the knob disarms itself
 * after firing. Affects the raw repair path (w25qxx_page_write). */
void mock_torn_program(uint8_t feeder, bool backup, int pages);

/* Cumulative write attempts per feeder copy (failed attempts included). */
uint32_t mock_writes_primary(uint8_t feeder);
uint32_t mock_writes_backup(uint8_t feeder);

/* Direct image access (4096 bytes per feeder slot): tests construct
 * flash states the module itself must never produce. A failed write
 * (see the fail knobs) leaves the target slot ERASED, modelling the
 * real driver's erase-before-program failure mode. */
uint8_t *mock_slot_image_rw(bool backup, uint8_t feeder);

#endif /* FAULT_LOG_TEST_MOCK_PLATFORM_H_ */
