/*
 * efw.h
 *
 *  Created on: Nov 3, 2022
 *      Author: fatih
 */

#ifndef EFW_H_
#define EFW_H_

#include <stdint.h>

#define EFW_LIB_VERSION 0x01
#define EFW_HEADER_SIZE 128

/* GCC packed attribute */
#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef struct __packed
{
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t extra;
}efw_version_t;

typedef struct
{
	efw_version_t app_version;
	uint8_t device_type;
	uint8_t device_model;
	uint8_t file_type;
	uint32_t app_size;
	uint32_t app_crc;
	uint8_t hmac[32];
	uint8_t short_commit_hash[8];
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
}efw_t;

int efw_parse(const void *data, efw_t *out);
uint32_t efw_get_header_size(void);
int efw_is_newer_than(const efw_t *fw, uint8_t major, uint8_t minor, uint8_t patch, uint8_t extra);


/* -------------------------------------------------------------------------
 * Endian detection & byte-swap macros
 * ---------------------------------------------------------------------- */

#ifndef __BYTE_ORDER

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __PDP_ENDIAN    3412

#if defined(__GNUC__) && defined(__BYTE_ORDER__)
#  define __BYTE_ORDER __BYTE_ORDER__
#elif defined(__LITTLE_ENDIAN__) || defined(__LIT)
#  define __BYTE_ORDER __LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN__) || defined(__BIG)
#  define __BYTE_ORDER __BIG_ENDIAN
#else
#  error Unknown byte order
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN    __BIG_ENDIAN
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#endif
#ifndef PDP_ENDIAN
#define PDP_ENDIAN    __PDP_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER    __BYTE_ORDER
#endif

#ifndef __bswap16
#define __bswap16(x) ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#endif
#ifndef __bswap32
#define __bswap32(x) ((__bswap16(x) + 0UL) << 16 | __bswap16((x) >> 16))
#endif
#ifndef __bswap64
#define __bswap64(x) ((__bswap32(x) + 0ULL) << 32 | __bswap32((x) >> 32))
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe16(x)  __bswap16(x)
#define be16toh(x)  __bswap16(x)
#define htobe32(x)  __bswap32(x)
#define be32toh(x)  __bswap32(x)
#define htobe64(x)  __bswap64(x)
#define be64toh(x)  __bswap64(x)
#define htole16(x)  (uint16_t)(x)
#define le16toh(x)  (uint16_t)(x)
#define htole32(x)  (uint32_t)(x)
#define le32toh(x)  (uint32_t)(x)
#define htole64(x)  (uint64_t)(x)
#define le64toh(x)  (uint64_t)(x)
#else
#define htobe16(x)  (uint16_t)(x)
#define be16toh(x)  (uint16_t)(x)
#define htobe32(x)  (uint32_t)(x)
#define be32toh(x)  (uint32_t)(x)
#define htobe64(x)  (uint64_t)(x)
#define be64toh(x)  (uint64_t)(x)
#define htole16(x)  __bswap16(x)
#define le16toh(x)  __bswap16(x)
#define htole32(x)  __bswap32(x)
#define le32toh(x)  __bswap32(x)
#define htole64(x)  __bswap64(x)
#define le64toh(x)  __bswap64(x)
#endif

#endif /* __BYTE_ORDER */

/* -------------------------------------------------------------------------
 * Project-wide type definitions
 * ---------------------------------------------------------------------- */

/* File type descriptors (upper 3 bits of file_type byte) */
#define EFW_FILE_TYPE_CALIBRATION   (0 << 5)
#define EFW_FILE_TYPE_CONFIG        (1 << 5)
#define EFW_FILE_TYPE_BOOTLOADER    (2 << 5)
#define EFW_FILE_TYPE_APPLICATION   (3 << 5)


#endif /* EFW_H_ */
