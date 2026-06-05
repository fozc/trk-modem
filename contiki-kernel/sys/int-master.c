/*
 * Copyright (c) 2017, George Oikonomou - http://www.spd.gr
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*---------------------------------------------------------------------------*/
/**
 * \file
 * Master interrupt manipulation implementation for the nrf52840
 */
/*---------------------------------------------------------------------------*/

#include "contiki.h"
#include "int-master.h"
#include <stdbool.h>

#if (PLATFORM(_ARM_CORTEXM_))
  #include "cmsis_gcc.h"
#endif

/*---------------------------------------------------------------------------*/
void
int_master_enable(void)
{
#if (PLATFORM(_ARM_CORTEXM_))
  __disable_irq();
#endif
}
/*---------------------------------------------------------------------------*/
int_master_status_t
int_master_read_and_disable(void)
{
#if (PLATFORM(_ARM_CORTEXM_))
  int_master_status_t primask = __get_PRIMASK();

  __disable_irq();
#else
  return 1;
#endif
}
/*---------------------------------------------------------------------------*/
void
int_master_status_set(int_master_status_t status)
{
#if (PLATFORM(_ARM_CORTEXM_))
  __set_PRIMASK(status);
#endif
}
/*---------------------------------------------------------------------------*/
bool
int_master_is_enabled(void)
{
#if (PLATFORM(_ARM_CORTEXM_))
  return __get_PRIMASK() ? false : true;
#else
  return 0;
#endif
}
/*---------------------------------------------------------------------------*/
