/*
 * rf_monitor_json.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Serialize the canonical RF phase state for the monitor endpoint.
 */

#ifndef RF_MONITOR_JSON_H
#define RF_MONITOR_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* line_filter is a zero-based line index, or -1 for all lines.
 * On insufficient space, return false, set length zero and clear buffer.
 */
bool rf_json_monitor_build(char *buffer, size_t capacity, int32_t line_filter,
                            uint32_t now_ms, size_t *length);

#ifdef __cplusplus
}
#endif

#endif /* RF_MONITOR_JSON_H */

/*** end of file ***/
