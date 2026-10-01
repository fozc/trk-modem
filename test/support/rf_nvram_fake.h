/*
 * rf_nvram_fake.h
 *
 *  Created on: Sep 29, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Shared NVRAM fake for RF Ceedling scenarios.
 */

#ifndef TEST_SUPPORT_RF_NVRAM_FAKE_H_
#define TEST_SUPPORT_RF_NVRAM_FAKE_H_

#include "nvram.h"

int rf_nvram_fake_sync_count(void);
void rf_nvram_fake_reset(void);

#endif /* TEST_SUPPORT_RF_NVRAM_FAKE_H_ */

/*** end of file ***/
