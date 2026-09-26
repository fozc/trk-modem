/*
 * mock_nvram.h
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * Host-test mock arayuzu (mock_nvram.c).
 */

#ifndef RF_TEST_MOCK_NVRAM_H_
#define RF_TEST_MOCK_NVRAM_H_

#include "nvram.h"

/** @brief nvram_sync cagri sayaci (senkron testleri icin). */
int mock_nvram_sync_count(void);

/** @brief Mock durumunu sifirla (her test senaryosu oncesi). */
void mock_nvram_reset(void);

#endif /* RF_TEST_MOCK_NVRAM_H_ */
