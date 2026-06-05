/*
 * crc32.h
 *
 *  Created on: Apr 28, 2026
 *      Author: fatih.ozcan
 */

#ifndef LIBS_CRC32_H_
#define LIBS_CRC32_H_

/**
 * \file
 * Functions and types for CRC-32 checks.
 *
 * Generated on Fri Aug 15 14:18:37 2025
 * by pycrc v0.10.0, https://pycrc.org
 * using the configuration:
 *  - Width         = 32
 *  - Poly          = 0x04c11db7
 *  - XorIn         = 0xffffffff
 *  - ReflectIn     = True
 *  - XorOut        = 0xffffffff
 *  - ReflectOut    = True
 *  - Algorithm     = table-driven
 */

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC_ALGO_TABLE_DRIVEN 1

typedef uint_fast32_t crc32_t;

crc32_t crc32_reflect(crc32_t data, size_t data_len);

static inline crc32_t crc32_init(void)
{
    return 0xffffffff;
}

crc32_t crc32_update(crc32_t crc, const void *data, size_t data_len);

static inline crc32_t crc32_finalize(crc32_t crc)
{
    return crc ^ 0xffffffff;
}

#ifdef __cplusplus
}
#endif

#endif /* LIBS_CRC32_H_ */
