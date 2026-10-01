/*
 * efw.h
 *
 *  Created on: Nov 3, 2022
 *      Author: fatih
 */

#ifndef EFW_H_
#define EFW_H_

#include <stdint.h>
#include <stddef.h>

#define EFW_LIB_VERSION 0x01
#define EFW_BASE_HEADER_SIZE 128U
#define EFW_HEADER_SIZE 256U

/* EFW file version identifier */
#define EFW_FILE_VERSION     0x01U

/* Signature field offsets in the packed EFW base header (efw_base_header_t).
 * Used to zero the r/s fields before hashing the header as part of
 * the combined signature.  Must match bin2efw.py. */
#define EFW_SIG_R_OFFSET  64U
#define EFW_SIG_S_OFFSET  96U

/* Payload field offsets in the packed base header.
 * Must match bin2efw.py. */
#define EFW_COMPRESSION_TYPE_OFFSET 41U
#define EFW_STORED_SIZE_OFFSET      20U
#define EFW_LZMA_PROPS_OFFSET       42U
#define EFW_LZMA_PROPS_SIZE         5U

/* Authentication type code */
#define EFW_AUTH_TYPE_ECDSA_P256  0x02U

/* ECDSA signature component size (P-256 = 32 bytes per component) */
#define EFW_ECDSA_SIG_SIZE  32U

/* Encryption type codes */
#define EFW_ENCRYPTION_NONE          0x00U
#define EFW_ENCRYPTION_AES128_CTR    0x01U

/* Compression type codes */
#define EFW_COMPRESSION_NONE         0x00U
#define EFW_COMPRESSION_LZMA1        0x01U
#define EFW_COMPRESSION_LZMA1_ARMTHUMB 0x02U /* file-relative PC=0 */

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

/* Wire integers are little-endian; ECDSA r/s are big-endian. */
typedef struct __packed
{
    uint8_t magic[4];
    uint8_t format_version;
    uint8_t auth_type;
    uint16_t key_id;
    uint8_t device_type;
    uint8_t device_model;
    uint8_t file_type;
    uint8_t reserved0;
    uint32_t load_address;
    uint32_t app_size;
    efw_version_t app_version;
    uint8_t app_sha256[32];
    uint8_t reserved1[8];
    uint8_t signature_r[32];
    uint8_t signature_s[32];
} efw_identity_t;

typedef struct __packed
{
    uint8_t magic[4];
    uint8_t file_version;
    uint8_t file_type;
    uint8_t device_type;
    uint8_t device_model;
    efw_version_t app_version;
    uint32_t app_size;
    uint32_t app_crc;
    uint32_t stored_size;
    uint8_t short_commit_hash[8];
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t auth_type;
    uint8_t encryption_type;
    uint8_t compression_type;
    uint8_t lzma_props[5];
    uint8_t reserved;
    uint8_t iv[16];
    uint8_t signature_r[32];
    uint8_t signature_s[32];
} efw_base_header_t;

typedef struct __packed
{
    efw_base_header_t base;
    efw_identity_t identity;
} efw_header_t;

#define EFW_IDENTITY_DOMAIN "TROIKA-FW-ID-V1"
#define EFW_PACKAGE_DOMAIN "TROIKA-EFW-V1"
#define EFW_IDENTITY_SIGNED_SIZE 64U
_Static_assert(sizeof(efw_identity_t) == 128U, "Identity size");
_Static_assert(sizeof(efw_base_header_t) == 128U, "Base header size");
_Static_assert(sizeof(efw_header_t) == 256U, "Header size");
_Static_assert(offsetof(efw_identity_t, signature_r) == 64U, "Identity R");
_Static_assert(offsetof(efw_identity_t, signature_s) == 96U, "Identity S");
_Static_assert(offsetof(efw_identity_t, app_sha256) == 24U, "Identity hash");
_Static_assert(offsetof(efw_base_header_t, signature_r) == EFW_SIG_R_OFFSET, "R");
_Static_assert(offsetof(efw_base_header_t, signature_s) == EFW_SIG_S_OFFSET, "S");
_Static_assert(offsetof(efw_base_header_t, compression_type) == EFW_COMPRESSION_TYPE_OFFSET, "Codec");
_Static_assert(offsetof(efw_base_header_t, stored_size) == EFW_STORED_SIZE_OFFSET, "Stored");
_Static_assert(offsetof(efw_base_header_t, lzma_props) == EFW_LZMA_PROPS_OFFSET, "Props");

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

	/* Payload description:
	 * app_size/app_crc always describe the RAW (expanded) firmware;
	 * stored_size is the byte count kept in SPI after the header. */
	uint8_t  compression_type;
	uint32_t stored_size;
	uint8_t  lzma_props[EFW_LZMA_PROPS_SIZE];
    efw_identity_t identity;
} efw_t;

/* Base-only parsing is a receive bound gate, never authentication. */
int efw_parse_base(const void *data, size_t len, efw_t *out);
int efw_parse(const void *data, size_t len, efw_t *out);
int efw_identity_valid(const efw_identity_t *identity);
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
