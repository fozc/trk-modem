/*
 * test_http_session_token.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Entropy failure and token authentication boundary tests.
 */
#include "unity.h"
#include "http_session_token.h"
#include "mock_bsp_random.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char expected_token[] = "0123456789ABCDEF10203040FEDCBA98";
static uint32_t words[4];
static size_t word_index;
static size_t fail_index;

static bool read_word(uint32_t *value, int call_count)
{
    (void)call_count;
    if ((word_index == fail_index) || (4U <= word_index))
    {
        return false;
    }
    *value = words[word_index];
    word_index++;
    return true;
}

void setUp(void)
{
    words[0] = 0x01234567U;
    words[1] = 0x89ABCDEFU;
    words[2] = 0x10203040U;
    words[3] = 0xFEDCBA98U;
    word_index = 0U;
    fail_index = 4U;
    bsp_random_word_StubWithCallback(read_word);
}

void tearDown(void)
{
}

void test_session_token_uses_all_four_entropy_words(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE];

    TEST_ASSERT_TRUE(http_session_token_generate(token));
    TEST_ASSERT_EQUAL_STRING(expected_token, token);
    TEST_ASSERT_EQUAL_UINT32(4U, word_index);
}

void test_session_token_entropy_failure_clears_partial_secret(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE];
    const char cleared[HTTP_SESSION_TOKEN_SIZE] = {0};

    fail_index = 2U;
    memset(token, 'X', sizeof(token));
    TEST_ASSERT_FALSE(http_session_token_generate(token));
    TEST_ASSERT_EQUAL_MEMORY(cleared, token, sizeof(token));
}

void test_session_token_rejects_zero_entropy_without_fallback(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE];

    memset(words, 0, sizeof(words));
    TEST_ASSERT_FALSE(http_session_token_generate(token));
    TEST_ASSERT_EQUAL_STRING("", token);
}

void test_session_token_rejects_null_output(void)
{
    TEST_ASSERT_FALSE(http_session_token_generate(NULL));
}

void test_session_token_accepts_exact_query_parameter(void)
{
    TEST_ASSERT_TRUE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA98"));
    TEST_ASSERT_TRUE(http_session_token_matches(expected_token,
        "offset=1&t=0123456789ABCDEF10203040FEDCBA98&cs=2"));
}

void test_session_token_accepts_lowercase_hex(void)
{
    TEST_ASSERT_TRUE(http_session_token_matches(expected_token,
        "t=0123456789abcdef10203040fedcba98"));
}

void test_session_token_rejects_short_and_long_values(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=01234567"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA9"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA980"));
}

void test_session_token_rejects_missing_and_empty_values(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, NULL));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, ""));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, "t="));
    TEST_ASSERT_FALSE(http_session_token_matches("", "t="));
    TEST_ASSERT_FALSE(http_session_token_matches(NULL, "t="));
}

void test_session_token_rejects_substring_parameter_keys(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "not=0123456789ABCDEF10203040FEDCBA98"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "x=1&at=0123456789ABCDEF10203040FEDCBA98"));
}

void test_session_token_rejects_nonhex_and_encoded_suffix(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA9G"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA98%00"));
}

void test_session_token_rejects_mismatch_at_either_end(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=1123456789ABCDEF10203040FEDCBA98"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA99"));
}

void test_session_token_changes_when_entropy_changes(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE];

    words[3] ^= 1U;
    TEST_ASSERT_TRUE(http_session_token_generate(token));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(expected_token, token));
}

/*** end of file ***/
