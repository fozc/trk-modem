/*
 * system_status_json.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Bounded system monitor JSON with SCP power quality.
 */
#ifndef SYSTEM_STATUS_JSON_H
#define SYSTEM_STATUS_JSON_H

#include "system_status.h"
#include <stddef.h>

bool system_status_json_build(const system_status_t *status, char *buffer,
                              size_t capacity, size_t *length);

#endif /* SYSTEM_STATUS_JSON_H */
/*** end of file ***/
