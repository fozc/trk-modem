/*
 * console_logger.h
 *
 * Stub of cslog/console_logger.h for the rfwu_auth host tests. The real
 * build reaches this header through bsp.h; the fixture bsp.h mirrors
 * that include. Macros are silenced with EMPTY bodies exactly matching
 * the real console_logger_config.h disabled branch. CCSLOG drops its
 * color argument in the expansion, so XCOLOR_* codes are not needed.
 */
#ifndef TEST_RFWU_CONSOLE_LOGGER_H
#define TEST_RFWU_CONSOLE_LOGGER_H

#ifndef CSLOG
#define CSLOG(a...)
#endif
#ifndef CCSLOG
#define CCSLOG(color, a...)
#endif
#ifndef CSLOG_ERR
#define CSLOG_ERR(a...)
#endif
#ifndef CSLOG_WARN
#define CSLOG_WARN(a...)
#endif
#ifndef CSLOG_WARN_NODT
#define CSLOG_WARN_NODT(a...)
#endif

#endif /* TEST_RFWU_CONSOLE_LOGGER_H */
/*** end of file ***/
