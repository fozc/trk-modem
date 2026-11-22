/*
 * test_http_auth_scenario.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Real HTTP login, session replacement, lockout and hardware RNG fallback.
 */
#include "unity.h"
#include "../support/http_handler_platform.inc"
#include "mock_stm32u3xx_hal_rng.h"
#include "device_auth.h"
#include "auth_defaults_fixture.h"
TEST_SOURCE_FILE("bsp_random.c")
TEST_SOURCE_FILE("device_auth.c")
TEST_SOURCE_FILE("hmac_sha256.c")
TEST_SOURCE_FILE("sha256.c")
TEST_SOURCE_FILE("json.c")
TEST_SOURCE_FILE("xprintf.c")
RNG_HandleTypeDef hrng;
uint32_t test_rng_flags;
static uint32_t test_tick = 123456U;
static uint32_t random_counter = 1U;
static bool entropy_ok = true;
static int response_status;
static char output[512];
static auth_record_t records[AUTH_ACCOUNT_COUNT];
static uint32_t revisions[AUTH_ACCOUNT_COUNT];
const auth_record_t *nvram_get_auth_record(auth_account_t account)
{
    return ((uint32_t)account < AUTH_ACCOUNT_COUNT) ? &records[account] : NULL;
}
bool nvram_save_auth_record(auth_account_t account, const auth_record_t *record)
{
    records[account] = *record;
    revisions[account]++;
    return true;
}
uint32_t nvram_auth_revision(auth_account_t account)
{
    return ((uint32_t)account < AUTH_ACCOUNT_COUNT) ? revisions[account] : 0U;
}
uint32_t bsp_get_tick(void)
{
    return test_tick;
}
void gsm_get_rxtx_counters(uint32_t *tx, uint32_t *rx)
{
    *tx = 4096U;
    *rx = 1024U;
}
uint32_t gsm_get_ip_addr(void)
{
    return 0x0A000018U;
}
uint32_t gsm_get_web_client_ip(void)
{
    return 0U;
}
void elog_log_web_login_fail(uint32_t ip)
{
    (void)ip;
}
void http_send_error(int code, const char *text)
{
    (void)text;
    response_status = code;
}
void http_send_bad_request(void)
{
    response_status = 400;
}
void http_send_json(const char *body, int length)
{
    (void)body;
    (void)length;
    response_status = 200;
}
void http_send_response(int status, const char *reason, const char *type, const char *body, int length)
{
    (void)reason;
    (void)type;
    (void)body;
    (void)length;
    response_status = status;
}
static HAL_StatusTypeDef hardware_random(RNG_HandleTypeDef *handle, uint32_t *word, int call)
{
    (void)handle;
    (void)call;
    if (!entropy_ok)
        return HAL_ERROR;
    *word = random_counter++;
    return HAL_OK;
}
#include "../../Application/web-server/http_handlers.c"
static char saved_token[HTTP_SESSION_TOKEN_SIZE];
static char query[40];

