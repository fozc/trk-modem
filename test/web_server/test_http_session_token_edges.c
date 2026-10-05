/*
 * test_http_session_token_edges.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Additional token contract pins beyond test_http_session_token.c:
 * one-way hex normalization, first-match-wins scanning, failure at
 * every entropy word and the exact termination rule.
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

void test_generate_writes_exact_length_with_terminator(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE] = {0};

    TEST_ASSERT_TRUE(http_session_token_generate(token));
    TEST_ASSERT_EQUAL_UINT(HTTP_SESSION_TOKEN_HEX_LEN, strlen(token));
    TEST_ASSERT_EQUAL_HEX8(0U, (uint8_t)token[HTTP_SESSION_TOKEN_HEX_LEN]);
}

void test_entropy_failure_at_any_word_clears_partial_secret(void)
{
    char token[HTTP_SESSION_TOKEN_SIZE];
    const char cleared[HTTP_SESSION_TOKEN_SIZE] = {0};

    for (fail_index = 0U; fail_index < 4U; fail_index++)
    {
        memset(token, 'X', sizeof(token));
        word_index = 0U;
        TEST_ASSERT_FALSE(http_session_token_generate(token));
        TEST_ASSERT_EQUAL_MEMORY(cleared, token, sizeof(token));
    }
}

void test_only_the_query_side_hex_is_normalized(void)
{
    /* Lowercase hex in the query matches an uppercase token, but a
     * non-canonical (lowercase) stored token never matches: the
     * comparison normalizes one direction only. */
    TEST_ASSERT_TRUE(http_session_token_matches(expected_token,
        "t=0123456789abcdef10203040fedcba98"));
    TEST_ASSERT_FALSE(http_session_token_matches(
        "0123456789abcdef10203040fedcba98",
        "t=0123456789ABCDEF10203040FEDCBA98"));
}

void test_first_t_parameter_wins_and_no_retry_scan_follows(void)
{
    /* A wrong first t= fails the match even when the correct value
     * appears again later in the query string. */
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=1123456789ABCDEF10203040FEDCBA98&"
        "t=0123456789ABCDEF10203040FEDCBA98"));
    /* Same rule for the &t= form. */
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "a=1&t=1123456789ABCDEF10203040FEDCBA98&"
        "t=0123456789ABCDEF10203040FEDCBA98"));
}

void test_lone_t_key_without_equals_is_rejected(void)
{
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, "t"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, "a=1&t"));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token, "at="));
}

void test_token_value_may_end_at_ampersand_or_string_end_only(void)
{
    /* '&' after the token is a legal continuation... */
    TEST_ASSERT_TRUE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA98&next=1"));
    /* ...but any other byte after the 32nd digit fails. */
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA98 "));
    TEST_ASSERT_FALSE(http_session_token_matches(expected_token,
        "t=0123456789ABCDEF10203040FEDCBA98;t=1"));
}

/*** end of file ***/
