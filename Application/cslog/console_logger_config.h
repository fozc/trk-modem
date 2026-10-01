/*
 * console_logger_config.h
 *
 *  Created on: Dec 22, 2025
 *      Author: fatih
 *
 * Konsol loglarinin tek merkezi: modul/seviye tablosu
 * console_logger.c'de, cikti kapisu buradaki CSLOG ailesi.
 *
 * Modul bazli kontrol (Zephyr LOG_MODULE_REGISTER / ChromeEC dosya
 * basi CC_ sarmalayicisi ile ayni desen): kaynak .c dosyasi TUM
 * include'lardan once
 *
 *     #define CSLOG_MODULE LOG_MOD_RF
 *
 * tanimlar; o dosyanin CSLOG/CSLOG_ERR/CSLOG_WARN/CSLOG_NODT cagrilari
 * otomatik olarak RF modulunun seviyesine baglanir. Seviye eslemesi:
 *   CSLOG_ERR / CSLOG_WARN        -> en az NORMAL
 *   CSLOG / CCSLOG / *_NODT       -> yalniz VERBOSE
 * CSLOG_MODULE tanimlanmamis dosyalarda makrolar eski davranisi
 * korur (yalniz cslog on/off kapisindan gecer) - boylece gecis
 * dosya dosya yapilabilir.
 *
 * Seviyeler: "log" shell komutu (log <gsm|rf|http|iec104|all>
 * <off|on|verbose>) - bkz. console_logger.c. http, gsm ile ayni NVRAM
 * alanini paylasir.
 */

#ifndef CONSOLE_LOGGER_CONFIG_H_
#define CONSOLE_LOGGER_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "xprintf.h"

/* -- Log modulleri ve seviyeler ------------------------------------ */

typedef enum
{
    LOG_MOD_GSM = 0,    /**< AT motoru, GSM bilgi satirlari        */
    LOG_MOD_RF,         /**< RF hub SCP trafigi + ham paket dokumu */
    LOG_MOD_HTTP,       /**< web sunucusu (gsm seviyesini takip)   */
    LOG_MOD_IEC104,     /**< IEC-104 SCADA baglanti akislari       */
    LOG_MOD_COUNT
} log_mod_t;

typedef enum
{
    LOG_LVL_OFF     = 0,   /**< cikti yok                     */
    LOG_LVL_NORMAL  = 1,   /**< yalniz hata + uyari           */
    LOG_LVL_VERBOSE = 2    /**< tam iz + ham paket dokumleri  */
} log_lvl_t;

#ifndef NO_CONSOLE_LOG
	#define  BSP_CONSOLE_LOG
#endif


#ifdef   BSP_CONSOLE_LOG

	extern bool console_logger_level_enabled(log_mod_t module,
	                                         log_lvl_t min_level);

	#ifdef CSLOG_MODULE
	/* -- Modul bagli dosya: seviye + enable cift kapisi ------------ */

		#define CSLOG_NODT(a...)\
		              do{ \
						if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_VERBOSE)){ \
					      xprintf(a); \
	                    } \
						}while(0)
		#define CCSLOG_NODT(color, a...)\
					  do{ \
						if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_VERBOSE)){ \
						  xcprintf(color, a); \
						} \
						}while(0)

	extern void rtc_print_now(void);
		#define CSLOG(a...) \
						do{ \
				if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_VERBOSE)){ \
				rtc_print_now(); \
				xprintf(a); \
					} \
						}while(0)
		#define CCSLOG(color, a...) \
				do{ \
				if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_VERBOSE)){ \
				rtc_print_now(); \
				xcprintf(color, a); \
				}\
				}while(0)
		/* Renkli NODT varyantlari uyari/hata sinifinda: >= NORMAL. */
		#define CSLOG_ERR_NODT(a...)\
				do{ \
				if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_NORMAL)){ \
				xcprintf(XCOLOR_RED, a); \
				}\
				}while(0)
		#define CSLOG_WARN_NODT(a...)\
				do{ \
				if(console_logger_level_enabled(CSLOG_MODULE, LOG_LVL_NORMAL)){ \
				xcprintf(XCOLOR_YELLOW, a); \
				}\
				}while(0)

	#else
	/* -- Modul tanimsiz dosya: yalniz cslog on/off kapisi (eski
	 *    davranis - kademeli gecis icin korunur) ------------------ */

		#define CSLOG_NODT(a...)\
		              do{ \
						if(console_logger_is_enabled()){ \
					      xprintf(a); \
	                    } \
						}while(0)
		#define CCSLOG_NODT(color, a...)\
					  do{ \
						if(console_logger_is_enabled()){ \
						  xcprintf(color, a); \
						} \
						}while(0)

	extern void rtc_print_now(void);
		#define CSLOG(a...) \
						do{ \
				if(console_logger_is_enabled()){ \
				rtc_print_now(); \
				xprintf(a); \
					} \
						}while(0)
		#define CCSLOG(color, a...) \
				do{ \
				if(console_logger_is_enabled()){ \
				rtc_print_now(); \
				xcprintf(color, a); \
				}\
				}while(0)
		#define CSLOG_ERR_NODT(a...)  CCSLOG_NODT(XCOLOR_RED, a)
		#define CSLOG_WARN_NODT(a...) CCSLOG_NODT(XCOLOR_YELLOW, a)

	#endif /* CSLOG_MODULE */

	#define CSLOG_ERR(a...)  CCSLOG(XCOLOR_RED, a)
	#define CSLOG_WARN(a...) CCSLOG(XCOLOR_YELLOW, a)

#else
	#ifndef CSLOG_NODT
		#define CSLOG_NODT(...) \
			do { if (false) { xprintf(__VA_ARGS__); } } while (0)
	#endif
	#ifndef CCSLOG_NODT
		#define CCSLOG_NODT(color, ...) \
			do { if (false) { xcprintf((color), __VA_ARGS__); } } while (0)
	#endif
	#ifndef CSLOG
		#define CSLOG(...) CSLOG_NODT(__VA_ARGS__)
	#endif
	#ifndef CCSLOG
		#define CCSLOG(color, ...) CCSLOG_NODT((color), __VA_ARGS__)
	#endif
	#ifndef CSLOG_ERR
		#define CSLOG_ERR(...) CSLOG_NODT(__VA_ARGS__)
	#endif
	#ifndef CSLOG_WARN
		#define CSLOG_WARN(...) CSLOG_NODT(__VA_ARGS__)
	#endif
	#ifndef CSLOG_ERR_NODT
		#define CSLOG_ERR_NODT(...) CSLOG_NODT(__VA_ARGS__)
	#endif
	#ifndef CSLOG_WARN_NODT
		#define CSLOG_WARN_NODT(...) CSLOG_NODT(__VA_ARGS__)
	#endif
#endif

#endif /* CONSOLE_LOGGER_CONFIG_H_ */
