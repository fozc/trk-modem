/*---------------------------------------------------------------------------*/
#ifndef __PLATFORM_CONF_H__
#define __PLATFORM_CONF_H__
/*---------------------------------------------------------------------------*/
#include <inttypes.h>
#include <string.h>

/*---------------------------------------------------------------------------*/

#define CPU_CLOCK_FREQ_   550000000
#define CPU_TIME_RATIO    ((CPU_CLOCK_FREQ_) / 1000000) //For usec

//#define CONTIKI_CPU_USAGE_KERNEL    //Define this to measure total core CPU usage.
//#define CONTIKI_CPU_USAGE_PROCESS   //Define this to measure tasks CPU usage.

#define CC_CONF_REGISTER_ARGS          		0
#define CC_CONF_FUNCTION_POINTER_ARGS  		1
#define CC_CONF_FASTCALL					1
#define CC_CONF_VA_ARGS                		1
#define CC_CONF_NO_VA_ARGS			   		0
#define AUTOSTART_ENABLE			   		1

#ifndef CC_CONF_INLINE
	#define CC_CONF_INLINE                 	inline
#endif

#define CCIF
#define CLIF

#ifndef BV
#define BV(x) 								(1<<(x))
#endif

#define CC_BYTE_ALIGNED 					__attribute__ ((packed, aligned(1)))

#define CC_WEAK_FUNCTION 					__attribute__((weak))

#define _ARM_CORTEXM_ 						0x100F
#define _WIN32_       						0x200F
#define _LINUX_       						0x300F

#define _PLATFORM_ 							_WIN32_

#ifndef _PLATFORM_
	#error "You must define platfom!"
#endif

#if defined _PLATFORM_ && (_PLATFORM_ != _ARM_CORTEXM_ && _PLATFORM_ != _WIN32_ && _PLATFORM_ != _LINUX_)
	#error "Unknown Platform defined!"
#endif

#define PLATFORM(x) ((x) == _PLATFORM_)

/*---------------------------------------------------------------------------*/
#endif /* __PLATFORM_CONF_H__ */
/*---------------------------------------------------------------------------*/
