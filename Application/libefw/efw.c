/*
 * efw.c
 *
 *  Created on: Nov 3, 2022
 *      Author: fatih
 */
#include <string.h>
#include "efw.h"

#define EFW_MAGIC (uint32_t)0x2A454657 /* *EFW */

typedef struct __attribute__((packed))
{
	uint32_t magic;
	uint8_t efw_file_version;
	uint8_t file_type;
	uint8_t device_type;
	uint8_t device_model;
	efw_version_t app_version;
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
}efw_raw_fields_t;

typedef struct __attribute__((packed))
{
	efw_raw_fields_t fields;
	uint8_t reserve[EFW_HEADER_SIZE - sizeof(efw_raw_fields_t)];
}efw_raw_header_t;

_Static_assert(sizeof(efw_raw_header_t) == EFW_HEADER_SIZE,
	"efw_raw_header_t size mismatch: must be EFW_HEADER_SIZE bytes");

int efw_parse(const void *data, efw_t *out)
{
	if (!data || !out)
		return -1;

	const efw_raw_header_t *hdr = (const efw_raw_header_t *)data;

	if (hdr->fields.magic != htobe32(EFW_MAGIC))
		return -1;

	out->device_type  = hdr->fields.device_type;
	out->device_model = hdr->fields.device_model;
	out->file_type    = hdr->fields.file_type;
	out->app_version  = hdr->fields.app_version;
	out->app_size     = le32toh(hdr->fields.app_size);
	out->app_crc      = le32toh(hdr->fields.app_crc);
	memcpy(out->hmac, hdr->fields.hmac, 32);
	memcpy(out->short_commit_hash, hdr->fields.short_commit_hash, 8);
	out->day    = hdr->fields.day;
	out->month  = hdr->fields.month;
	out->year   = le16toh(hdr->fields.year);
	out->hour   = hdr->fields.hour;
	out->minute = hdr->fields.minute;
	out->second = hdr->fields.second;

	return 0;
}

uint32_t efw_get_header_size(void)
{
	return EFW_HEADER_SIZE;
}

int efw_is_newer_than(const efw_t *fw, uint8_t major, uint8_t minor, uint8_t patch, uint8_t extra)
{
	if (fw->app_version.major != major) return fw->app_version.major > major;
	if (fw->app_version.minor != minor) return fw->app_version.minor > minor;
	if (fw->app_version.patch != patch) return fw->app_version.patch > patch;
	return fw->app_version.extra > extra;
}
