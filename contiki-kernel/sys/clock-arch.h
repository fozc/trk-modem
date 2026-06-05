
#ifndef clock_arch_h_
#define clock_arch_h_

#include <stdint.h>

#ifdef CONTIKI_CPU_USAGE_KERNEL
#include "dwt.h"
#endif

typedef uint32_t clock_time_t;

#define system_time() (clock_seconds())
#define CLOCK_CONF_SECOND 1000

void clock_tick(void);

#endif
