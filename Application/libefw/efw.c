/* EFW wire parser. Structure validation does not authenticate an image. */
#include "efw.h"
#include <string.h>

int efw_identity_valid(const efw_identity_t *id)
{
    static const uint8_t zero[8] = {0};
    if (id == NULL || memcmp(id->magic, "FWID", 4U) != 0 ||
        id->format_version != 1U || id->auth_type != EFW_AUTH_TYPE_ECDSA_P256 ||
        le16toh(id->key_id) != 0U || id->reserved0 != 0U ||
        memcmp(id->reserved1, zero, sizeof(zero)) != 0)
        return -1;
    return 0;
}

int efw_parse_base(const void *data, size_t len, efw_t *out)
{
    if (data == NULL || out == NULL || len < EFW_BASE_HEADER_SIZE)
        return -1;
    efw_base_header_t h;
    memcpy(&h, data, sizeof(h));
    if (memcmp(h.magic, "*EFW", 4U) != 0 || h.file_version != EFW_FILE_VERSION ||
        h.reserved != 0U ||
        (h.compression_type != EFW_COMPRESSION_NONE &&
         h.compression_type != EFW_COMPRESSION_LZMA1 &&
         h.compression_type != EFW_COMPRESSION_LZMA1_ARMTHUMB) ||
        (h.encryption_type != EFW_ENCRYPTION_NONE &&
         h.encryption_type != EFW_ENCRYPTION_AES128_CTR) ||
        h.auth_type != EFW_AUTH_TYPE_ECDSA_P256)
        return -1;
    memset(out, 0, sizeof(*out));
    out->efw_file_version = h.file_version;
    out->device_type = h.device_type;
    out->device_model = h.device_model;
    out->file_type = h.file_type;
    out->app_version = h.app_version;
    out->app_size = le32toh(h.app_size);
    out->app_crc = le32toh(h.app_crc);
    out->stored_size = le32toh(h.stored_size);
    memcpy(out->short_commit_hash, h.short_commit_hash, 8U);
    out->day = h.day;
    out->month = h.month;
    out->year = le16toh(h.year);
    out->hour = h.hour;
    out->minute = h.minute;
    out->second = h.second;
    out->auth_type = h.auth_type;
    memcpy(out->signature.r, h.signature_r, 32U);
    memcpy(out->signature.s, h.signature_s, 32U);
    out->encryption_type = h.encryption_type;
    out->compression_type = h.compression_type;
    memcpy(out->iv, h.iv, 16U);
    memcpy(out->lzma_props, h.lzma_props, 5U);
    return 0;
}

int efw_parse(const void *data, size_t len, efw_t *out)
{
    if (len < EFW_HEADER_SIZE || efw_parse_base(data, len, out) != 0)
        return -1;
    memcpy(&out->identity, (const uint8_t *)data + EFW_BASE_HEADER_SIZE,
           sizeof(out->identity));
    const efw_identity_t *id = &out->identity;
    if (efw_identity_valid(id) != 0 ||
        id->device_type != out->device_type || id->device_model != out->device_model ||
        id->file_type != out->file_type || le32toh(id->app_size) != out->app_size ||
        memcmp(&id->app_version, &out->app_version, sizeof(id->app_version)) != 0)
        return -1;
    return 0;
}

uint32_t efw_get_header_size(void)
{
    return EFW_HEADER_SIZE;
}

int efw_is_newer_than(const efw_t *fw, uint8_t major, uint8_t minor,
                      uint8_t patch, uint8_t extra)
{
    if (fw->app_version.major != major) return fw->app_version.major > major;
    if (fw->app_version.minor != minor) return fw->app_version.minor > minor;
    if (fw->app_version.patch != patch) return fw->app_version.patch > patch;
    return fw->app_version.extra > extra;
}
