/*
 * Stub of cslog/console_logger.h for the nvram host tests. The CSLOG
 * macros come from the real console_logger_config.h compiled out via
 * -DNO_CONSOLE_LOG; this stub only provides the xcprintf declaration
 * (and the XCOLOR_* codes) plus console_logger_init.
 */
#ifndef NVRAM_TEST_STUB_CONSOLE_LOGGER_H_
#define NVRAM_TEST_STUB_CONSOLE_LOGGER_H_

#include "xprintf.h"

void console_logger_init(void);

#endif /* NVRAM_TEST_STUB_CONSOLE_LOGGER_H_ */
