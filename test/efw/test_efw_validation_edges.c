/*
 * test_efw_validation_edges.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise every structural identity gate and version ordering decision.
 */

#include "unity.h"
#include "efw.h"
#include <string.h>

static efw_header_t header;

void setUp(void)
{
    (void)memset(&header, 0, sizeof(header));
    (void)memcpy(header.base.magic, "*EFW", 4U);
    header.base.file_version = EFW_FILE_VERSION;
    header.base.auth_type = EFW_AUTH_TYPE_ECDSA_P256;
    (void)memcpy(header.identity.magic, "FWID", 4U);
    header.identity.format_version = 1U;
    header.identity.auth_type = EFW_AUTH_TYPE_ECDSA_P256;
}

void tearDown(void)
{
}

void test_base_null_and_every_short_length_preserve_output(void)
{
    efw_t out;
    (void)memset(&out, 0xA5, sizeof(out));
    const efw_t before = out;

    TEST_ASSERT_EQUAL_INT(-1, efw_parse_base(NULL, sizeof(header), &out));
    TEST_ASSERT_EQUAL_INT(-1, efw_parse_base(&header, sizeof(header), NULL));
    TEST_ASSERT_EQUAL_INT(-1, efw_parse(NULL, sizeof(header), &out));
    TEST_ASSERT_EQUAL_INT(-1, efw_parse(&header, sizeof(header), NULL));
    for (size_t length = 0U; EFW_BASE_HEADER_SIZE > length; length++)
    {
        TEST_ASSERT_EQUAL_INT(-1, efw_parse_base(&header, length, &out));
        TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
    }
}

void test_base_invalid_fields_preserve_output_and_valid_retry_recovers(void)
{
    const size_t offsets[] =
    {
        offsetof(efw_base_header_t, magic),
        offsetof(efw_base_header_t, file_version),
        offsetof(efw_base_header_t, reserved),
        offsetof(efw_base_header_t, compression_type),
        offsetof(efw_base_header_t, encryption_type),
        offsetof(efw_base_header_t, auth_type)
    };
    const efw_header_t valid = header;
    efw_t out;

    for (size_t index = 0U; sizeof(offsets) / sizeof(offsets[0]) > index;
         index++)
    {
        header = valid;
        ((uint8_t *)&header.base)[offsets[index]] = 0x7FU;
        (void)memset(&out, 0xA5, sizeof(out));
        const efw_t before = out;
        TEST_ASSERT_EQUAL_INT(-1,
            efw_parse_base(&header, sizeof(header), &out));
        TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
        header = valid;
        TEST_ASSERT_EQUAL_INT(0, efw_parse(&header, sizeof(header), &out));
    }
}

void test_identity_rejects_every_reserved_byte_and_header_gate(void)
{
    const size_t offsets[] =
    {
        offsetof(efw_identity_t, magic),
        offsetof(efw_identity_t, format_version),
        offsetof(efw_identity_t, auth_type),
        offsetof(efw_identity_t, key_id),
        offsetof(efw_identity_t, key_id) + 1U,
        offsetof(efw_identity_t, reserved0)
    };
    const efw_identity_t valid = header.identity;
    TEST_ASSERT_EQUAL_INT(-1, efw_identity_valid(NULL));
    for (size_t index = 0U; sizeof(offsets) / sizeof(offsets[0]) > index;
         index++)
    {
        header.identity = valid;
        ((uint8_t *)&header.identity)[offsets[index]] = 0x7FU;
        TEST_ASSERT_EQUAL_INT(-1, efw_identity_valid(&header.identity));
    }
    for (size_t index = 0U; sizeof(header.identity.reserved1) > index; index++)
    {
        header.identity = valid;
        header.identity.reserved1[index] = 1U;
        TEST_ASSERT_EQUAL_INT(-1, efw_identity_valid(&header.identity));
    }
    header.identity = valid;
    TEST_ASSERT_EQUAL_INT(0, efw_identity_valid(&header.identity));
}

