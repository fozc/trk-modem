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

/* -- Modul seviye API'si (tablo console_logger.c'de) --------------- */

/** @brief Modul seviyesini ayarla (NVRAM'e yazilir + sync). */
void console_logger_set_level(log_mod_t module, log_lvl_t level);

/** @brief Gecerli modul seviyesi. */
log_lvl_t console_logger_get_level(log_mod_t module);

/** @brief Kisa modul adi: "gsm" / "rf" / "http" / "iec104". */
const char *console_logger_module_name(log_mod_t module);

/** @brief CSLOG ailesinin kapi fonksiyonu: konsol acik VE modul
 *         seviyesi minimum seviyede mi? (makrolar bunu cagirir) */
bool console_logger_level_enabled(log_mod_t module, log_lvl_t min_level);

#endif /* LIBS_CONSOLE_LOGGER_H_ */
