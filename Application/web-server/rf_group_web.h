/*
 * rf_group_web.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Bounded JSON adapter for RF group and application sequence status.
 */

#ifndef RF_GROUP_WEB_H
#define RF_GROUP_WEB_H

#include <stdbool.h>
#include <stddef.h>

bool rf_group_status_json_build(char *buffer, size_t capacity, size_t *length);

#endif /* RF_GROUP_WEB_H */

/*** end of file ***/
