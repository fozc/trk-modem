/*
 * rf_nvram_fake.c
 *
 *  Created on: Sep 29, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Shared NVRAM fake for RF Ceedling scenarios.
 */

#include "rf_nvram_fake.h"

#include <string.h>

#include "types.h"

static breaker_t fake_breaker;
static int sync_count;
static int sync_result;
static nvram_save_result_t save_result;

breaker_t *nvram_get_breaker_rw(void)
{
    return &fake_breaker;
}

int nvram_sync(bool crc_no_check)
{
    (void)crc_no_check;
    sync_count++;
    return sync_result;
}

nvram_save_result_t nvram_save(bool crc_no_check)
{
    (void)crc_no_check;
    sync_count++;
    return save_result;
}

int rf_nvram_fake_sync_count(void)
{
    return sync_count;
}

void rf_nvram_fake_reset(void)
{
    (void)memset(&fake_breaker, 0, sizeof(fake_breaker));
    sync_count = 0;
    sync_result = 0;
    save_result = NVRAM_SAVE_COMPLETE;
}

void rf_nvram_fake_set_sync_result(int result)
{
    sync_result = result;
    save_result = (0 == result) ? NVRAM_SAVE_COMPLETE : NVRAM_SAVE_FAILED;
}

void rf_nvram_fake_set_save_result(nvram_save_result_t result)
{
    save_result = result;
}

/*** end of file ***/
