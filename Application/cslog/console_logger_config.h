/*
 * console_logger.h
 *
 *  Created on: Dec 22, 2025
 *      Author: fatih
 */

#ifndef CONSOLE_LOGGER_CONFIG_H_
#define CONSOLE_LOGGER_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "xprintf.h"

#ifndef NO_CONSOLE_LOG
	#define  BSP_CONSOLE_LOG
#endif


#ifdef   BSP_CONSOLE_LOG
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
	#define CSLOG_ERR_NODT(a...)  CCSLOG_NODT(XCOLOR_RED, a)
	#define CSLOG_WARN_NODT(a...) CCSLOG_NODT(XCOLOR_YELLOW, a)
#else
	#define CSLOG_NODT(a...)
	#define CCSLOG_NODT(color, a...)
	#define CSLOG_ERR_NODT(a...)
	#define CSLOG_WARN_NODT(a...)
#endif

 

#ifdef   BSP_CONSOLE_LOG
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
		#define CSLOG_ERR(a...)  CCSLOG(XCOLOR_RED, a)
		#define CSLOG_WARN(a...) CCSLOG(XCOLOR_YELLOW, a)
	#else
		#define CSLOG(a...)
		#define CCSLOG(color, a...)
		#define CSLOG_ERR(a...)
		#define CSLOG_WARN(a...)
#endif

#endif /* CONSOLE_LOGGER_CONFIG_H_ */
