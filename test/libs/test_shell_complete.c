/*
 * test_shell_complete.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "shell_complete.h"
#include <string.h>

static char output[128];
static size_t output_length;

static const shell_cmd_t command_table[] = {
    { "help",   "help",   0U, NULL },
    { "status", "status", 0U, NULL },
    { "set",    "set",    1U, NULL },
    { "rf",     "rf",     0U, NULL },
};
static uint8_t command_count = 4U;

static void capture_char(int chr)
{
    TEST_ASSERT_TRUE(output_length + 1U < sizeof(output));
    output[output_length++] = (char)chr;
    output[output_length] = '\0';
}

/* Copy the typed text into a zeroed buffer, run one TAB press on it and
 * return the resulting input length. */
static uint32_t run_tab(const char *typed, uint8_t *buffer, uint32_t max_len)
{
    uint32_t length = (uint32_t)strlen(typed);

    memset(buffer, 0, max_len);
    memcpy(buffer, typed, length);
    shell_tab_complete(buffer, &length, max_len);
    return length;
}

void setUp(void)
{
    output_length = 0U;
    output[0] = '\0';
    shell_complete_init(command_table, &command_count, capture_char);
    shell_complete_reset();
}

void tearDown(void)
{
}

void test_not_initialized_call_is_ignored(void)
{
    uint8_t buffer[8];
    uint32_t length = 2U;

    shell_complete_init(NULL, NULL, NULL);
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = 'a';
    buffer[1] = 'b';
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(2U, length);
    TEST_ASSERT_EQUAL_UINT8('a', buffer[0]);
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_empty_input_resets_without_output(void)
{
    uint8_t buffer[8];
    uint32_t length = 0U;

    memset(buffer, 0, sizeof(buffer));
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_word_with_space_is_ignored(void)
{
    uint8_t buffer[16];
    const uint32_t length = run_tab("help x", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(6U, length);
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_EQUAL_UINT8('h', buffer[0]);
}

void test_unknown_prefix_rings_bell_only(void)
{
    uint8_t buffer[8];
    const uint32_t length = run_tab("zz", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(2U, length);
    TEST_ASSERT_EQUAL_UINT8('z', buffer[0]);
    TEST_ASSERT_EQUAL_STRING("\a", output);
}

void test_unique_prefix_completes_word_and_space(void)
{
    uint8_t buffer[16];
    const uint32_t length = run_tab("hel", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(5U, length);
    TEST_ASSERT_EQUAL_MEMORY("help", buffer, 4U);
    TEST_ASSERT_EQUAL_UINT8(' ', buffer[4]);
    TEST_ASSERT_EQUAL_STRING("p ", output);
}

void test_fully_typed_command_gets_only_space(void)
{
    uint8_t buffer[16];
    const uint32_t length = run_tab("help", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(5U, length);
    TEST_ASSERT_EQUAL_MEMORY("help", buffer, 4U);
    TEST_ASSERT_EQUAL_UINT8(' ', buffer[4]);
    TEST_ASSERT_EQUAL_STRING(" ", output);
}

void test_multiple_matches_select_first_on_first_tab(void)
{
    uint8_t buffer[16];
    const uint32_t length = run_tab("s", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(6U, length);
    TEST_ASSERT_EQUAL_STRING("status", (const char *)buffer);
    TEST_ASSERT_EQUAL_STRING("\033[1D\033[Kstatus", output);
}

void test_repeated_tab_cycles_through_matches_and_wraps(void)
{
    uint8_t buffer[16];
    uint32_t length = run_tab("s", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(6U, length);

    /* Second TAB: same prefix, cycle to the second match. */
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT32(3U, length);
    TEST_ASSERT_EQUAL_STRING("set", (const char *)buffer);

    /* Third TAB: wrap back to the first match. */
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT32(6U, length);
    TEST_ASSERT_EQUAL_STRING("status", (const char *)buffer);
}

void test_changed_prefix_starts_new_search(void)
{
    uint8_t buffer[16];
    uint32_t length = run_tab("s", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(6U, length);

    length = run_tab("hel", buffer, (uint32_t)sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT32(5U, length);
    TEST_ASSERT_EQUAL_MEMORY("help", buffer, 4U);
    /* Unique-match path echoes only the completion, no VT100 clear. */
    TEST_ASSERT_EQUAL_STRING("p ", output + strlen("\033[1D\033[Kstatus"));
}

void test_reset_clears_cycle_position(void)
{
    uint8_t buffer[16];
    uint32_t length = run_tab("s", buffer, (uint32_t)sizeof(buffer));

    TEST_ASSERT_EQUAL_UINT32(6U, length);
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("set", (const char *)buffer);

    shell_complete_reset();
    length = 1U;
    buffer[1] = '\0';
    shell_tab_complete(buffer, &length, (uint32_t)sizeof(buffer));

    /* After reset the search restarts: first match again, not "set". */
    TEST_ASSERT_EQUAL_UINT32(6U, length);
    TEST_ASSERT_EQUAL_STRING("status", (const char *)buffer);
}

void test_completion_clamps_to_max_length(void)
{
    uint8_t buffer[8];
    const uint32_t length = run_tab("hel", buffer, 5U);

    TEST_ASSERT_EQUAL_UINT32(4U, length);
    TEST_ASSERT_EQUAL_MEMORY("help", buffer, 4U);
    /* No room for the trailing space inside max_len = 5. */
    TEST_ASSERT_EQUAL_UINT8('\0', buffer[4]);
    TEST_ASSERT_EQUAL_STRING("p", output);
}