void test_identity_must_match_every_base_target_size_and_version_byte(void)
{
    const size_t offsets[] =
    {
        offsetof(efw_identity_t, device_type),
        offsetof(efw_identity_t, device_model),
        offsetof(efw_identity_t, file_type),
        offsetof(efw_identity_t, app_size),
        offsetof(efw_identity_t, app_size) + 1U,
        offsetof(efw_identity_t, app_size) + 2U,
        offsetof(efw_identity_t, app_size) + 3U,
        offsetof(efw_identity_t, app_version),
        offsetof(efw_identity_t, app_version) + 1U,
        offsetof(efw_identity_t, app_version) + 2U,
        offsetof(efw_identity_t, app_version) + 3U
    };
    const efw_identity_t valid = header.identity;
    efw_t out;

    for (size_t index = 0U; sizeof(offsets) / sizeof(offsets[0]) > index;
         index++)
    {
        header.identity = valid;
        ((uint8_t *)&header.identity)[offsets[index]] = 1U;
        TEST_ASSERT_EQUAL_INT(-1, efw_parse(&header, sizeof(header), &out));
    }
    header.identity = valid;
    TEST_ASSERT_EQUAL_INT(0, efw_parse(&header, sizeof(header), &out));
}

void test_base_decodes_little_endian_fields_arrays_and_aes_codec(void)
{
    efw_t out;
    header.base.app_size = htole32(0x12345678U);
    header.identity.app_size = header.base.app_size;
    header.base.app_crc = htole32(0x89ABCDEFU);
    header.base.stored_size = htole32(0x87654321U);
    header.base.year = htole16(2026U);
    header.base.encryption_type = EFW_ENCRYPTION_AES128_CTR;
    (void)memset(header.base.iv, 0x12, sizeof(header.base.iv));
    (void)memset(header.base.signature_r, 0x34,
                 sizeof(header.base.signature_r));
    (void)memset(header.base.signature_s, 0x56,
                 sizeof(header.base.signature_s));
    (void)memset(header.base.lzma_props, 0x78,
                 sizeof(header.base.lzma_props));
    TEST_ASSERT_EQUAL_INT(0, efw_parse(&header, sizeof(header), &out));
    TEST_ASSERT_EQUAL_UINT32(0x12345678U, out.app_size);
    TEST_ASSERT_EQUAL_UINT32(0x89ABCDEFU, out.app_crc);
    TEST_ASSERT_EQUAL_UINT32(0x87654321U, out.stored_size);
    TEST_ASSERT_EQUAL_UINT16(2026U, out.year);
    TEST_ASSERT_EQUAL_UINT8(EFW_ENCRYPTION_AES128_CTR, out.encryption_type);
    TEST_ASSERT_EQUAL_MEMORY(header.base.iv, out.iv, sizeof(out.iv));
    TEST_ASSERT_EQUAL_MEMORY(header.base.signature_r, out.signature.r, 32U);
    TEST_ASSERT_EQUAL_MEMORY(header.base.signature_s, out.signature.s, 32U);
    TEST_ASSERT_EQUAL_MEMORY(header.base.lzma_props, out.lzma_props, 5U);
    TEST_ASSERT_EQUAL_MEMORY(&header.identity, &out.identity,
                             sizeof(out.identity));
    TEST_ASSERT_EQUAL_UINT32(EFW_HEADER_SIZE, efw_get_header_size());
}

void test_version_order_uses_first_difference_and_rejects_equal(void)
{
    efw_t fw = {.app_version = {2U, 3U, 4U, 5U}};
    TEST_ASSERT_FALSE(efw_is_newer_than(&fw, 2U, 3U, 4U, 5U));
    TEST_ASSERT_TRUE(efw_is_newer_than(&fw, 1U, 255U, 255U, 255U));
    TEST_ASSERT_FALSE(efw_is_newer_than(&fw, 3U, 0U, 0U, 0U));
    TEST_ASSERT_TRUE(efw_is_newer_than(&fw, 2U, 2U, 255U, 255U));
    TEST_ASSERT_FALSE(efw_is_newer_than(&fw, 2U, 4U, 0U, 0U));
    TEST_ASSERT_TRUE(efw_is_newer_than(&fw, 2U, 3U, 3U, 255U));
    TEST_ASSERT_FALSE(efw_is_newer_than(&fw, 2U, 3U, 5U, 0U));
    TEST_ASSERT_TRUE(efw_is_newer_than(&fw, 2U, 3U, 4U, 4U));
    TEST_ASSERT_FALSE(efw_is_newer_than(&fw, 2U, 3U, 4U, 6U));
}

/*** end of file ***/
