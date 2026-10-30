/*
 * test_http_rf_group_routes.c
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise production HTTP routing and authorization for RF group actions.
 */

#include "unity.h"
#include "mock_http_handlers.h"
#include "mock_http_request_parser.h"
#include "mock_http_response.h"
#include "mock_bsp.h"
#include "mock_reboot.h"

uint32_t HAL_GetTick(void);
uint32_t HAL_GetTick(void)
{
    return 0U;
}

/* W-01: http_server.c now calls this when the unauthorized streak
 * hits the threshold; the transport tests exercise the real one. */
static uint8_t close_after_response_calls;
void gsm_http_server_close_after_response(void);
void gsm_http_server_close_after_response(void)
{
    close_after_response_calls++;
}

#include "../../Application/web-server/http_server.c"

void setUp(void)
{
    http_handlers_set_auth_from_token_Ignore();
}

void tearDown(void)
{
}

void test_unauthenticated_apply_request_is_rejected_before_action(void)
{
    http_request_t request = {.method = HTTP_METHOD_POST};

    request.path = "/config/rf/apply/1/7";
    http_handlers_is_authenticated_ExpectAndReturn(false);
    http_send_error_Expect(401, "Unauthorized");
    route_and_handle_request(&request);
}

void test_user_cannot_apply_or_abort_but_can_read_group_status(void)
{
    http_request_t request = {.method = HTTP_METHOD_POST};

    request.path = "/config/rf/apply/1/7";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(false);
    http_send_error_Expect(403, "Admin role required");
    route_and_handle_request(&request);
    request.path = "/config/rf/abort";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(false);
    http_send_error_Expect(403, "Admin role required");
    route_and_handle_request(&request);
    request.path = "/config/rf";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(false);
    http_send_error_Expect(403, "Admin role required");
    route_and_handle_request(&request);
    request.method = HTTP_METHOD_GET;
    request.path = "/status/rf-group";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    handle_get_rf_group_status_json_Expect();
    route_and_handle_request(&request);
}

void test_admin_save_abort_and_retired_apply_route_to_handlers(void)
{
    http_request_t request = {.method = HTTP_METHOD_POST};

    request.path = "/config/rf/apply/7/255";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_post_rf_apply_Expect("7/255");
    route_and_handle_request(&request);
    request.path = "/config/rf/abort";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_post_rf_abort_Expect();
    route_and_handle_request(&request);
    request.path = "/config/rf";
    request.body = "{}";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_post_rf_config_json_Expect("{}");
    route_and_handle_request(&request);
}

void test_get_never_starts_an_apply_operation(void)
{
    http_request_t request = {.method = HTTP_METHOD_GET};

    request.path = "/config/rf/apply/1/7";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_send_not_found_Expect();
    route_and_handle_request(&request);
}

void test_alarm_ack_requires_authenticated_admin_and_post(void)
{
    http_request_t request = {.method = HTTP_METHOD_POST, .path = "/alarm/rf/ack", .body = "{}"};
    http_handlers_is_authenticated_ExpectAndReturn(false);
    http_send_error_Expect(401, "Unauthorized");
    route_and_handle_request(&request);
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(false);
    http_send_error_Expect(403, "Admin role required");
    route_and_handle_request(&request);
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_post_rf_alarm_ack_Expect("{}");
    route_and_handle_request(&request);
    request.method = HTTP_METHOD_GET;
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_send_not_found_Expect();
    route_and_handle_request(&request);
}

void test_firmware_chunk_routes_exact_numeric_parameters_and_aliases(void)
{
    static const char *const queries[] =
    {
        "t=valid&o=42&cs=65535", "offset=42&cs=65535&t=valid",
        "foo=999&xcs=123&o=42&cs=65535"
    };
    http_request_t request = {.method = HTTP_METHOD_POST, .path = "/c", .body = "abc", .body_length = 3};

    for (size_t index = 0U; index < sizeof(queries) / sizeof(queries[0]); index++)
    {
        request.query_string = (char *)queries[index];
        http_handlers_is_authenticated_ExpectAndReturn(true);
        http_handlers_is_admin_ExpectAndReturn(true);
        handle_fw_chunk_Expect(42U, (const uint8_t *)request.body, 3U, true, 65535U);
        route_and_handle_request(&request);
    }
    request.path = "/fw_chunk";
    request.query_string = "offset=4294967295";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_fw_chunk_Expect(UINT32_MAX, (const uint8_t *)request.body, 3U, false, 0U);
    route_and_handle_request(&request);
}

void test_invalid_firmware_chunk_parameters_never_reach_update_handler(void)
{
    static const char *const queries[] =
    {
        "o=-1", "o=+1", "o=abc", "o=", "o", "o=12x", "o=4294967296",
        "o=9999999999999999999999", "cs=65536", "cs=-1", "cs=", "cs", "cs=12x",
        "o=1&offset=2", "cs=1&cs=2"
    };
    http_request_t request = {.method = HTTP_METHOD_POST, .path = "/c", .body = "abc", .body_length = 3};

    for (size_t index = 0U; index < sizeof(queries) / sizeof(queries[0]); index++)
    {
        request.query_string = (char *)queries[index];
        http_handlers_is_authenticated_ExpectAndReturn(true);
        http_handlers_is_admin_ExpectAndReturn(true);
        http_send_bad_request_Expect();
        route_and_handle_request(&request);
    }
    request.query_string = "foo=123&xcs=456";
    http_handlers_is_authenticated_ExpectAndReturn(true);
    http_handlers_is_admin_ExpectAndReturn(true);
    handle_fw_chunk_Expect(0U, (const uint8_t *)request.body, 3U, false, 0U);
    route_and_handle_request(&request);
}

/*** end of file ***/
