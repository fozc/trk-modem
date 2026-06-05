/*
 * mock_bsp.c
 *
 * Mock BSP (Board Support Package) for host-based testing
 */

#include "bsp.h"
#include <stdarg.h>
#include <stdio.h>

/* Console logger state */
static bool console_enabled = true;

bool console_logger_is_enabled(void)
{
	return console_enabled;
}

void console_logger_enable(void)
{
	console_enabled = true;
}

void console_logger_disable(void)
{
	console_enabled = false;
}

/* xprintf implementation - just redirects to printf */
void xprintf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}
