/*
 * console_logger.h
 *
 *  Created on: Dec 22, 2025
 *      Author: fatih
 */

#ifndef LIBS_CONSOLE_LOGGER_H_
#define LIBS_CONSOLE_LOGGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "console_logger_config.h"


static inline bool console_logger_is_enabled(void)
{
    extern bool console_logger_is_enabled_flag;
	return console_logger_is_enabled_flag;
}

void console_logger_set_enabled(bool enabled, bool persist);
void console_logger_init(void);

#endif /* LIBS_CONSOLE_LOGGER_H_ */
