/*
 * test_http_rf_save_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real RF save and operator ACK handlers with stateful neighboring-service doubles.
 */
#include "unity.h"
#include "../support/http_handler_platform.inc"
TEST_SOURCE_FILE("json.c")
TEST_SOURCE_FILE("xprintf.c")
bool rf_store_stage_begin_unused(void);
void rf_store_stage_commit_unused(void);
void rf_store_stage_abort_unused(void);
/* Storage staging is a boundary double; EUI decoding remains production code. */
#define rf_store_stage_begin rf_store_stage_begin_unused
#define rf_store_stage_commit rf_store_stage_commit_unused
#define rf_store_stage_abort rf_store_stage_abort_unused
#include "../../Application/rf/rf_config.c"
#undef rf_store_stage_begin
#undef rf_store_stage_commit
#undef rf_store_stage_abort
TEST_SOURCE_FILE("rf_group_web.c")
static uint32_t operator_ack_calls;
static rf_alarm_ack_result_t operator_ack_result;
static rf_alarm_ack_request_t received_ack;
rf_alarm_ack_result_t rf_alarm_acknowledge(const rf_alarm_ack_request_t *request)
{
    operator_ack_calls++;
    received_ack = *request;
    return operator_ack_result;
}
static char canvas[514], output[512];
static int response_status, output_length;
static uint32_t apply_calls, abort_calls;
static bool accepts_apply, accepts_abort;
static rf_group_status_t group;
bool rf_group_start(size_t line, uint8_t id)
{
    TEST_ASSERT_TRUE(line == 2U && id == 7U);
    apply_calls++;
    return accepts_apply;
}
bool rf_group_get_status(rf_group_status_t *out)
{
    *out = group;
    return true;
}
bool rf_group_matches_config(void)
{
    return false;
}
bool rf_apply_abort(void)
{
    abort_calls++;
    return accepts_abort;
}
bool rf_apply_can_abort(void)
{
    return accepts_abort;
}
rf_inventory_status_t rf_inventory_get_status(void)
{
    return RF_INVENTORY_READY;
}
bool rf_inventory_has_pending(void)
{
    return false;
}
void rf_apply_get_status(rf_apply_status_t *out)
{
    *out = (rf_apply_status_t){.state = RF_APPLY_RUNNING, .targets = 5U};
}
void rf_inventory_get_wait(rf_inventory_wait_t *out)
{
    *out = (rf_inventory_wait_t){.reason = "none"};
}
static bool can_save, parses, stages;
static uint32_t parse_calls, commit_calls, save_calls, stage_aborts;
static rf_apply_save_result_t save_result;
bool rf_apply_can_save(void)
{
    return can_save;
}
rf_apply_save_result_t rf_apply_save(void)
{
    TEST_ASSERT_TRUE(commit_calls == 0U);
    save_calls++;
    return save_result;
}
bool rf_store_stage_begin(void)
{
    return stages;
}
void rf_store_stage_commit(void)
{
    commit_calls++;
}
void rf_store_stage_abort(void)
{
    stage_aborts++;
}
int parse_rf_config(const char *text, jayirici_rf_config_t *out)
{
    (void)text;
    memset(out, 0, sizeof(*out));
    parse_calls++;
    return parses;
}
uint32_t gsm_get_web_client_ip(void)
{
    return 0x7F000001U;
}
void elog_log_config_change(elog_code_t a, elog_config_source_t b, uint32_t c, const char *d, bool e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}
void http_send_bad_request(void)
{
    response_status = 400;
}
void http_send_json(const char *data, int length)
{
    TEST_ASSERT_TRUE(length >= 0 && (size_t)length < sizeof(output));
    memcpy(output, data, (size_t)length);
    output[length] = '\0';
    output_length = length;
    response_status = 200;
}
void http_send_error(int status, const char *text)
{
    (void)text;
    response_status = status;
}
#include "../../Application/web-server/http_handlers.c"
static void check_save_and_abort_paths(void)
{
    memset(canvas, '*', sizeof(canvas));
    handler_state.tx_buffer = canvas + 1;
    handler_state.tx_buffer_size = 512;
    group.state = RF_GROUP_APPLIED;
    group.line = 3U;
    group.feeder = 1U;
    group.has_report = true;
    group.expected_crc = 0x096DU;
    group.report.config_crc = 0x096DU;
    handle_get_rf_group_status_json();
    TEST_ASSERT_TRUE(response_status == 200 && output_length == (int)strlen(output));
    TEST_ASSERT_TRUE(strstr(output, "\"MatchesDesired\":false") != NULL);
    TEST_ASSERT_TRUE(apply_calls == 0U && abort_calls == 0U);
    TEST_ASSERT_TRUE(canvas[0] == '*' && canvas[513] == '*');
    handler_state.tx_buffer_size = 8;
    handle_get_rf_group_status_json();
    TEST_ASSERT_TRUE(response_status == 500);
    handler_state.tx_buffer_size = 512;
    handle_post_rf_apply("3/256");
    TEST_ASSERT_TRUE(response_status == 410);
    handle_post_rf_apply("3/7");
    TEST_ASSERT_TRUE(response_status == 410);
    TEST_ASSERT_TRUE(apply_calls == 0U);
    handle_post_rf_config_json(NULL);
    TEST_ASSERT_TRUE(response_status == 400);
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 409);
    TEST_ASSERT_TRUE(parse_calls == 0U && commit_calls == 0U && save_calls == 0U);
    can_save = true;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 500);
    TEST_ASSERT_TRUE(parse_calls == 0U && save_calls == 0U);
    stages = true;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 400);
    TEST_ASSERT_TRUE(stage_aborts == 1U && commit_calls == 0U && save_calls == 0U);
    parses = true;
    save_result = RF_APPLY_SAVE_ERROR;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 500);
    TEST_ASSERT_TRUE(commit_calls == 0U && save_calls == 1U);
    save_result = RF_APPLY_SAVE_PRIMARY_ONLY;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 200);
    TEST_ASSERT_TRUE(strstr(output, "\"warning\":\"backup_failed\"") != NULL);
    save_result = RF_APPLY_SAVE_OK;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 200);
    TEST_ASSERT_TRUE(commit_calls == 0U && save_calls == 3U);
    TEST_ASSERT_TRUE(strstr(output, "\"success\":true") != NULL);
    save_result = RF_APPLY_SAVE_MAINTENANCE;
    const uint32_t old_aborts = stage_aborts;
    handle_post_rf_config_json("{}");
    TEST_ASSERT_TRUE(response_status == 409);
    TEST_ASSERT_TRUE(stage_aborts == old_aborts + 1U && commit_calls == 0U && save_calls == 4U);
    handle_post_rf_abort();
    TEST_ASSERT_TRUE(response_status == 409);
    accepts_abort = true;
    handle_post_rf_abort();
    TEST_ASSERT_TRUE(response_status == 200 && abort_calls == 2U);
}

