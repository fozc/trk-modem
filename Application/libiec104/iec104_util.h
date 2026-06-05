/*
 * iec104_util.h
 *
 *  Created on: Jan 31, 2026
 *      Author: fatih
 */

#ifndef LIBIEC104_IEC104_UTIL_H_
#define LIBIEC104_IEC104_UTIL_H_

#include <stdint.h>
#include <stdbool.h>
#include "iec104_types.h"

uint32_t iec104_ioa_3byte_to_uint32(ioa_3byte_t ioa);
ioa_3byte_t iec104_make_ioa_3byte(uint32_t ioa);
bool iec104_ioa_3byte_equals(ioa_3byte_t ioa1, ioa_3byte_t ioa2);

#endif /* LIBIEC104_IEC104_UTIL_H_ */
