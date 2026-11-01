/*
 * test_http_board_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real board handler, separate GSM measurements and guarded JSON output.
 */
#include "unity.h"
#include "../support/http_handler_platform.inc"
#include <stdio.h>
#include <stdlib.h>
TEST_SOURCE_FILE("system_status_json.c")
TEST_SOURCE_FILE("json.c")
TEST_SOURCE_FILE("xprintf.c")
static FILE *board_output;
static system_status_t board;
static char canvas[4098];
static uint8_t rxlev, rscp, rsrp, rsrq, creg, cgreg, cereg;
const system_status_t *system_status_get(void)
{
    return &board;
}
uint8_t gsm_info_get_signal_quality_2G(void)
{
    return rxlev;
}
uint8_t gsm_info_get_signal_quality_3G(void)
{
    return rscp;
}
uint8_t gsm_info_get_signal_quality_4G(void)
{
    return rsrp;
}
uint8_t gsm_info_get_4G_rsrq(void)
{
    return rsrq;
}
gsm_net_reg_state_t gsm_info_get_creg(void)
{
    return (gsm_net_reg_state_t)creg;
}
gsm_net_reg_state_t gsm_info_get_cgreg(void)
{
    return (gsm_net_reg_state_t)cgreg;
}
gsm_net_reg_state_t gsm_info_get_cereg(void)
{
    return (gsm_net_reg_state_t)cereg;
}
void http_send_error(int code, const char *message)
{
    (void)code;
    (void)message;
    TEST_ASSERT_TRUE(false);
}
void http_send_json(const char *data, int length)
{
    TEST_ASSERT_TRUE(length > 0 && length < 4096);
    TEST_ASSERT_TRUE(strlen(data) == (size_t)length);
    TEST_ASSERT_TRUE(data[0] == '{' && data[length - 1] == '}');
    static const char *const keys[] = {"GsmRAT",  "GsmRxlev", "GsmRscp",  "GsmRsrp",
                                       "GsmRsrq", "GsmCREG",  "GsmCGREG", "GsmCEREG"};
    static const uint8_t expected[2][8] = {{4U, 99U, 255U, 41U, 20U, 0U, 0U, 1U},
                                           {2U, 51U, 255U, 255U, 255U, 1U, 5U, 0U}};
    const size_t row = (board.gsm_rat == 4U) ? 0U : 1U;
    for (size_t index = 0U; index < 8U; index++)
    {
        char field[48];
        (void)snprintf(field, sizeof(field), "\"%s\":%u", keys[index], (unsigned)expected[row][index]);
        const char *match = strstr(data, field);
        TEST_ASSERT_NOT_NULL_MESSAGE(match, keys[index]);
        const char next = match[strlen(field)];
        TEST_ASSERT_TRUE(next == ',' || next == '}');
    }
    TEST_ASSERT_NOT_NULL(strstr(data, "\"GsmSig\":13,"));
    TEST_ASSERT_NOT_NULL(strstr(data, "\"DIP\":[1,0]"));
    if (board_output != NULL)
    {
        TEST_ASSERT_TRUE(fputs(data, board_output) >= 0);
        TEST_ASSERT_TRUE(fputc('\n', board_output) != EOF);
    }
}
#include "../../Application/web-server/http_handlers.c"
void test_board_signal_and_registration_fields_survive_handler_serialization(void)
{
    memset(canvas, '*', sizeof(canvas));
    handler_state.tx_buffer = canvas + 1;
    handler_state.tx_buffer_size = 4096;
    board.gsm_signal = 13;
    board.dip_sw[0] = 1U;
    board.dip_sw[1] = 0U;
    board.gsm_rat = 4;
    rxlev = 99;
    rscp = 255;
    rsrp = 41;
    rsrq = 20;
    creg = 0;
    cgreg = 0;
    cereg = 1;
    handle_get_board_status_json();
    board.gsm_rat = 2;
    rxlev = 51;
    rscp = 255;
    rsrp = 255;
    rsrq = 255;
    creg = 1;
    cgreg = 5;
    cereg = 0;
    handle_get_board_status_json();
    TEST_ASSERT_TRUE(canvas[0] == '*' && canvas[4097] == '*');
}


void setUp(void)
{
    const char *path = getenv("HTTP_BOARD_OUTPUT");
    if (path != NULL)
    {
        board_output = fopen(path, "w");
        TEST_ASSERT_NOT_NULL(board_output);
    }
}

void tearDown(void)
{
    if (board_output != NULL)
    {
        TEST_ASSERT_EQUAL_INT(0, fclose(board_output));
        board_output = NULL;
    }
}

/*** end of file ***/
