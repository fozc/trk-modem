/*
 * mock_platform.h
 *
 * Host-test double for the w25qxx driver as used by the two-slot NVRAM
 * core: two independent 4 KB sectors with real NOR semantics (erase sets
 * 0xFF, programming can only clear bits) and phase-level fault injection
 * at the erase / program / verify boundaries.
 */
#ifndef NVRAM_TEST_MOCK_PLATFORM_H_
#define NVRAM_TEST_MOCK_PLATFORM_H_

#include <stdint.h>
#include <stdbool.h>

/* Injection point for a whole-image write (w25qxx_write_buff). */
typedef enum
{
    MOCK_FAIL_NONE = 0,
    MOCK_FAIL_BEFORE_ERASE,   /* rejected before touching the flash */
    MOCK_FAIL_AFTER_ERASE,    /* sector erased, then failure */
    MOCK_FAIL_AFTER_PROGRAM   /* data programmed, verify reported failed */
} mock_fail_stage_t;

/* Slot indices: 0 = A (NVRAM_ADDRESS), 1 = B (NVRAM_BACKUP_ADDRESS). */
#define MOCK_SLOT_A 0U
#define MOCK_SLOT_B 1U

/* Wipe both slot images to 0xFF and clear all knobs/counters. */
void mock_reset(void);

/* One-shot whole-image failure injection for the given slot. */
void mock_fail_write(uint32_t slot, mock_fail_stage_t stage);

/* One-shot erase failure for w25qxx_erase_sector on the given slot. */
void mock_fail_erase(uint32_t slot);

/* Page-program failure AFTER `page` successful pages (0-based): the
 * destination ends up TORN - first pages written, rest erased. -1 = off. */
void mock_fail_page(uint32_t slot, int page);

/* Cumulative operation counters per slot (failed attempts included). */
uint32_t mock_write_buff_calls(uint32_t slot);
uint32_t mock_erase_calls(uint32_t slot);
uint32_t mock_page_writes(uint32_t slot);

/* Direct image access (4096 bytes) for constructing flash states. */
uint8_t *mock_slot_image_rw(uint32_t slot);

#endif /* NVRAM_TEST_MOCK_PLATFORM_H_ */
