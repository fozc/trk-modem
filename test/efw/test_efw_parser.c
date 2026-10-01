/**
 * @file test_efw_parser.c
 * @brief EFW codec negotiation regression tests.
 */
#include "unity.h"
#include "efw.h"
#include <string.h>

static uint8_t header[EFW_HEADER_SIZE];

void setUp(void)
{
    memset(header, 0, sizeof(header));
    memcpy(header, "*EFW", 4U);
    header[4] = EFW_FILE_VERSION;
    header[39] = EFW_AUTH_TYPE_ECDSA_P256;
    memcpy(header + EFW_BASE_HEADER_SIZE, "FWID", 4U);
    header[132] = 1U;
    header[133] = EFW_AUTH_TYPE_ECDSA_P256;
}

void tearDown(void)
{
}

void test_efw_parser_accepts_legacy_and_thumb_codecs(void)
{
    efw_t result;
    const uint8_t codecs[] = {EFW_COMPRESSION_NONE,
        EFW_COMPRESSION_LZMA1, EFW_COMPRESSION_LZMA1_ARMTHUMB};
    for (size_t i = 0U; i < sizeof(codecs); i++)
    {
        header[EFW_COMPRESSION_TYPE_OFFSET] = codecs[i];
        TEST_ASSERT_EQUAL_INT(0, efw_parse(header, sizeof(header), &result));
        TEST_ASSERT_EQUAL_UINT8(codecs[i], result.compression_type);
    }
}

void test_efw_parser_rejects_unknown_codec_and_version(void)
{
    efw_t result;
    header[EFW_COMPRESSION_TYPE_OFFSET] = 0x7FU;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, sizeof(header), &result));
    header[EFW_COMPRESSION_TYPE_OFFSET] = EFW_COMPRESSION_LZMA1_ARMTHUMB;
    header[4] = 2U;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, sizeof(header), &result));
}

void test_efw_parser_rejects_every_truncated_header(void)
{
    efw_t result;
    for (size_t len = 0U; len < EFW_HEADER_SIZE; len++)
    {
        TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, len, &result));
    }
}

void test_efw_parser_rejects_identity_target_mismatch(void)
{
    efw_t result;
    header[136] = 1U;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, sizeof(header), &result));
}

void test_efw_parser_rejects_unknown_identity_key(void)
{
    efw_t result;
    header[134] = 1U;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, sizeof(header), &result));
}

void test_efw_base_parser_accepts_first_receive_block_only(void)
{
    efw_t result;
    TEST_ASSERT_EQUAL_INT(0, efw_parse_base(header, EFW_BASE_HEADER_SIZE, &result));
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, EFW_BASE_HEADER_SIZE, &result));
}

/*** end of file ***/