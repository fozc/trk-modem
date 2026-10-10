/*
 * iec104_platform_fake.h
 *
 *  Created on: Oct 01, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host platform boundaries for real IEC104 protocol tests.
 */

#ifndef IEC104_PLATFORM_FAKE_H
#define IEC104_PLATFORM_FAKE_H

#include <stdint.h>
#include <stdbool.h>

#include "fault_log.h"
#include "rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every mock back to its power-on state. */
void mock_platform_reset(void);

uint32_t mock_rtc_sync_count(void);
rtc_t mock_rtc_last_sync(void);
void mock_rtc_sync_failure(bool failed);

/* Marks power line 'feeder_id' as in use so the emitters walk it. */
void mock_breaker_set_line_in_use(uint8_t feeder_id, bool in_use);

/* Fills the fault log of one feeder/phase with 'count' synthetic entries.
 * Entry n carries fault_current = n so the tests can spot a repeat or a
 * gap in the emitted objects. */
void mock_fault_log_fill(uint8_t feeder_id, uint8_t phase_id,
                         fault_log_type_t type, uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* IEC104_PLATFORM_FAKE_H */

/*** end of file ***/
