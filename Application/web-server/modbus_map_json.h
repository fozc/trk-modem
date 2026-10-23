/*
 * modbus_map_json.h
 *
 *  Created on: Oct 15, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Read-only register metadata for the Modbus configuration page.
 */
#ifndef MODBUS_MAP_JSON_H
#define MODBUS_MAP_JSON_H

#include "../../libraries/json/json.h"

#include <stdbool.h>
#include <stddef.h>

void modbus_map_json_write(json_writer_t *writer);

/* On failure, clear the output and set length to zero when writable. */
bool modbus_map_json_build(char *buffer, size_t capacity, size_t *length);

#endif /* MODBUS_MAP_JSON_H */

/*** end of file ***/
