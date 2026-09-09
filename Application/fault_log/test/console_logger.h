/*
 * Stub of cslog/console_logger.h for the fault_log host tests. The CSLOG
 * macros are silenced with EMPTY bodies exactly matching the real
 * console_logger_config.h disabled branch (-DNO_CONSOLE_LOG), so any
 * include order redefines them identically without diagnostics.
 */
#ifndef FAULT_LOG_TEST_STUB_CONSOLE_LOGGER_H_
#define FAULT_LOG_TEST_STUB_CONSOLE_LOGGER_H_

#include "xprintf.h"

void console_logger_init(void);

#ifndef CSLOG
#define CSLOG(a...)
#endif
#ifndef CCSLOG
#define CCSLOG(color, a...)
#endif
#ifndef CSLOG_NODT
#define CSLOG_NODT(a...)
#endif
#ifndef CCSLOG_NODT
#define CCSLOG_NODT(color, a...)
#endif
#ifndef CSLOG_ERR
#define CSLOG_ERR(a...)
#endif
#ifndef CSLOG_WARN
#define CSLOG_WARN(a...)
#endif

#endif /* FAULT_LOG_TEST_STUB_CONSOLE_LOGGER_H_ */
