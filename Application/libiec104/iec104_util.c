/*
 * iec104_util.c
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */
#include "iec104_util.h"

uint32_t iec104_ioa_3byte_to_uint32(ioa_3byte_t ioa)
{
    return (ioa.ioa_low | (ioa.ioa_mid << 8) | (ioa.ioa_high << 16));
}

ioa_3byte_t iec104_make_ioa_3byte(uint32_t ioa)
{
    return (ioa_3byte_t){
        .ioa_low = (uint8_t)(ioa & 0xFF),
        .ioa_mid = (uint8_t)((ioa >> 8) & 0xFF),
        .ioa_high = (uint8_t)((ioa >> 16) & 0xFF)
    };
}

bool iec104_ioa_3byte_equals(ioa_3byte_t ioa1, ioa_3byte_t ioa2)
{
    return (ioa1.ioa_low == ioa2.ioa_low) &&
           (ioa1.ioa_mid == ioa2.ioa_mid) &&
           (ioa1.ioa_high == ioa2.ioa_high);
}
