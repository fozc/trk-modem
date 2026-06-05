/*
 * console_logger.c
 *
 *  Created on: Dec 22, 2025
 *      Author: fatih
 */
#include "console_logger.h"
#include "shell.h"
#include <string.h>
#include "bsp.h"
#include "nvram.h"

bool console_logger_is_enabled_flag = false;

void console_logger_set_enabled(bool enabled, bool persist)
{
	console_logger_is_enabled_flag = enabled;
	if(persist){
		nvram_set_cslog_enabled(console_logger_is_enabled_flag);
		nvram_sync(false);
	}
}

static int console_logger_shell_cmd_handler(int argc, char *argv[])
{
	if (argc != 2)
	{
		CSLOG("Usage: cslog <on/1|off/0>\r\n");
	    return -1;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0)
	{
		console_logger_set_enabled(true, true);
		CSLOG("Console logger started!\r\n");
	    return 0;
	}

	if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0)
	{
		CSLOG("Console logger stopped!\r\n");
		console_logger_set_enabled(false, true);
	    return 0;
	}

	CSLOG("Unknown argument!\r\n");

	return -1;
}


void console_logger_init(void)
{
	console_logger_is_enabled_flag = nvram_is_cslog_enabled();

	shell_register_command(&(shell_cmd_t){.cmd = "cslog",
		.desc = "Console logger control\r\n"
		        "cslog <on/1|off/0>",
		.func = console_logger_shell_cmd_handler});
}
