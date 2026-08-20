/*
 * mock_nvram.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * Host-test mock: rf_config.c'nin bagimli oldugu nvram arayuzunun en
 * kucik yerine getirmesi (sadece store tarafinin ihtiyaci olanlar).
 */

#include "nvram.h"
#include "types.h"
#include <string.h>

static breaker_t mock_breaker = {0};

static int mock_sync_count = 0;

breaker_t *nvram_get_breaker_rw(void)
{
    return &mock_breaker;
}

int nvram_sync(bool crc_no_check)
{
    (void)crc_no_check;
    mock_sync_count++;
    return 0;
}

int mock_nvram_sync_count(void)
{
    return mock_sync_count;
}

void mock_nvram_reset(void)
{
    memset(&mock_breaker, 0, sizeof(mock_breaker));
    mock_sync_count = 0;
}