static void check_operator_acknowledgement_paths(void)
{
    const char *ack =
        "{\"Line\":3,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false}";
    handle_post_rf_alarm_ack(ack);
    TEST_ASSERT_TRUE(response_status == 403 && operator_ack_calls == 0U);
    handler_state.is_authenticated = true;
    strcpy(handler_state.username, "admin");
    const char *invalid_acks[] = {
        "{}",
        "[]",
        "null",
        "{\"Line\":3,\"Phase\":1,\"Eui64\":\"\",\"Revision\":17,\"ConfirmOffline\":false}",
        "{\"Line\":3,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":0,\"ConfirmOffline\":false}",
        "{\"Line\":0,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false}",
        "{\"Line\":8,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false}",
        "{\"Line\":3,\"Phase\":4,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false}",
        "{\"Line\":3,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":1}",
        "{\"Line\":3,\"Line\":3,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false}",
        "{\"Line\":3,\"Phase\":1,\"Eui64\":\"00124B0038C9F1BE\",\"Revision\":17,\"ConfirmOffline\":false,\"User\":\"admin\"}"};
    for (size_t i = 0U; i < sizeof(invalid_acks) / sizeof(invalid_acks[0]); i++)
    {
        handle_post_rf_alarm_ack(invalid_acks[i]);
        TEST_ASSERT_TRUE(response_status == 400 && operator_ack_calls == 0U);
    }
    operator_ack_result = RF_ALARM_ACK_OFFLINE;
    handle_post_rf_alarm_ack(ack);
    TEST_ASSERT_TRUE(response_status == 428 && operator_ack_calls == 1U);
    TEST_ASSERT_TRUE(received_ack.line == 2U && received_ack.phase == 0U && received_ack.revision == 17U);
    TEST_ASSERT_TRUE(received_ack.eui64[7] == 0xBEU && !received_ack.confirm_offline);
    operator_ack_result = RF_ALARM_ACK_STORAGE;
    handle_post_rf_alarm_ack(ack);
    TEST_ASSERT_TRUE(response_status == 503);
    operator_ack_result = RF_ALARM_ACK_STALE;
    handle_post_rf_alarm_ack(ack);
    TEST_ASSERT_TRUE(response_status == 409);
    operator_ack_result = RF_ALARM_ACK_OK;
    handle_post_rf_alarm_ack(ack);
    TEST_ASSERT_TRUE(response_status == 200);
}

void test_rf_save_validation_backup_warning_and_operator_acknowledgement(void)
{
    check_save_and_abort_paths();
    check_operator_acknowledgement_paths();
}

void setUp(void)
{
}

void tearDown(void)
{
}

/*** end of file ***/
