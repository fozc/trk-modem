/*
 * console_logger.c
 *
 *  Created on: Dec 22, 2025
 *      Author: fatih
 *
 * Konsol loglarinin tek merkezi: on/off durumu, modul seviye tablosu,
 * "cslog" ve "log" shell komutlari. Seviyeler NVRAM'de kalici;
 * http, gsm ile ayni alani paylasir.
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

/* ================================================================== */
/* Modul seviye tablosu                                                */
/* ================================================================== */

static uint8_t levels[LOG_MOD_COUNT];

static const char *const module_names[LOG_MOD_COUNT] =
{
	"gsm", "rf", "http", "iec104"
};

static uint8_t clamped(uint8_t stored)
{
	if (stored > (uint8_t)LOG_LVL_VERBOSE)
	{
		return (uint8_t)LOG_LVL_NORMAL;
	}

	return stored;
}

void console_logger_set_level(log_mod_t module, log_lvl_t level)
{
	if (((uint8_t)module >= (uint8_t)LOG_MOD_COUNT)
	    || ((uint8_t)level > (uint8_t)LOG_LVL_VERBOSE))
	{
		return;
	}

	levels[module] = (uint8_t)level;

	switch (module)
	{
		case LOG_MOD_GSM:
		case LOG_MOD_HTTP:
			/* Ayni NVRAM alanini paylasirlar: birlikte guncellenirler. */
			levels[LOG_MOD_GSM]  = (uint8_t)level;
			levels[LOG_MOD_HTTP] = (uint8_t)level;
			nvram_set_gsm_log_level((uint8_t)level);
			break;

		case LOG_MOD_RF:
			nvram_set_rf_log_level((uint8_t)level);
			break;

		case LOG_MOD_IEC104:
			nvram_set_iec104_log_level((uint8_t)level);
			break;

		default:             /* MISRA 16.4 */
			break;
	}

	nvram_sync(false);
}

log_lvl_t console_logger_get_level(log_mod_t module)
{
	if ((uint8_t)module >= (uint8_t)LOG_MOD_COUNT)
	{
		return LOG_LVL_NORMAL;
	}

	return (log_lvl_t)levels[module];
}

const char *console_logger_module_name(log_mod_t module)
{
	if ((uint8_t)module >= (uint8_t)LOG_MOD_COUNT)
	{
		return "?";
	}

	return module_names[module];
}

bool console_logger_level_enabled(log_mod_t module, log_lvl_t min_level)
{
	if (!console_logger_is_enabled_flag)
	{
		return false;
	}

	if ((uint8_t)module >= (uint8_t)LOG_MOD_COUNT)
	{
		return (min_level <= LOG_LVL_NORMAL);
	}

	return ((log_lvl_t)levels[module] >= min_level);
}

/* ================================================================== */
/* "cslog" komutu: konsol cikisi on/off                                */
/* ================================================================== */

static int console_logger_shell_cmd_handler(int argc, char *argv[])
{
	if (argc != 2)
	{
		SHELL_LOG("Usage: cslog <on/1|off/0>\r\n");
	    return -1;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0)
	{
		console_logger_set_enabled(true, true);
		SHELL_LOG("Console logger started!\r\n");
	    return 0;
	}

	if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0)
	{
		SHELL_LOG("Console logger stopped!\r\n");
		console_logger_set_enabled(false, true);
	    return 0;
	}

	SHELL_LOG("Unknown argument!\r\n");

	return -1;
}

/* ================================================================== */
/* "log" komutu: modul seviyeleri                                      */
/* ================================================================== */

static const char *log_level_name(log_lvl_t lvl)
{
	switch (lvl)
	{
		case LOG_LVL_OFF:     return "OFF";
		case LOG_LVL_NORMAL:  return "NORMAL (hata/uyari)";
		case LOG_LVL_VERBOSE: return "VERBOSE (tum iz)";
		default:              return "?";
	}
}

static int log_parse_level(const char *text)
{
	if (0 == strcmp(text, "off"))
	{
		return (int)LOG_LVL_OFF;
	}

	if (0 == strcmp(text, "on"))
	{
		return (int)LOG_LVL_NORMAL;
	}

	if (0 == strcmp(text, "verbose"))
	{
		return (int)LOG_LVL_VERBOSE;
	}

	return -1;
}

static int log_parse_module(const char *text)
{
	for (int module = 0; module < (int)LOG_MOD_COUNT; module++)
	{
		if (0 == strcmp(text, module_names[module]))
		{
			return module;
		}
	}

	return -1;
}

static int console_log_shell_handler(int argc, char *argv[])
{
	/* log -> durum listesi */
	if (argc < 2)
	{
		for (int module = 0; module < (int)LOG_MOD_COUNT; module++)
		{
			SHELL_LOG("%-7s: %s%s\r\n",
			          module_names[module],
			          log_level_name(console_logger_get_level((log_mod_t)module)),
			          (module == (int)LOG_MOD_HTTP) ? "  [gsm'yi takip eder]" : "");
		}

		SHELL_LOG("Usage: log <gsm|rf|http|iec104|all> <off|on|verbose>\r\n");
		return 0;
	}

	int level = log_parse_level(argv[argc - 1]);

	if (level < 0)
	{
		SHELL_LOG("Bilinmeyen seviye '%s'. Usage: off | on | verbose\r\n",
		          argv[argc - 1]);
		return -1;
	}

	/* log all <seviye> */
	if (0 == strcmp(argv[1], "all"))
	{
		for (int module = 0; module < (int)LOG_MOD_COUNT; module++)
		{
			console_logger_set_level((log_mod_t)module, (log_lvl_t)level);
		}

		SHELL_LOG("Tum moduller: %s\r\n", log_level_name((log_lvl_t)level));
		return 0;
	}

	/* log <modul> <seviye> */
	int module = log_parse_module(argv[1]);

	if (module < 0)
	{
		SHELL_LOG("Bilinmeyen modul '%s'. Usage: gsm | rf | http | iec104 | all\r\n",
		          argv[1]);
		return -1;
	}

	if (module == (int)LOG_MOD_HTTP)
	{
		SHELL_LOG("http, gsm seviyesini takip eder - gsm ayarlaniyor\r\n");
	}

	console_logger_set_level((log_mod_t)module, (log_lvl_t)level);
	SHELL_LOG("%s: %s\r\n", module_names[module],
	          log_level_name((log_lvl_t)level));
	return 0;
}

void console_logger_init(void)
{
	console_logger_is_enabled_flag = nvram_is_cslog_enabled();

	levels[LOG_MOD_GSM]    = clamped(nvram_get_gsm_log_level());
	levels[LOG_MOD_RF]     = clamped(nvram_get_rf_log_level());
	levels[LOG_MOD_IEC104] = clamped(nvram_get_iec104_log_level());
	levels[LOG_MOD_HTTP]   = levels[LOG_MOD_GSM];

	shell_register_command(&(shell_cmd_t){.cmd = "cslog",
		.desc = "Console logger control\r\n"
		        "cslog <on/1|off/0>",
		.func = console_logger_shell_cmd_handler});

	shell_register_command(&(shell_cmd_t){.cmd = "log",
		.desc = "Log seviyeleri: log [gsm|rf|http|iec104|all] [off|on|verbose]",
		.level = SHELL_LVL_USER,
		.func = console_log_shell_handler});
}

/*** end of file ***/
