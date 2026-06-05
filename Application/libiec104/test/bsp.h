/*
 * bsp.h
 *
 * Mock BSP header for host-based testing
 */

#ifndef MOCK_BSP_H_
#define MOCK_BSP_H_

#include <stdint.h>
#include <stdbool.h>

/* Console logger mock */
bool console_logger_is_enabled(void);
void console_logger_enable(void);
void console_logger_disable(void);

/* xprintf mock */
void xprintf(const char *format, ...);

/* CSLOG macro */
#define CSLOG(fmt, ...) \
	do { \
		if (console_logger_is_enabled()) { \
			xprintf(fmt, ##__VA_ARGS__); \
		} \
	} while(0)

#endif /* MOCK_BSP_H_ */