static void check_login_input_preserves_session(void)
{
    handler_state.tx_buffer = output;
    handler_state.tx_buffer_size = sizeof(output);
    handle_post_login(" \t{\n \"password\" : \"admin\",\r\n \"username\" : \"admin\" } \n");
    TEST_ASSERT_TRUE(response_status == 200 && http_handlers_is_admin());
    puts("PASS: whitespace and reversed credential fields authenticate");
    handle_post_login("{\"username\":\"\\u0061dmin\",\"password\":\"ad\\u006din\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    puts("PASS: escaped ASCII credentials authenticate");
    char protected_token[HTTP_SESSION_TOKEN_SIZE];
    strcpy(protected_token, handler_state.session_token);
    const char *bad_json[] = {
        "{\"username\":\"admin\"}",
        "{\"username\":\"admin\",\"password\":25}",
        "{\"username\":\"admin\",\"password\":\"admin\"} trailing",
        "{\"username\":\"admin\",\"username\":\"user\",\"password\":\"admin\"}",
        "{\"username\":\"admin\",\"password\":\"admin\",}",
        "{\"username\":\"admin\\u0000junk\",\"password\":\"admin\"}",
        "{\"username\":\"admin\",\"password\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}"};
    for (size_t i = 0U; i < sizeof(bad_json) / sizeof(bad_json[0]); i++)
    {
        login_lock_reset(); /* Independent input cases; lockout is checked below. */
        handle_post_login(bad_json[i]);
        TEST_ASSERT_TRUE(strcmp(protected_token, handler_state.session_token) == 0);
        TEST_ASSERT_TRUE(handler_state.is_authenticated);
    }
    puts("PASS: malformed credentials preserve the existing session");
}

static void check_session_replacement_and_fallback(void)
{
    test_tick += LOGIN_LOCKOUT_MS;
    handle_post_logout();
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    TEST_ASSERT_FALSE(device_auth_set_password(AUTH_ACCOUNT_ADMIN, "bad\x1B" "secret"));
    TEST_ASSERT_TRUE(strlen(handler_state.session_token) == 32U);
    char first_token[HTTP_SESSION_TOKEN_SIZE];
    strcpy(first_token, handler_state.session_token);
    snprintf(query, sizeof(query), "t=%s", first_token);
    handler_state.is_authenticated = false;
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    puts("PASS: configured admin password and new token authenticate");
    http_handlers_set_auth_from_token("t=5A5BE221");
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    puts("PASS: old tick-derived 8-digit token rejected");
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    TEST_ASSERT_TRUE(strcmp(first_token, handler_state.session_token) != 0);
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    puts("PASS: relogin invalidates previous token");
    strcpy(saved_token, handler_state.session_token);
    snprintf(query, sizeof(query), "t=%s", saved_token);
    entropy_ok = false;
    handle_post_login("{\"username\":\"user\",\"password\":\"user11\"}");
    TEST_ASSERT_TRUE(response_status == 200 && handler_state.is_authenticated);
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    TEST_ASSERT_TRUE(strcmp(saved_token, handler_state.session_token) != 0);
    TEST_ASSERT_TRUE(strcmp(handler_state.username, "user") == 0);
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_TRUE(!handler_state.is_authenticated);
    snprintf(query, sizeof(query), "t=%s", handler_state.session_token);
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_TRUE(handler_state.is_authenticated && !http_handlers_is_admin());
    puts("PASS: RNG failure creates authenticated user fallback and replaces old token");
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(response_status == 200 && http_handlers_is_admin());
    puts("PASS: valid admin credentials can create fallback session");
}

static void check_expiry_logout_and_lockout(void)
{
    strcpy(saved_token, handler_state.session_token);
    handle_post_login("{\"username\":\"admin\",\"password\":\"wrong\"}");
    TEST_ASSERT_TRUE(strcmp(saved_token, handler_state.session_token) == 0);
    TEST_ASSERT_TRUE(response_status == 401);
    puts("PASS: invalid credentials do not create a fallback session (HTTP 401)");
    handle_post_logout();
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(response_status == 200 && http_handlers_is_admin());
    TEST_ASSERT_TRUE(strlen(handler_state.session_token) == 32U);
    strcpy(saved_token, handler_state.session_token);
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(strcmp(saved_token, handler_state.session_token) != 0);
    puts("PASS: repeated fallback logins at the same tick create different tokens");
    entropy_ok = true;
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    snprintf(query, sizeof(query), "t=%s", handler_state.session_token);
    test_tick += HTTP_SESSION_TIMEOUT_MS + 1U;
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    TEST_ASSERT_TRUE(handler_state.session_token[0] == '\0');
    puts("PASS: expired token rejected and cleared");
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    handle_post_logout();
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    TEST_ASSERT_TRUE(handler_state.session_token[0] == '\0');
    puts("PASS: logout clears session");
    for (unsigned int i = 0U; i < 5U; i++)
        handle_post_login("{\"username\":\"admin\",\"password\":\"wrong\"}");
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(!http_handlers_is_admin());
    test_tick += LOGIN_LOCKOUT_MS;
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    puts("PASS: login lockout still works and expires");
}

void test_login_session_replacement_expiry_lockout_and_fallback(void)
{
    check_login_input_preserves_session();
    check_session_replacement_and_fallback();
    check_expiry_logout_and_lockout();
}

void setUp(void)
{
    records[AUTH_ACCOUNT_ADMIN] = (auth_record_t){
        .salt = AUTH_DEFAULT_ADMIN_SALT, .digest = AUTH_DEFAULT_ADMIN_DIGEST
    };
    records[AUTH_ACCOUNT_FACTORY] = (auth_record_t){
        .salt = AUTH_DEFAULT_FACTORY_SALT, .digest = AUTH_DEFAULT_FACTORY_DIGEST
    };
    handler_state.tx_buffer = output;
    handler_state.tx_buffer_size = sizeof(output);
    handler_state.is_authenticated = false;
    handler_state.session_token[0] = '\0';
    entropy_ok = true;
    login_lock_reset();
    HAL_RNG_GenerateRandomNumber_Stub(hardware_random);
}

void test_legacy_admin_password_is_rejected_and_default_password_logs_in(void)
{
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin60\"}");
    TEST_ASSERT_EQUAL_INT(401, response_status);
    TEST_ASSERT_FALSE(http_handlers_is_authenticated());

    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_EQUAL_INT(200, response_status);
    TEST_ASSERT_TRUE(http_handlers_is_admin());
}

void test_factory_can_change_admin_password_from_web(void)
{
    handle_post_login("{\"username\":\"admin\",\"password\":\"admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    handle_post_password("{\"username\":\"factory-admin\",\"password\":\"admin\",\"newPassword\":\"changed\"}");
    TEST_ASSERT_EQUAL_INT(403, response_status);
    TEST_ASSERT_TRUE(device_auth_verify(AUTH_ACCOUNT_FACTORY, "factory-admin"));

    handle_post_login("{\"username\":\"factory-admin\",\"password\":\"factory-admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    handle_post_password("{\"username\":\"admin\",\"password\":\"factory-admin\",\"newPassword\":\"new-admin\"}");
    TEST_ASSERT_EQUAL_INT(200, response_status);
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    TEST_ASSERT_FALSE(device_auth_verify(AUTH_ACCOUNT_ADMIN, "admin"));
    TEST_ASSERT_TRUE(device_auth_verify(AUTH_ACCOUNT_ADMIN, "new-admin"));

    handle_post_login("{\"username\":\"admin\",\"password\":\"new-admin\"}");
    TEST_ASSERT_TRUE(http_handlers_is_admin());
    snprintf(query, sizeof(query), "t=%s", handler_state.session_token);
    TEST_ASSERT_TRUE(device_auth_set_password(AUTH_ACCOUNT_ADMIN, "from-usb"));
    http_handlers_set_auth_from_token(query);
    TEST_ASSERT_FALSE(http_handlers_is_authenticated());
}

void tearDown(void)
{
}

/*** end of file ***/
