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
        TEST_ASSERT_EQUAL_INT(0, efw_parse(header, &result));
        TEST_ASSERT_EQUAL_UINT8(codecs[i], result.compression_type);
    }
}

void test_efw_parser_rejects_unknown_codec_and_version(void)
{
    efw_t result;
    header[EFW_COMPRESSION_TYPE_OFFSET] = 0x7FU;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, &result));
    header[EFW_COMPRESSION_TYPE_OFFSET] = EFW_COMPRESSION_LZMA1_ARMTHUMB;
    header[4] = 1U;
    TEST_ASSERT_NOT_EQUAL(0, efw_parse(header, &result));
}

/*** end of file ***/