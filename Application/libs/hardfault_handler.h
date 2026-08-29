/*
 * hardfault_handler.h
 *
 *  Created on: Sep 20, 2022
 *      Author: fatih.ozcan
 */

#ifndef HARDFAULT_HANDLER_H_
#define HARDFAULT_HANDLER_H_

#include <stdint.h>

/* Fault trace stashed in TAMP backup registers (survive system reset) so
 * the next boot can persist it to elog. DR0 is the RTC validity magic. */
#define HF_BKPR_MAGIC      0xC0DEFA17U
#define HF_BKPR_DR_MAGIC   1U
#define HF_BKPR_DR_PC      2U
#define HF_BKPR_DR_LR      3U
#define HF_BKPR_DR_CFSR    4U
#define HF_BKPR_DR_HFSR    5U

#endif /* HARDFAULT_HANDLER_H_ */
