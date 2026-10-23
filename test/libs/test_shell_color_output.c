/*
 * test_shell_color_output.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#undef NO_SHELL_LOG
#undef DISABLE_SHELL_LOG
#include "mock_shell.h"
#include "web_shell.h"
TEST_SOURCE_FILE("json.c")
#include "xprintf.h"
#include <string.h>

static char output[128];
static size_t output_length;
static shell_putchar_fn_t sink;
static void capture_char(int chr)
{
    TEST_ASSERT_TRUE(output_length + 1U < sizeof(output));
    output[output_length++] = (char)chr;
    output[output_length] = '\0';
}
static void shell_char(int chr, int call_count)
{
    (void)call_count;
    sink(chr);
}
void setUp(void)
{
    output_length = 0U;
    output[0] = '\0';
    sink = capture_char;
    shell_putchr_Stub(shell_char);
}
void tearDown(void)
{
}
void test_shell_color_reaches_shell_writer_and_resets_color(void)
{
    SHELL_CLOG(XCOLOR_RED, "bad %c", 'A');
    TEST_ASSERT_EQUAL_STRING("\033[31mbad A\033[0m", output);
}
void test_shell_null_color_preserves_plain_output(void)
{
    SHELL_CLOG(NULL, "plain %u", 7U);
    TEST_ASSERT_EQUAL_STRING("plain 7", output);
}
void test_web_shell_filters_color_sequences_and_preserves_text(void)
{
    char buffer[128];
    sink = web_shell_putchar;
    json_writer_t writer;
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, buffer, sizeof(buffer) - 1U));
    web_shell_capture_begin(&writer);
    SHELL_CLOG(XCOLOR_RED, "bad %c\"\\\n", 'A');
    TEST_ASSERT_EQUAL_INT(15, web_shell_capture_end());
    buffer[writer.used] = '\0';
    TEST_ASSERT_EQUAL_STRING("\"bad A\\\"\\\\\\u000a\"", buffer);
}
void test_web_shell_escape_state_is_reset_for_each_capture(void)
{
    char buffer[128];
    json_writer_t writer;
    sink = web_shell_putchar;
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, buffer, sizeof(buffer) - 1U));
    web_shell_capture_begin(&writer);
    web_shell_putchar(0x1B);
    TEST_ASSERT_EQUAL_INT(0, web_shell_capture_end());
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, buffer, sizeof(buffer) - 1U));
    web_shell_capture_begin(&writer);
    SHELL_LOG("[%c]", 'B');
    TEST_ASSERT_EQUAL_INT(3, web_shell_capture_end());
    buffer[writer.used] = '\0';
    TEST_ASSERT_EQUAL_STRING("\"[B]\"", buffer);
}
void test_web_shell_color_does_not_consume_text_capacity(void)
{
    char buffer[64];
    json_writer_t writer;
    sink = web_shell_putchar;
    TEST_ASSERT_EQUAL(JSON_OK,
                      json_writer_init(&writer, buffer, sizeof(buffer) - 1U));
    web_shell_capture_begin(&writer);
    SHELL_CLOG(XCOLOR_CYAN, "hello");
    TEST_ASSERT_EQUAL_INT(5, web_shell_capture_end());
    buffer[writer.used] = '\0';
    TEST_ASSERT_EQUAL_STRING("\"hello\"", buffer);
}
/*** end of file ***/
