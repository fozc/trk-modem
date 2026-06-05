/*
 * gsm_firmware_update.h
 *
 *  Created on: Dec 28, 2025
 *      Author: fatih
 */

#ifndef GSM_GSM_FIRMWARE_UPDATE_H_
#define GSM_GSM_FIRMWARE_UPDATE_H_

#include <gsm_types.h>


/** Register HTTP firmware-update callbacks. */
void gsm_firmware_update_init(void);

/** Register RFWU (raw TCP) firmware-update callbacks with the RFWU module. */
void gsm_firmware_update_rfwu_init(void);

#endif /* GSM_GSM_FIRMWARE_UPDATE_H_ */
