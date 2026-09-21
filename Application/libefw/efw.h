/*
 * efw.h
 *
 *  Created on: Nov 3, 2022
 *      Author: fatih
 */

#ifndef EFW_H_
#define EFW_H_

#include <stdint.h>

#define EFW_LIB_VERSION 0x02
#define EFW_HEADER_SIZE 128

/* EFW file version identifier */
#define EFW_FILE_VERSION     0x02U  /* v2: compressed payload, stored_size field */

/* Signature field offsets in the packed EFW header (efw_raw_fields_t).
 * Used to zero the r/s fields before hashing the header as part of
 * the combined signature.  Must match bin2efw.py. */
#define EFW_SIG_R_OFFSET  20U
#define EFW_SIG_S_OFFSET  68U

/* v2 payload field offsets in the packed header (v1 reserve area).
 * Must match bin2efw.py. */
#define EFW_COMPRESSION_TYPE_OFFSET 117U
#define EFW_STORED_SIZE_OFFSET      118U
#define EFW_LZMA_PROPS_OFFSET       122U
#define EFW_LZMA_PROPS_SIZE         5U

/* Authentication type code */
#define EFW_AUTH_TYPE_ECDSA_P256  0x02U

/* ECDSA signature component size (P-256 = 32 bytes per component) */
#define EFW_ECDSA_SIG_SIZE  32U

/* Encryption type codes */
#define EFW_ENCRYPTION_NONE          0x00U
#define EFW_ENCRYPTION_AES128_CTR    0x01U

/* Compression type codes (v2) */
#define EFW_COMPRESSION_NONE         0x00U
#define EFW_COMPRESSION_LZMA1        0x01U

/* AES-128 IV size in bytes */
#define EFW_AES_IV_SIZE  16U

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
	uint8_t efw_file_version;
	uint32_t app_size;
	uint32_t app_crc;
	uint8_t short_commit_hash[8];
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;

	/* Authentication: ECDSA-P256 signature */
	uint8_t auth_type;
	struct
	{
		uint8_t r[EFW_ECDSA_SIG_SIZE];
		uint8_t s[EFW_ECDSA_SIG_SIZE];
	} signature;

	/* Encryption */
	uint8_t encryption_type;
	uint8_t iv[EFW_AES_IV_SIZE];

	/* v2 payload description:
	 * app_size/app_crc always describe the RAW (expanded) firmware;
	 * stored_size is the byte count kept in SPI after the header. */
	uint8_t  compression_type;
	uint32_t stored_size;
	uint8_t  lzma_props[EFW_LZMA_PROPS_SIZE];
} efw_t;

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
