/*
 * test_gsm_sms.c
 *
 *  Created on: Oct 24, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify received SMS parsing, callback logging and rejected responses.
 */

#include "unity.h"
#include "gsm_types.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char log_output[1024];
static uint32_t log_calls;

static void capture_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(log_output, sizeof(log_output),
                                   format, args);
    va_end(args);
    TEST_ASSERT_TRUE(written >= 0);
    TEST_ASSERT_TRUE((size_t)written < sizeof(log_output));
    log_calls++;
}

#undef CSLOG
#define CSLOG(...) capture_log(__VA_ARGS__)
#include "../../Application/gsm/gsm_sms.c"

void setUp(void)
{
    memset(log_output, 0, sizeof(log_output));
    log_calls = 0U;
}

void tearDown(void)
{
}

void test_received_sms_logs_sender_and_multiline_message(void)
{
    static const uint8_t response[] =
        "\r\n+CMGR: \"REC UNREAD\",\"+905551234567\",\"\",\"26/10/24\"\r\n"
        "first line\r\nsecond line\r\n\r\nOK\r\n";
    bool unread = false;
    TEST_ASSERT_TRUE(gsm_sms_handle_response(response,
                                            sizeof(response) - 1U, &unread));
    TEST_ASSERT_TRUE(unread);
    TEST_ASSERT_EQUAL_UINT32(1U, log_calls);
    TEST_ASSERT_NOT_NULL(strstr(log_output, "+905551234567"));
    TEST_ASSERT_NOT_NULL(strstr(log_output, "first line\r\nsecond line"));
}

void test_read_sms_and_exact_nonterminated_response_are_supported(void)
{
    static const uint8_t response[] =
        "+CMGR: \"REC READ\",\"SENDER\",\"\",\"date\"\r\nhello\r\nOK\r\n";
    uint8_t exact[sizeof(response) - 1U];
    memcpy(exact, response, sizeof(exact));
    bool unread = true;
    TEST_ASSERT_TRUE(gsm_sms_handle_response(exact, sizeof(exact), &unread));
    TEST_ASSERT_FALSE(unread);
    TEST_ASSERT_NOT_NULL(strstr(log_output, "SENDER: hello"));
}

void test_invalid_and_sent_messages_do_not_call_received_callback(void)
{
    const char *invalid[] = {
        "\r\nOK\r\n", "\r\n+CMS ERROR: 321\r\n",
        "+CMGR: \"REC UNREAD\"\r\nhello\r\nOK\r\n",
        "+CMGR: \"REC UNREAD\",\"sender\"\r\nhello",
        "+CMGR: \"STO SENT\",\"sender\"\r\nhello\r\nOK\r\n"
    };
    bool unread = true;
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         index++)
    {
        TEST_ASSERT_FALSE(gsm_sms_handle_response(
            (const uint8_t *)invalid[index], strlen(invalid[index]), &unread));
        TEST_ASSERT_TRUE(unread);
    }
    TEST_ASSERT_FALSE(gsm_sms_handle_response(NULL, 30U, &unread));
    TEST_ASSERT_FALSE(gsm_sms_handle_response((const uint8_t *)invalid[0],
                                             strlen(invalid[0]), NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, log_calls);
}

/*** end of file ***/
