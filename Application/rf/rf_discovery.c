/*
 * rf_discovery.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * Kesif (discovery) kuyrugu: hatta katilan ama atanmamis cihazlarin
 * EUI-64 kimlikleri. Kaynak SCP 0x14 DISCOVERY_REPORT; SCP yokken kuyruk
 * bos kalir.
 */

#include "rf_discovery.h"
#include "rf_types.h"
#include <string.h>

static uint8_t list[RF_DISCOVERY_MAX][RF_EUI64_LEN];
static uint8_t list_count = 0;

void rf_discovery_reset(void)
{
    list_count = 0;
    memset(list, 0, sizeof(list));
}

bool rf_discovery_add(const uint8_t *eui64)
{
    if (list_count >= RF_DISCOVERY_MAX)
    {
        return false;
    }

    if (eui64 == NULL)
    {
        return false;
    }

    /* Ayni EUI ikinci kez eklenmez (0x14 periyodik tekrarlar olabilir). */
    for (uint8_t i = 0U; i < list_count; i++)
    {
        if (memcmp(list[i], eui64, RF_EUI64_LEN) == 0)
        {
            return false;
        }
    }

    memcpy(list[list_count], eui64, RF_EUI64_LEN);
    list_count++;
    return true;
}

uint8_t rf_discovery_get_count(void)
{
    return list_count;
}

bool rf_discovery_get_device(uint8_t index, uint8_t *eui64_out)
{
    if (index >= list_count)
    {
        return false;
    }

    if (eui64_out == NULL)
    {
        return false;
    }

    memcpy(eui64_out, list[index], RF_EUI64_LEN);
    return true;
}

/*** end of file ***/
