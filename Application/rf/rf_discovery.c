/*
 * rf_discovery.c
 *
 *  Created on: Aug 20, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Kesif (discovery) kuyrugu: hatta katilan ama atanmamis cihazlarin
 * EUI-64 kimlikleri. Kaynak SCP 0x14 DISCOVERY_REPORT; SCP yokken kuyruk
 * bos kalir.
 */

#include "rf_discovery.h"
#include "rf_types.h"
#include <string.h>

static rf_discovery_entry_t list[RF_DISCOVERY_MAX];
static uint8_t list_count = 0U;

static size_t find_device(const uint8_t *eui64)
{
    for (size_t index = 0U; index < list_count; index++)
    {
        if (0 == memcmp(list[index].eui64, eui64, RF_EUI64_LEN))
        {
            return index;
        }
    }
    return list_count;
}

void rf_discovery_reset(void)
{
    list_count = 0U;
    memset(list, 0, sizeof(list));
}

bool rf_discovery_add(const uint8_t *eui64)
{
    if ((NULL == eui64) || (RF_DISCOVERY_MAX <= list_count))
    {
        return false;
    }

    if (list_count != find_device(eui64))
    {
        return false;
    }

    (void)memcpy(list[list_count].eui64, eui64, RF_EUI64_LEN);
    list[list_count].has_rssi = false;
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

    memcpy(eui64_out, list[index].eui64, RF_EUI64_LEN);
    return true;
}

bool rf_discovery_report(const uint8_t *eui64, int8_t rssi)
{
    if (NULL == eui64)
    {
        return false;
    }
    size_t index = find_device(eui64);

    if (list_count == index)
    {
        if (RF_DISCOVERY_MAX <= list_count)
        {
            return false;
        }
        (void)memcpy(list[index].eui64, eui64, RF_EUI64_LEN);
        list_count++;
    }
    list[index].rssi = rssi;
    list[index].has_rssi = true;
    return true;
}

bool rf_discovery_get(size_t index, rf_discovery_entry_t *out)
{
    if ((NULL == out) || (list_count <= index))
    {
        return false;
    }
    *out = list[index];
    return true;
}

bool rf_discovery_remove(const uint8_t *eui64)
{
    if (NULL == eui64)
    {
        return false;
    }
    size_t index = find_device(eui64);

    if (list_count == index)
    {
        return false;
    }
    size_t remaining = (size_t)list_count - (size_t)index - 1U;

    (void)memmove(&list[index], &list[index + 1U],
                  remaining * sizeof(list[0]));
    list_count--;
    (void)memset(&list[list_count], 0, sizeof(list[0]));
    return true;
}

/*** end of file ***/
