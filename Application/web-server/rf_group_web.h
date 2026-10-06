/*
 * rf_group_web.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Web request and bounded JSON adapter for the RF group service.
 */

#ifndef RF_GROUP_WEB_H
#define RF_GROUP_WEB_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    RF_WEB_APPLY_INVALID = 0,
    RF_WEB_APPLY_NOT_STARTED,
    RF_WEB_APPLY_STARTED
} rf_web_apply_result_t;

/* suffix: one-based store line / fresh group ID, e.g. "1/7". */
rf_web_apply_result_t rf_web_start_apply(const char *suffix);
bool rf_group_status_json_build(char *buffer, size_t capacity, size_t *length);

#endif /* RF_GROUP_WEB_H */

/*** end of file ***/
