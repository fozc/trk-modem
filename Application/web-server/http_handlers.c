/*
 * http_handlers.c
 *
 *  Created on: Apr 18, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * HTTP endpoint handlers implementation.
 */

#define CSLOG_MODULE LOG_MOD_HTTP
#include "http_handlers.h"
#include "modem_config.h"
#include "http_response.h"
#include "http_session_token.h"
#include "bsp_random.h"
#include "http_request_parser.h"
#include "index_html.h"
#include "fw_update_html.h"
#include "loading_html.h"
#include "web_shell.h"
#include "json_config.h"
#include "web_json.h"
#include "nvram.h"
#include "xprintf.h"
#include <stddef.h>
#include <string.h>
#include "../utils.h"
#include "version.h"
#include "boot.h"
#include "app_ipc.h"
#include "reboot.h"

#include "system_status.h"
#include "index_html.h"
#include "modbus_config.h"
#include "modbus_map_json.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "rf.h"
#include "rf_alarm.h"
#include "rf_config.h"
#include "rf_discovery.h"
#include "rf_json.h"
#include "rf_monitor_json.h"
#include "rf_group_web.h"
#include "rf_group.h"
#include "rf_apply.h"
#include "system_status_json.h"
#include "gsm_engine.h"
#include "gsm_info.h"
#include "elog.h"
#include "fault_log.h"
#include "cp56time2a.h"
#include "datetime.h"
#include "bsp.h"
#include "rtc.h"
#include "stm32u3xx_hal.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

#define USER_ROLE_ADMIN "admin"
#define USER_ROLE_USER "user"

/** Session idle timeout: if no authenticated request is received within this
 *  period the session token is invalidated and re-login is required. */
#define HTTP_SESSION_TIMEOUT_MS (15UL * 60UL * 1000UL)

/** Brute-force lockout: this many consecutive failed logins locks the login
 *  endpoint for LOGIN_LOCKOUT_MS. The counter resets on a successful login
 *  and when the lock expires. RAM-only state: a reboot clears the lock. */
#define LOGIN_MAX_FAILED_ATTEMPTS (5U)
#define LOGIN_LOCKOUT_MS (60UL * 1000UL)

/* ============================================================================
 * MODULE STATE
 * ============================================================================
 */

static struct
{
    char *tx_buffer;
    int tx_buffer_size;
    const char *current_query_string; /* Current request query string */
    bool is_authenticated;            /* Per-request auth flag, set by
                                         set_auth_from_token() */
    char username[16];                /* Logged-in role: "admin" or "user" */
    char session_token[HTTP_SESSION_TOKEN_SIZE]; /* Empty = no session */
    uint32_t
        last_activity_tick;   /* bsp_get_tick() of last authenticated request */
    uint8_t login_fail_count; /* Consecutive failed logins since last reset */
    bool login_lock_active;   /* Brute-force lock engaged */
    uint32_t login_lock_start_tick; /* bsp_get_tick() when the lock engaged */
} handler_state;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================
 */

void http_handlers_init(char *tx_buffer_ptr, int tx_buffer_size)
{
    handler_state.tx_buffer = tx_buffer_ptr;
    handler_state.tx_buffer_size = tx_buffer_size;
    handler_state.current_query_string = NULL;
    handler_state.is_authenticated = false;
    handler_state.session_token[0] = '\0';
    handler_state.last_activity_tick = 0U;
    handler_state.login_fail_count = 0U;
    handler_state.login_lock_active = false;
    handler_state.login_lock_start_tick = 0U;
}

void http_handlers_set_query_string(const char *query_string)
{
    handler_state.current_query_string = query_string;
}

void http_handlers_reset(void)
{
    /* Clear the per-request auth flag and stale query string pointer.
     * session_token and username are intentionally preserved so the client
     * can continue using the same token after a socket reconnect. */
    handler_state.is_authenticated = false;
    handler_state.current_query_string = NULL;
    CSLOG("[HTTP] Connection closed, session preserved\r\n");
}

char *http_handlers_get_tx_buffer(int *size)
{
    if (size)
    {
        *size = handler_state.tx_buffer_size;
    }
    return handler_state.tx_buffer;
}

const char *http_handlers_get_query_string(void)
{
    return handler_state.current_query_string;
}

bool http_handlers_session_active(void)
{
    return '\0' != handler_state.session_token[0];
}

bool http_handlers_is_authenticated(void)
{
    return handler_state.is_authenticated;
}

bool http_handlers_is_admin(void)
{
    return handler_state.is_authenticated &&
           strcmp(handler_state.username, USER_ROLE_ADMIN) == 0;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/* ============================================================================
 * TOKEN AUTHENTICATION
 * ============================================================================
 */

/**
 * @brief Validate the session token from the request query string.
 *
 * Must be called at the start of every request, before the auth gate.
 * Sets handler_state.is_authenticated accordingly.
 *
 * @param query_string Request query string (may be NULL)
 */
void http_handlers_set_auth_from_token(const char *query_string)
{
    /* No active session */
    if ('\0' == handler_state.session_token[0])
    {
        handler_state.is_authenticated = false;
        return;
    }

    if (!http_session_token_matches(handler_state.session_token, query_string))
    {
        handler_state.is_authenticated = false;
        return;
    }

    /* Check idle TTL */
    if ((bsp_get_tick() - handler_state.last_activity_tick) >
        HTTP_SESSION_TIMEOUT_MS)
    {
        handler_state.is_authenticated = false;
        handler_state.session_token[0] = '\0';
        handler_state.username[0] = '\0';
        CSLOG("[HTTP] Session expired\r\n");
        return;
    }

    /* Token valid and within TTL */
    handler_state.is_authenticated = true;
    handler_state.last_activity_tick = bsp_get_tick();
}

/* ============================================================================
 * GET HANDLERS
 * ============================================================================
 */

void handle_get_index(void)
{
    /* Serve index.html from flash */
    http_send_html_resource(get_index_html());
}

void handle_get_loading(void)
{
    http_send_html_resource(get_loading_html());
}

void handle_get_index_package(void)
{
    http_send_gzip_package(get_index_html());
}

void handle_get_fw_update(void)
{
    /* Serve fw_update.html from flash */
    http_send_html_resource(get_fw_update_html());
}

/* ============================================================================
 * LOGIN BRUTE-FORCE LOCKOUT
 * ============================================================================
 */

/**
 * @brief Check whether the login lockout currently rejects login attempts.
 *
 * An engaged lock expires lazily here (no periodic timer): once
 * LOGIN_LOCKOUT_MS has passed the lock is released and the failure
 * counter starts fresh.
 *
 * @return true while the lock rejects attempts, false otherwise
 */
static bool login_lock_is_active(void)
{
    if (!handler_state.login_lock_active)
    {
        return false;
    }

    /* Wrap-safe elapsed compare (same idiom as the session timeout). */
    if ((bsp_get_tick() - handler_state.login_lock_start_tick) <
        LOGIN_LOCKOUT_MS)
    {
        return true;
    }

    handler_state.login_lock_active = false;
    handler_state.login_fail_count = 0U;
    return false;
}

/**
 * @brief Register a failed login and engage the lock at the threshold.
 */
static void login_lock_register_failure(void)
{
    handler_state.login_fail_count++;
    if ((unsigned int)handler_state.login_fail_count >=
        LOGIN_MAX_FAILED_ATTEMPTS)
    {
        handler_state.login_lock_active = true;
        handler_state.login_lock_start_tick = bsp_get_tick();
        handler_state.login_fail_count = 0U;
        CSLOG_ERR("[HTTP] Login locked for %lu s after %u failed attempts\r\n",
                  LOGIN_LOCKOUT_MS / 1000UL,
                  (unsigned int)LOGIN_MAX_FAILED_ATTEMPTS);
    }
}

/**
 * @brief Clear the lockout state after a successful login.
 */
static void login_lock_reset(void)
{
    handler_state.login_fail_count = 0U;
    handler_state.login_lock_active = false;
}

/* ============================================================================
 * POST HANDLERS
 * ============================================================================
 */

static void http_json_reply(const char *body)
{
    /* Callers supply short, static JSON documents, never binary spans. */
    http_send_json(body, (int)strlen(body));
}

static bool parse_login_credentials(const char *body, char *username,
                                    size_t username_size, char *password,
                                    size_t password_size)
{
    if (body == NULL)
    {
        return false;
    }
    json_reader_t reader;
    json_token_t token;
    char scratch[64];
    json_span_t input = {body, strlen(body)};
    bool has_username = false;
    bool has_password = false;
    if (json_reader_init(&reader, scratch, sizeof(scratch)) != JSON_OK ||
        json_reader_feed(&reader, input, true) != JSON_OK ||
        json_read(&reader, &token) != JSON_OK ||
        token.type != JSON_OBJECT_BEGIN)
    {
        return false;
    }
    for (;;)
    {
        if (json_read(&reader, &token) != JSON_OK)
        {
            return false;
        }
        if (token.type == JSON_OBJECT_END)
        {
            return has_username && has_password &&
                   json_read(&reader, &token) == JSON_DONE;
        }
        if (token.type != JSON_KEY)
        {
            return false;
        }
        bool is_username = json_token_equals(&token, "username");
        bool is_password = json_token_equals(&token, "password");
        if (json_read(&reader, &token) != JSON_OK || token.type != JSON_STRING)
        {
            return false;
        }
        if (is_username)
        {
            if (has_username ||
                json_token_copy_string(&token, username, username_size) !=
                    JSON_ERROR_NONE)
            {
                return false;
            }
            has_username = true;
        }
        else if (is_password)
        {
            if (has_password ||
                json_token_copy_string(&token, password, password_size) !=
                    JSON_ERROR_NONE)
            {
                return false;
            }
            has_password = true;
        }
        else
        {
            /* Unknown string fields do not supply credentials. */
        }
    }
}

static bool http_json_init(json_writer_t *writer)
{
    if (handler_state.tx_buffer_size <= 0 ||
        !web_json_init(writer, handler_state.tx_buffer,
                       (size_t)handler_state.tx_buffer_size))
    {
        http_send_error(500, "JSON response buffer unavailable");
        return false;
    }
    return true;
}

static void http_json_send(json_writer_t *writer)
{
    size_t length = 0U;
    if (!web_json_finish(writer, handler_state.tx_buffer, &length))
    {
        http_send_error(500, "JSON response exceeds buffer or invalid data");
        return;
    }
    /* The HTTP buffer is supplied through an int-sized API. */
    http_send_json(handler_state.tx_buffer, (int)length);
}

static void create_login_session(const char *username)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    login_lock_reset();

    uint32_t tx_bytes = 0U;
    uint32_t rx_bytes = 0U;
    gsm_get_rxtx_counters(&tx_bytes, &rx_bytes);
    bsp_random_fallback_seed(bsp_get_tick(), tx_bytes, rx_bytes);

    char new_token[HTTP_SESSION_TOKEN_SIZE];
    if (!http_session_token_generate(new_token))
    {
        static const char error_response[] =
            "{\"success\":false,\"error\":\"Token unavailable\"}";
        http_send_response(503, NULL, "application/json", error_response,
                           (int)(sizeof(error_response) - 1U));
        return;
    }
    /* Replace the role and token only after token generation completes. */
    strncpy(handler_state.username, username,
            sizeof(handler_state.username) - 1U);
    handler_state.username[sizeof(handler_state.username) - 1U] = '\0';
    memcpy(handler_state.session_token, new_token, sizeof(new_token));
    handler_state.last_activity_tick = bsp_get_tick();
    handler_state.is_authenticated = true;
    CSLOG("[HTTP] Session authenticated\r\n");

    (void)json_write_object_begin(&writer);
    web_json_bool(&writer, "success", true);
    web_json_string(&writer, "token", handler_state.session_token);
    web_json_string(&writer, "role", handler_state.username);
    (void)json_write_object_end(&writer);
    http_json_send(&writer);
}

/**
 * @brief Handle POST /auth/login - Simple IP-based authentication
 */
void handle_post_login(const char *json_body)
{
    CSLOG("[HTTP] POST /auth/login - Login attempt\r\n");

    /* Brute-force lockout: reject before touching credentials. */
    if (login_lock_is_active())
    {
        CSLOG_ERR("[HTTP] Login rejected - lockout active\r\n");
        const char *locked_response =
            "{\"success\":false,\"error\":\"Too many attempts, "
            "try again later\"}";
        /* W-02: auth failures carry HTTP 401, not 200. */
        http_send_response(401, "Unauthorized", "application/json",
                           locked_response, (int)strlen(locked_response));
        return;
    }

    if (!json_body)
    {
        http_send_response(401, "Unauthorized", "application/json",
                           "{\"success\":false,\"error\":\"No body\"}", 35);
        return;
    }

    /* Parse username and password from JSON */
    char username[32] = {0};
    char password[64] = {0};

    const bool parsed = parse_login_credentials(
        json_body, username, sizeof(username), password, sizeof(password));

    CSLOG("[HTTP] Login attempt - Username: %s\r\n", username);

    /* Get device IP address */
    uint32_t ip = gsm_get_ip_addr();
    uint8_t ip_a = (uint8_t)((ip >> 24) & 0xFFU); /* First octet */
    uint8_t ip_d = (uint8_t)(ip & 0xFFU);         /* Last octet */

    CSLOG("[HTTP] Device IP octets - First: %d, Last: %d\r\n", ip_a, ip_d);

    /* Calculate expected passwords based on IP.
     * 12 bytes covers the longest value ("admin256" + NUL);
     * see doc/Web_Giris_Sifresi_Plani.md. */
    char expected_admin_pass[12] = {0};
    char expected_user_pass[12] = {0};
    unsigned int admin_len = xsnprintf(
        expected_admin_pass, sizeof(expected_admin_pass), "admin%d", ip_d + 1);
    unsigned int user_len = xsnprintf(
        expected_user_pass, sizeof(expected_user_pass), "user%d", ip_a + 1);
    if ((admin_len >= sizeof(expected_admin_pass)) ||
        (user_len >= sizeof(expected_user_pass)))
    {
        CSLOG_ERR("[HTTP] Login password truncated - increase buffer!\r\n");
    }

    /* Validate credentials */
    bool valid = false;
    if (parsed && strcmp(username, USER_ROLE_ADMIN) == 0 &&
        strcmp(password, expected_admin_pass) == 0)
    {
        valid = true;
    }
    else if (parsed && strcmp(username, USER_ROLE_USER) == 0 &&
             strcmp(password, expected_user_pass) == 0)
    {
        valid = true;
    }
    else
    {
        CSLOG_ERR("[HTTP] Login failed - Invalid credentials\r\n");
        elog_log_web_login_fail(gsm_get_web_client_ip());
        login_lock_register_failure();
    }

    /* Send response */
    if (valid)
    {
        create_login_session(username);
    }
    else
    {
        const char *error_response =
            "{\"success\":false,\"error\":\"Invalid credentials\"}";
        /* W-02: auth failure must carry HTTP 401, not 200. */
        http_send_response(401, "Unauthorized", "application/json",
                           error_response, (int)strlen(error_response));
    }
}

/**
 * @brief Handle POST /auth/logout - Clear session
 */
void handle_post_logout(void)
{
    CSLOG("[HTTP] POST /auth/logout - Logout request\r\n");

    /* Clear authentication state */
    handler_state.is_authenticated = false;
    handler_state.session_token[0] = '\0';
    memset(handler_state.username, 0, sizeof(handler_state.username));

    CSLOG("[HTTP] Session cleared\r\n");

    /* Send success response */
    const char *success_response = "{\"success\":true}";
    http_send_json(success_response, (int)strlen(success_response));
}

void handle_post_echo(const char *body, int body_length)
{
    /* Limit echo size */
    if (body_length > 512)
    {
        http_send_error(413, "Payload Too Large");
        return;
    }

    http_send_ok(body, body_length);
}

static bool parse_shell_request(const char *body, char *command,
                                size_t capacity)
{
    json_reader_t reader;
    json_token_t token;
    char scratch[256];
    json_span_t input = {body, strlen(body)};
    bool found = false;
    if (json_reader_init(&reader, scratch, sizeof(scratch)) != JSON_OK ||
        json_reader_feed(&reader, input, true) != JSON_OK ||
        json_read(&reader, &token) != JSON_OK ||
        token.type != JSON_OBJECT_BEGIN)
    {
        return false;
    }
    for (;;)
    {
        if (json_read(&reader, &token) != JSON_OK)
        {
            return false;
        }
        if (token.type == JSON_OBJECT_END)
        {
            return found && json_read(&reader, &token) == JSON_DONE;
        }
        if (token.type != JSON_KEY)
        {
            return false;
        }
        if (json_token_equals(&token, "tx"))
        {
            if (found || json_read(&reader, &token) != JSON_OK ||
                json_token_copy_string(&token, command, capacity) !=
                    JSON_ERROR_NONE)
            {
                return false;
            }
            found = true;
        }
        else if (json_skip_value(&reader) != JSON_OK)
        {
            return false;
        }
        else
        {
            /* Unknown fields do not execute commands. */
        }
    }
}

void handle_post_web_shell(const char *json_body)
{
    char command[256];
    if (json_body == NULL ||
        !parse_shell_request(json_body, command, sizeof(command)))
    {
        http_send_bad_request();
        return;
    }
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }
    (void)json_write_object_begin(&writer);
    web_json_key(&writer, "rx");
    web_shell_capture_begin(&writer);
    size_t length = strlen(command);
    if (length != 0U)
    {
        /* The command buffer bounds this conversion to uint16_t. */
        web_shell_on_rx((const uint8_t *)command, (uint16_t)length);
    }
    int captured = web_shell_capture_end();
    (void)json_write_object_end(&writer);
    if (captured == 0 && json_writer_finish(&writer) == JSON_DONE)
    {
        http_json_reply("{\"pending\":true}");
        return;
    }
    http_json_send(&writer);
}

void handle_get_web_shell(void)
{
    /* Capture is request-scoped: the POST response already carried the
     * command output, background output is never captured. */
    http_json_reply("{\"rx\":\"\"}");
}

/* JSON bodies are built by the bounded common writer. HTTP buffers
 * reserve one NUL byte and are sent only after JSON_DONE. */

/* ============================================================================
 * JSON CONFIGURATION HANDLERS
 * ============================================================================
 */

/**
 * @brief Handle GET /r?deviceConfig - Read device configuration as JSON
 */
static void write_device_identity(json_writer_t *writer,
                                  const modem_config_t *config)
{
    /* RO Fields */
    web_json_uint32(writer, "SeriNumarasi", (uint32_t)(config->serial_number));
    web_json_object(writer, "CihazKoordinati");
    web_json_string(writer, "MCC", config->coordinates.mcc);
    web_json_string(writer, "MNC", config->coordinates.mnc);
    web_json_string(writer, "LAC", config->coordinates.lac);
    web_json_string(writer, "CI", config->coordinates.ci);
    (void)json_write_object_end(writer);
    /* TODO: Convert epoch to human-readable date format */
    web_json_uint32(writer, "UretimTarihi",
                    (uint32_t)(config->production_date));
    web_json_uint32(writer, "LifeTime", modem_config_get_lifetime());

    /* Son resetten bu yana gecen sure (saniye) - bsp_get_run_time uzerinden
     * ayri cagri (shell'deki "Run Time" satiri ile ayni kaynak). NVRAM'de
     * birikmez; tick acilista sifirdan baslar, ~49.7 gunde sarar. */
    web_json_uint32(writer, "RunTime",
                    (uint32_t)((unsigned)(bsp_get_run_time() / 1000U)));

    fw_info_t fw = *boot_get_installed_fw_info();

    (void)json_write_key(writer, web_json_span("ModemYazilimVeriyonu"));
    {
        char text[80];
        (void)xsnprintf(
            text, sizeof(text), "v%u.%u.%u (%04u-%02u-%02u %02u:%02u:%02u %s)",
            (unsigned)VERSION_MAJOR, (unsigned)VERSION_MINOR,
            (unsigned)VERSION_PATCH, (unsigned)fw.year, (unsigned)fw.month,
            (unsigned)fw.day, (unsigned)fw.hour, (unsigned)fw.minute,
            (unsigned)fw.second, (const char *)fw.short_commit_hash);
        (void)json_write_string(writer, web_json_span(text));
    }

    /* Kurulum zamani: ham epoch sayisi (UI fmtEpoch ile cevirir; 0 ->
     * '-----'). Bootloader bu alani henuz RTC ile doldurmuyor. */
    web_json_uint32(writer, "KurulumTarihi",
                    (uint32_t)((unsigned long)fw.installation_date));

    (void)json_write_key(writer, web_json_span("RFYazilimVeriyonu"));
    {
        char text[80];
        (void)xsnprintf(
            text, sizeof(text), "v%u.%u.%u", config->rf_firmware_version[0],
            config->rf_firmware_version[1], config->rf_firmware_version[2]);
        (void)json_write_string(writer, web_json_span(text));
    }
}

void handle_get_device_config_json(void)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    CSLOG("[HTTP] GET /r?deviceConfig - Reading device config\r\n");
    const modem_config_t *config = get_device_config();
    if (!config)
    {
        CSLOG_ERR("[HTTP] ERROR: Config not available\r\n");
        http_send_error(500, "Config not available");
        return;
    }

    (void)json_write_object_begin(&writer);

    write_device_identity(&writer, config);

    /* RW Fields */
    web_json_uint32(&writer, "WebArayuzuPortu",
                    (uint32_t)(config->web_interface_port));
    web_json_uint32(&writer, "SimKartPin", (uint32_t)(config->sim_card_pin));
    web_json_string(&writer, "SimKartAPN", config->apn.apn);
    web_json_string(&writer, "SimKartAPNUsername", config->apn.user_name);
    web_json_string(&writer, "SimKartAPNSifresi", config->apn.user_pass);
    web_json_string(&writer, "NtpServer", config->ntp_server);
    web_json_uint32(&writer, "NtpServerPortu",
                    (uint32_t)(config->ntp_server_port));
    web_json_uint32(&writer, "Time", (uint32_t)(rtc_get_epoch()));
    web_json_int32(&writer, "TimeZone", (int32_t)(config->time_zone));
    web_json_bool(&writer, "PreserveOutputStates",
                  nvram_retain_output_states());
    web_json_uint32(&writer, "PeriyodikModemResetPeriyodu",
                    (uint32_t)(config->periodic_modem_reset_period));
    web_json_uint32(&writer, "WebIlkVeriZamanAsimi",
                    (uint32_t)(config->web_first_data_timeout_sec));
    web_json_uint32(&writer, "WebBostaKalmaZamanAsimi",
                    (uint32_t)(config->web_idle_timeout_sec));
    web_json_uint32(&writer, "IEC104IlkVeriZamanAsimi",
                    (uint32_t)(config->iec104_first_data_timeout_sec));
    web_json_uint32(&writer, "IEC104BostaKalmaZamanAsimi",
                    (uint32_t)(config->iec104_idle_timeout_sec));
    web_json_uint32(&writer, "DevreyeAlinmaZamani",
                    (uint32_t)(config->commissioning_time));

    (void)json_write_object_end(&writer);
    CSLOG("[HTTP] JSON response size: %u bytes\r\n", (unsigned int)writer.used);
    if (writer.used >= writer.capacity)
    {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! writer.used=%u, "
                  "buf_size=%u\r\n",
                  (unsigned int)writer.used, (unsigned int)writer.capacity);
    }
    http_json_send(&writer);
}

/* Use the existing RTC service so calendar and epoch stay consistent. */
static bool sync_device_time(uint32_t epoch)
{
    datetime_t calendar;
    dt_conv_from_epoch(epoch, &calendar);
    if (calendar.date.year < 2000U || calendar.date.year > 2099U)
    {
        return false;
    }
    const rtc_t clock_time = {.second = calendar.time.second,
                              .minute = calendar.time.minute,
                              .hour = calendar.time.hour,
                              .day = calendar.date.day,
                              .month = calendar.date.month,
                              .year = (uint8_t)(calendar.date.year - 2000U)};
    return rtc_sync(&clock_time);
}

/**
 * @brief Handle POST /w?deviceConfig - Write device configuration from JSON
 */
void handle_post_device_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?deviceConfig - Updating device config\r\n");

    if (!json_body)
    {
        CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }

    CSLOG("[HTTP] JSON body: %.100s...\r\n", json_body);

    modem_config_t config;

    /* Load current config as base - preserves RO fields and non-updated values
     */
    config = *get_device_config();

    device_config_options_t options = {.retain_outputs =
                                           nvram_retain_output_states()};
    if (!parse_device_config(json_body, &config, &options))
    {
        CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }

    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] SimKartAPN: %s\r\n", config.apn.apn);

    /* Save config via data_model */
    if (!nvram_set_output_retention(options.retain_outputs))
    {
        http_send_error(500, "Configuration is busy");
        return;
    }
    int res = set_device_config(&config);
    const bool clock_failed =
        res == 0 && options.has_time && !sync_device_time(config.time);

    elog_log_config_change(ELOG_CONFIG_DEVICE_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "device",
                           (res == 0 && !clock_failed));
    CSLOG("[HTTP] Config %s to data_model\r\n",
          (res == 0) ? "saved" : "save FAILED (kept in RAM)");

    if (res != 0)
    {
        http_send_error(500, "Failed to save configuration");
        return;
    }

    if (clock_failed)
    {
        http_send_error(500, "Configuration saved but clock update failed");
        return;
    }

    const char *response_body =
        "{\"message\":\"Device configuration saved\",\"success\":true}";
    http_send_json(response_body, (int)strlen(response_body));
}

/**
 * @brief Handle GET /r?boardStatus - Read board status as JSON
 */
void handle_get_board_status_json(void)
{
    CSLOG("[HTTP] GET /r?boardStatus - Reading board status\r\n");

    const system_status_t *status = system_status_get();

    if (!status)
    {
        CSLOG_ERR("[HTTP] ERROR: Board status not available\r\n");
        http_send_error(500, "Board status not available");
        return;
    }

    char *buf = handler_state.tx_buffer;
    const size_t buf_size = (size_t)handler_state.tx_buffer_size;
    size_t pos = 0U;
    if (!system_status_json_build(status, buf, buf_size, &pos))
    {
        http_send_error(500, "Board status response buffer too small");
        return;
    }

    CSLOG("[HTTP] Board status JSON size: %u bytes\r\n", (unsigned int)pos);

    /* Buffer overflow check */
    if (pos >= buf_size - 1U)
    {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%u, buf_size=%u\r\n",
                  (unsigned int)pos, (unsigned int)buf_size);
    }

    http_send_json(buf, (int)pos);
}

/**
 * @brief Handle GET /syslogs?offset=X&limit=Y - Read system logs as JSON
 */
void handle_get_syslogs_json(void)
{
    CSLOG("[HTTP] GET /syslogs - Reading system logs\r\n");

    /* Parse query parameters */
    uint32_t offset = 0;
    uint32_t limit = 30; /* Default: 30 records per page */

    if (handler_state.current_query_string)
    {
        const char *offset_ptr =
            strstr(handler_state.current_query_string, "offset=");
        if (offset_ptr)
        {
            offset = (uint32_t)xstrtoi(offset_ptr + 7);
        }

        const char *limit_ptr =
            strstr(handler_state.current_query_string, "limit=");
        if (limit_ptr)
        {
            limit = (uint32_t)xstrtoi(limit_ptr + 6);
            if (limit > 100)
            {
                limit = 100; /* Cap at 100 to prevent buffer overflow */
            }
        }
    }

    /* Exact stored count (ring scan): with the deferred-erase policy the
     * ring holds up to every sector's worth of records; torn holes reduce
     * it. Used for pagination windowing and the "t" JSON field. */
    uint32_t available_entries = elog_get_stored_count();

    CSLOG("[HTTP] System Logs - offset=%lu, limit=%lu, available=%lu\r\n",
          offset, limit, available_entries);

    /* A past-the-end request serves an empty page, never a wrap-around
     * to the newest records: a client would append them as if they were
     * unread records (duplicate entries on the UI). */
    if (offset > available_entries)
    {
        offset = available_entries;
    }

    /* Calculate actual records to return */
    uint32_t end_idx = offset + limit;
    if (end_idx > available_entries)
    {
        end_idx = available_entries;
    }
    uint32_t count = end_idx - offset;

    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }
    (void)json_write_object_begin(&writer);
    web_json_key(&writer, "recs");
    (void)json_write_string_begin(&writer);
    uint32_t emitted = 0U;
    uint32_t skip = offset;
    bool has_room = true;
    while (emitted < count && has_room)
    {
        elog_entry_t chunk[8];
        uint32_t chunk_len = 0U;
        uint32_t want = count - emitted;
        if (want > sizeof(chunk) / sizeof(chunk[0]))
        {
            want = sizeof(chunk) / sizeof(chunk[0]);
        }
        if (elog_read_recent(skip, want, chunk, &chunk_len) != 0 ||
            chunk_len == 0U)
        {
            break;
        }
        for (uint32_t index = 0U; index < chunk_len; ++index)
        {
            const elog_entry_t *entry = &chunk[index];
            datetime_t date;
            dt_conv_from_epoch(entry->timestamp, &date);
            char line[256];
            size_t length = xsnprintf(
                line, sizeof(line),
                "%s#%lu TS:%04u-%02u-%02u %02u:%02u:%02u LVL:%u CODE:%s "
                "INFO:%s",
                emitted != 0U ? "\n" : "", offset + emitted + 1U,
                date.date.year, date.date.month, date.date.day, date.time.hour,
                date.time.minute, date.time.second, entry->level,
                elog_code_to_string((elog_code_t)entry->code),
                elog_info_to_text(entry));
            if (length >= sizeof(line) - 1U)
            {
                http_send_error(500, "System log entry exceeds line buffer");
                return;
            }
            /* Count with the same core, including all escape expansion. */
            if (!web_json_text_fits(&writer, line, 32U))
            {
                has_room = false;
                break;
            }
            web_json_text(&writer, line);
            ++emitted;
        }
        skip += chunk_len;
    }
    if (emitted == 0U && count != 0U && !has_room)
    {
        http_send_error(500, "System log response buffer too small");
        return;
    }
    (void)json_write_string_end(&writer);
    web_json_uint32(&writer, "t", available_entries);
    (void)json_write_object_end(&writer);
    http_json_send(&writer);
}

/**
 * @brief Handle GET /r?iecConfig - Read IEC104 config as JSON
 */
static void write_iec_lines(json_writer_t *writer)
{
    static const struct
    {
        const char *name;
        size_t offset;
    } fields[] = {
        {"TemporaryFaultBase", offsetof(iec104_line_config_t, temporary_fault)},
        {"PermanentFaultBase", offsetof(iec104_line_config_t, permanent_fault)},
        {"IOA_R_AnlikAkim",
         offsetof(iec104_line_config_t, anlik_akim[PHASE_L1])},
        {"IOA_S_AnlikAkim",
         offsetof(iec104_line_config_t, anlik_akim[PHASE_L2])},
        {"IOA_T_AnlikAkim",
         offsetof(iec104_line_config_t, anlik_akim[PHASE_L3])},
        {"IOA_R_EnerjiVarYok",
         offsetof(iec104_line_config_t, enerji_varyok[PHASE_L1])},
        {"IOA_S_EnerjiVarYok",
         offsetof(iec104_line_config_t, enerji_varyok[PHASE_L2])},
        {"IOA_T_EnerjiVarYok",
         offsetof(iec104_line_config_t, enerji_varyok[PHASE_L3])},
        {"IOA_R_YukAkimiVarYok",
         offsetof(iec104_line_config_t, yuk_akimi_varyok[PHASE_L1])},
        {"IOA_S_YukAkimiVarYok",
         offsetof(iec104_line_config_t, yuk_akimi_varyok[PHASE_L2])},
        {"IOA_T_YukAkimiVarYok",
         offsetof(iec104_line_config_t, yuk_akimi_varyok[PHASE_L3])},
        {"IOA_R_RfhabVarYok",
         offsetof(iec104_line_config_t, rf_haberlesme_varyok[PHASE_L1])},
        {"IOA_S_RfhabVarYok",
         offsetof(iec104_line_config_t, rf_haberlesme_varyok[PHASE_L2])},
        {"IOA_T_RfhabVarYok",
         offsetof(iec104_line_config_t, rf_haberlesme_varyok[PHASE_L3])},
        {"IOA_R_TripFailed",
         offsetof(iec104_line_config_t, trip_failed[PHASE_L1])},
        {"IOA_S_TripFailed",
         offsetof(iec104_line_config_t, trip_failed[PHASE_L2])},
        {"IOA_T_TripFailed",
         offsetof(iec104_line_config_t, trip_failed[PHASE_L3])},
    };
    web_json_object(writer, "Hatlar");
    web_json_array(writer, "inUse");
    for (uint32_t index = 0U; index < MAX_ARRAYS; ++index)
    {
        const iec104_line_config_t *line = iec104_get_line_config(index);
        (void)json_write_bool(writer, line != NULL && line->in_use);
    }
    (void)json_write_array_end(writer);
    for (size_t field = 0U; field < sizeof(fields) / sizeof(fields[0]); ++field)
    {
        web_json_array(writer, fields[field].name);
        for (uint32_t index = 0U; index < MAX_ARRAYS; ++index)
        {
            const iec104_line_config_t *line = iec104_get_line_config(index);
            ioa_3byte_t address = {0};
            if (line != NULL)
            {
                const uint8_t *data = (const uint8_t *)line;
                memcpy(&address, data + fields[field].offset, sizeof(address));
            }
            (void)json_write_uint32(writer,
                                    iec104_ioa_3byte_to_uint32(address));
        }
        (void)json_write_array_end(writer);
    }
    (void)json_write_object_end(writer);
}

void handle_get_iec_config_json(void)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    CSLOG("[HTTP] GET /r?iecConfig - Reading IEC config\r\n");

    const iec104_config_t *config = iec104_config_get();
    if (!config)
    {
        CSLOG_ERR("[HTTP] ERROR: IEC config not available\r\n");
        http_send_error(500, "IEC config not available");
        return;
    }

    // Convert IP from uint32_t to string
    uint32_t ip = config->scada_ip_address;
    uint8_t ip_a = (uint8_t)((ip >> 24) & 0xFFU);
    uint8_t ip_b = (uint8_t)((ip >> 16) & 0xFFU);
    uint8_t ip_c = (uint8_t)((ip >> 8) & 0xFFU);
    uint8_t ip_d = (uint8_t)(ip & 0xFFU);

    (void)json_write_object_begin(&writer);
    (void)json_write_key(&writer, web_json_span("ScadaIPAdresi"));
    {
        char text[80];
        (void)xsnprintf(text, sizeof(text), "%u.%u.%u.%u", (unsigned int)ip_a,
                        (unsigned int)ip_b, (unsigned int)ip_c,
                        (unsigned int)ip_d);
        (void)json_write_string(&writer, web_json_span(text));
    }
    web_json_uint32(&writer, "PeriodicSend",
                    (uint32_t)(config->periodical_send_interval));
    web_json_uint32(&writer, "Port", (uint32_t)(config->scada_port));
    web_json_uint32(&writer, "T0", (uint32_t)(config->t0_max));
    web_json_uint32(&writer, "T1", (uint32_t)(config->t1_max));
    web_json_uint32(&writer, "T2", (uint32_t)(config->t2_max));
    web_json_uint32(&writer, "T3", (uint32_t)(config->t3_max));
    web_json_uint32(&writer, "K", (uint32_t)(config->k_max));
    web_json_uint32(&writer, "W", (uint32_t)(config->w_max));
    web_json_uint32(&writer, "OriginatorAddr",
                    (uint32_t)(config->originator_address));
    web_json_uint32(&writer, "CommonAddr", (uint32_t)(config->common_address));
    web_json_bool(&writer, "SBO", config->is_sbo_active);
    web_json_uint32(&writer, "SBOTimeout",
                    (uint32_t)(config->sbo_execute_timeout));

    web_json_uint32(
        &writer, "AkuUyarisi",
        (uint32_t)(iec104_ioa_3byte_to_uint32(config->ioa_aku_uyarisi)));
    web_json_uint32(
        &writer, "ModemReset",
        (uint32_t)(iec104_ioa_3byte_to_uint32(config->ioa_modem_reset)));

    write_iec_lines(&writer);
    (void)json_write_object_end(&writer);

    CSLOG("[HTTP] IEC config JSON size: %u bytes\r\n",
          (unsigned int)writer.used);

    /* Buffer overflow check */
    if (writer.used >= writer.capacity)
    {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! writer.used=%u, "
                  "buf_size=%u\r\n",
                  (unsigned int)writer.used, (unsigned int)writer.capacity);
    }

    http_json_send(&writer);
}

/**
 * @brief Handle POST /w?iecConfig - Write IEC104 configuration from JSON
 */
void handle_post_iec_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?iecConfig - Updating IEC config\r\n");

    if (!json_body)
    {
        CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }

    CSLOG("[HTTP] JSON body length: %u bytes\r\n",
          (unsigned int)strlen(json_body));

    // Create local config from current NVRAM state
    jiec_config_t config = {0};
    const iec104_config_t *nvram_config = iec104_config_get();
    if (!nvram_config)
    {
        CSLOG_ERR("[HTTP] ERROR: Cannot read current config\r\n");
        http_send_error(500, "Config read error");
        return;
    }

    // Convert NVRAM config to JSON config format (for partial updates)
    config.periodical_send_interval = nvram_config->periodical_send_interval;
    uint32_t ip = nvram_config->scada_ip_address;
    xsnprintf(config.scada_ip_address, sizeof(config.scada_ip_address),
              "%u.%u.%u.%u", (unsigned int)((ip >> 24) & 0xFFU),
              (unsigned int)((ip >> 16) & 0xFFU),
              (unsigned int)((ip >> 8) & 0xFFU), (unsigned int)(ip & 0xFFU));
    config.scada_port = nvram_config->scada_port;
    config.t0_timeout = (uint8_t)nvram_config->t0_max;
    config.t1_timeout = (uint8_t)nvram_config->t1_max;
    config.t2_timeout = (uint8_t)nvram_config->t2_max;
    config.t3_timeout = nvram_config->t3_max;
    config.k_max = nvram_config->k_max;
    config.w_max = nvram_config->w_max;
    config.sbo_timeout = nvram_config->sbo_execute_timeout;
    config.sbo_active = nvram_config->is_sbo_active != 0;
    config.originator_address = nvram_config->originator_address;
    config.common_address = nvram_config->common_address;
    config.ioa_aku_uyarisi =
        iec104_ioa_3byte_to_uint32(nvram_config->ioa_aku_uyarisi);
    config.ioa_modem_reset =
        iec104_ioa_3byte_to_uint32(nvram_config->ioa_modem_reset);

    // Convert line configs
    for (uint32_t i = 0U; i < MAX_ARRAYS; i++)
    {
        const iec104_line_config_t *line = iec104_get_line_config(i);
        if (NULL != line)
        {
            config.line.temporary_fault_base[i] =
                iec104_ioa_3byte_to_uint32(line->temporary_fault);
            config.line.permanent_fault_base[i] =
                iec104_ioa_3byte_to_uint32(line->permanent_fault);
        }
        if (line && line->in_use)
        {
            config.line.in_use[i] = true;
            config.line.ioa_r_anlik_akim[i] =
                iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L1]);
            config.line.ioa_s_anlik_akim[i] =
                iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L2]);
            config.line.ioa_t_anlik_akim[i] =
                iec104_ioa_3byte_to_uint32(line->anlik_akim[PHASE_L3]);
            config.line.ioa_r_enerji_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L1]);
            config.line.ioa_s_enerji_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L2]);
            config.line.ioa_t_enerji_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->enerji_varyok[PHASE_L3]);
            config.line.ioa_r_yuk_akimi_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->yuk_akimi_varyok[PHASE_L1]);
            config.line.ioa_s_yuk_akimi_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->yuk_akimi_varyok[PHASE_L2]);
            config.line.ioa_t_yuk_akimi_varyok[i] =
                iec104_ioa_3byte_to_uint32(line->yuk_akimi_varyok[PHASE_L3]);
            config.line.ioa_r_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(
                line->rf_haberlesme_varyok[PHASE_L1]);
            config.line.ioa_s_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(
                line->rf_haberlesme_varyok[PHASE_L2]);
            config.line.ioa_t_rfhab_varyok[i] = iec104_ioa_3byte_to_uint32(
                line->rf_haberlesme_varyok[PHASE_L3]);
            config.line.ioa_r_trip_failed[i] =
                iec104_ioa_3byte_to_uint32(line->trip_failed[PHASE_L1]);
            config.line.ioa_s_trip_failed[i] =
                iec104_ioa_3byte_to_uint32(line->trip_failed[PHASE_L2]);
            config.line.ioa_t_trip_failed[i] =
                iec104_ioa_3byte_to_uint32(line->trip_failed[PHASE_L3]);
        }
        else
        {
            config.line.in_use[i] = false;
        }
    }

    if (!parse_iec_config(json_body, &config))
    {
        CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        const char *error = json_config_get_iec_address_error();
        http_send_error(400, ('\0' != error[0]) ? error
                                                : "Invalid IEC configuration");
        return;
    }

    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] ScadaIPAdresi: %s\r\n", config.scada_ip_address);
    CSLOG("[HTTP] ScadaPort: %u\r\n", config.scada_port);
    CSLOG("[HTTP] PeriyodikGonderimZamani: %lu\r\n",
          config.periodical_send_interval);

    /* Save config to persistent storage */
    int res = set_iec_config(&config);

    elog_log_config_change(ELOG_CONFIG_IEC104_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "iec104", (res == 0));
    CSLOG("[HTTP] IEC config %s\r\n",
          (res == 0) ? "saved" : "save FAILED (kept in RAM)");

    if (res != 0)
    {
        http_send_error(500, "Failed to save configuration");
        return;
    }

    const char *response_body =
        "{\"message\":\"IEC104 configuration saved\",\"success\":true}";
    http_send_json(response_body, (int)strlen(response_body));
}

/**
 * @brief Handle GET /r?modbusConfigs - Read Modbus configuration as JSON
 */
void handle_get_modbus_config_json(void)
{
    const modbus_configs_t *config = modbus_config_get();
    if (config == NULL)
    {
        http_send_error(500, "Modbus config not available");
        return;
    }
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }
    (void)json_write_object_begin(&writer);
    web_json_uint32(&writer, "CihazID", config->device_addr);
    web_json_uint32(&writer, "StopBits", config->stop_bits);
    web_json_uint32(&writer, "SonHataKodu", config->last_error_code);
    web_json_uint32(&writer, "SonHataZamani",
                    modbus_config_get_last_error_time());
    web_json_uint32(&writer, "BaudRate", config->baud_rate);
    web_json_key(&writer, "RegisterMap");
    modbus_map_json_write(&writer);
    (void)json_write_object_end(&writer);
    http_json_send(&writer);
}

/**
 * @brief Handle POST /w?modbusConfigs - Write Modbus configuration from JSON
 */
void handle_post_modbus_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?modbusConfigs - Updating Modbus config\r\n");

    if (!json_body)
    {
        CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }

    CSLOG("[HTTP] JSON body length: %u bytes\r\n",
          (unsigned int)strlen(json_body));

    // Create local config from current NVRAM state
    jmodbus_configs_t config = {0};
    const modbus_configs_t *nvram_config = modbus_config_get();
    if (!nvram_config)
    {
        CSLOG_ERR("[HTTP] ERROR: Cannot read current config\r\n");
        http_send_error(500, "Config read error");
        return;
    }

    // Convert NVRAM config to JSON config format (for partial updates)
    config.device_addr = nvram_config->device_addr;
    config.stop_bits = nvram_config->stop_bits;
    config.last_error_code = nvram_config->last_error_code;
    config.baud_rate = nvram_config->baud_rate;

    if (!parse_modbus_config(json_body, &config))
    {
        CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }

    CSLOG("[HTTP] JSON parsed successfully\r\n");
    CSLOG("[HTTP] CihazID: %u\r\n", config.device_addr);
    CSLOG("[HTTP] BaudRate: %lu\r\n", config.baud_rate);

    int res = set_modbus_config(&config);

    elog_log_config_change(ELOG_CONFIG_MODBUS_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "modbus", (res == 0));

    CSLOG("[HTTP] Modbus config %s\r\n", res == 0 ? "saved" : "save failed");

    const char *response_body =
        res == 0
            ? "{\"message\":\"Modbus configuration saved\",\"success\":true}"
            : "{\"message\":\"Failed to save Modbus "
              "configuration\",\"success\":false}";

    http_send_json(response_body, (int)strlen(response_body));
}

/**
 * @brief Handle GET /discovery/rf - sadece kesif listesi (Unassigned).
 *
 * Tam config degil; yalnizca atanmamis EUI-64 listesi doner.
 * Hafif yanit: Refresh butonu icin tasarlandi (GSM trafigi tasarrufu).
 */
void handle_get_rf_discovery_json(void)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    uint8_t count;
    uint8_t k;
    int emitted = 0;
    char hex[RF_EUI64_HEX_LEN];
    uint8_t eui[RF_EUI64_LEN];

    CSLOG("[HTTP] GET /discovery/rf\r\n");

    count = rf_discovery_get_count();

    (void)json_write_object_begin(&writer);
    web_json_bool(&writer, "success", true);
    web_json_object(&writer, "data");
    web_json_array(&writer, "Unassigned");

    for (k = 0U; k < count; k++)
    {
        bool assigned = false;
        int i;

        if (!rf_discovery_get_device(k, eui))
        {
            break;
        }

        for (i = 0; i < MAX_POWER_LINE_COUNT; i++)
        {
            const rf_feeder_t *feeder = rf_store_get((feeder_id_t)i);

            if (feeder != NULL)
            {
                if ((memcmp(eui, feeder->r_eui64, RF_EUI64_LEN) == 0) ||
                    (memcmp(eui, feeder->s_eui64, RF_EUI64_LEN) == 0) ||
                    (memcmp(eui, feeder->t_eui64, RF_EUI64_LEN) == 0))
                {
                    assigned = true;
                    break;
                }
            }
        }

        if (assigned)
        {
            continue;
        }

        rf_eui64_to_hex(eui, hex);
        (void)json_write_object_begin(&writer);
        web_json_string(&writer, "EUI64", hex);
        web_json_int32(&writer, "FiderID", 0);
        (void)json_write_object_end(&writer);
        emitted++;
    }

    (void)json_write_array_end(&writer);
    (void)json_write_object_end(&writer);
    (void)json_write_object_end(&writer);

    http_json_send(&writer);
}

/**
 * @brief Handle GET /r?ayiriciRFConfig - Read RF configuration as JSON (14
 * array format)
 */
void handle_get_rf_config_json(void)
{
    char *buf = handler_state.tx_buffer;
    int buf_size = handler_state.tx_buffer_size;
    int pos;

    CSLOG("[HTTP] GET /r?ayiriciRFConfig - Reading RF config\r\n");

    /* Govde uretimi tablo-tabanli rf_json modulundedir (kod hafizasi). */
    pos = rf_json_config_build(buf, buf_size);
    if (pos <= 0)
    {
        http_send_error(500,
                        "RF config exceeds response buffer or invalid data");
        return;
    }
    CSLOG("[HTTP] RF config JSON size: %d bytes\r\n", pos);

    if (pos >= (buf_size - 1))
    {
        CSLOG_ERR("[HTTP] WARNING: Buffer nearly full! pos=%d, buf_size=%d\r\n",
                  pos, buf_size);
    }

    http_send_json(buf, pos);
}

void handle_get_rf_group_status_json(void)
{
    size_t length;

    if ((0 >= handler_state.tx_buffer_size) ||
        !rf_group_status_json_build(handler_state.tx_buffer,
                                    (size_t)handler_state.tx_buffer_size,
                                    &length))
    {
        http_send_error(500, "RF group status buffer too small");
        return;
    }
    http_send_json(handler_state.tx_buffer, (int)length);
}

void handle_post_rf_apply(const char *suffix)
{
    (void)suffix;
    http_send_error(410, "Use RF Save to save and apply all active feeders");
}

void handle_post_rf_abort(void)
{
    if (!rf_apply_abort())
    {
        http_send_error(409, "RF abort not started: check current group state");
        return;
    }
    http_json_reply("{\"started\":true}");
}

/* Release the form/log snapshot before NVRAM/Flash persistence. */
static __attribute__((noinline)) bool parse_rf_settings(const char *json_body)
{
    jayirici_rf_config_t config;

    if (!parse_rf_config(json_body, &config))
    {
        return false;
    }
    CSLOG("[HTTP] JSON parsed successfully\r\n");
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; index++)
    {
        if (config.in_use[index])
        {
            CSLOG("[HTTP] Line %u: HatID=%u, ZoneID=%u, "
                  "EUI-64=[%s, %s, %s]\r\n",
                  (unsigned)(index + 1U), config.hat_id[index],
                  config.zone_id[index], config.r_eui64[index],
                  config.s_eui64[index], config.t_eui64[index]);
        }
    }
    return true;
}

/**
 * @brief Save desired RF settings and apply all active feeders in sequence.
 */
void handle_post_rf_config_json(const char *json_body)
{
    CSLOG("[HTTP] POST /w?ayiriciRFConfig - Updating RF config\r\n");

    if (!json_body)
    {
        CSLOG_ERR("[HTTP] ERROR: No JSON body\r\n");
        http_send_bad_request();
        return;
    }

    if (!rf_apply_can_save())
    {
        http_send_error(409, "RF operation active or unresolved; save blocked");
        return;
    }

    CSLOG("[HTTP] JSON body length: %u bytes\r\n",
          (unsigned int)strlen(json_body));

    /* Staging: parse yarida kalirsa yarim yazma kalici store'a gecmez. */
    if (!rf_store_stage_begin())
    {
        CSLOG_ERR("[HTTP] ERROR: RF staging busy\r\n");
        http_send_error(500, "RF config staging busy");
        return;
    }

    // Parse JSON - writes go to the staging copy via rf_store_get_mutable()
    if (!parse_rf_settings(json_body))
    {
        rf_store_stage_abort();
        CSLOG_ERR("[HTTP] ERROR: JSON parse failed\r\n");
        http_send_error(400, "JSON parse error");
        return;
    }

    /* Save the candidate before publishing it or starting RF operations. */
    const rf_apply_save_result_t result = rf_apply_save();

    if (RF_APPLY_SAVE_MAINTENANCE == result)
    {
        rf_store_stage_abort();
        http_send_error(409, "Assigned device relocation requires the R2.3 service procedure; configuration unchanged");
        return;
    }

    if (RF_APPLY_SAVE_BUSY == result)
    {
        rf_store_stage_abort();
        http_send_error(
            409,
            "Pending inventory operation; retry saved configuration first");
        return;
    }

    if (RF_APPLY_SAVE_PRIMARY_ONLY == result)
    {
        elog_log_config_change(ELOG_CONFIG_RF_CHANGED, ELOG_SOURCE_WEB,
                               gsm_get_web_client_ip(), "rf", true);
        const char *body = "{\"success\":true,\"warning\":\"backup_failed\"}";

        http_send_json(body, (int)strlen(body));
        return;
    }
    if (RF_APPLY_SAVE_OK != result)
    {
        rf_store_stage_abort();
        CSLOG_ERR("[HTTP] ERROR: Failed to sync RF config to storage\r\n");
        elog_log_config_change(ELOG_CONFIG_RF_CHANGED, ELOG_SOURCE_WEB,
                               gsm_get_web_client_ip(), "rf", false);
        http_send_error(500, "Failed to save configuration");
        return;
    }

    CSLOG("[HTTP] RF config saved\r\n");
    elog_log_config_change(ELOG_CONFIG_RF_CHANGED, ELOG_SOURCE_WEB,
                           gsm_get_web_client_ip(), "rf", true);

    const char *response_body =
        "{\"message\":\"RF configuration saved\",\"success\":true}";
    http_send_json(response_body, (int)strlen(response_body));
}

/**
 * @brief Parse the confirmed alarm identity; user and source are server-owned.
 */
static bool parse_rf_alarm_ack(const char *body, rf_alarm_ack_request_t *out)
{
    if ((body == NULL) || (out == NULL))
    {
        return false;
    }
    static const char *const keys[] = {"Line", "Phase", "Eui64", "Revision", "ConfirmOffline"};
    json_reader_t reader;
    json_token_t token;
    char scratch[64];
    rf_alarm_ack_request_t candidate = {0};
    uint8_t seen = 0U;
    if ((json_reader_init(&reader, scratch, sizeof(scratch)) != JSON_OK) ||
        (json_reader_feed(&reader, (json_span_t){body, strlen(body)}, true) != JSON_OK) ||
        (json_read(&reader, &token) != JSON_OK) || (token.type != JSON_OBJECT_BEGIN))
    {
        return false;
    }
    for (;;)
    {
        if (json_read(&reader, &token) != JSON_OK)
        {
            return false;
        }
        if (token.type == JSON_OBJECT_END)
        {
            if ((seen != 31U) || (candidate.line < 1U) || (candidate.line > MAX_POWER_LINE_COUNT) ||
                (candidate.phase < 1U) || (candidate.phase > PHASE_MAX) || (candidate.revision == 0U) ||
                (json_read(&reader, &token) != JSON_DONE))
            {
                return false;
            }
            candidate.line--;
            candidate.phase--;
            *out = candidate;
            return true;
        }
        if (token.type != JSON_KEY)
        {
            return false;
        }
        size_t field = 0U;
        while ((field < sizeof(keys) / sizeof(keys[0])) && !json_token_equals(&token, keys[field]))
        {
            field++;
        }
        if ((field >= sizeof(keys) / sizeof(keys[0])) || ((seen & (1U << field)) != 0U) ||
            (json_read(&reader, &token) != JSON_OK))
        {
            return false;
        }
        seen |= (uint8_t)(1U << field);
        if (field == 2U)
        {
            char hex[RF_EUI64_HEX_LEN];
            if ((json_token_copy_string(&token, hex, sizeof(hex)) != JSON_ERROR_NONE) ||
                (strlen(hex) != RF_EUI64_LEN * 2U) ||
                !rf_eui64_from_hex(candidate.eui64, hex))
            {
                return false;
            }
        }
        else if (field == 4U)
        {
            if (token.type != JSON_BOOLEAN)
            {
                return false;
            }
            candidate.confirm_offline = token.boolean;
        }
        else if (field == 3U)
        {
            if (json_token_uint32(&token, &candidate.revision) != JSON_ERROR_NONE)
            {
                return false;
            }
        }
        else if (json_token_uint8(&token, (field == 0U) ? &candidate.line : &candidate.phase) != JSON_ERROR_NONE)
        {
            return false;
        }
        else
        {
            /* Numeric field accepted. */
        }
    }
}

void handle_post_rf_alarm_ack(const char *body)
{
    if (!http_handlers_is_admin())
    {
        http_send_error(403, "Admin role required");
        return;
    }
    rf_alarm_ack_request_t request;
    if (!parse_rf_alarm_ack(body, &request))
    {
        http_send_error(400, "Invalid alarm acknowledgement");
        return;
    }
    const rf_alarm_ack_result_t result = rf_alarm_acknowledge(&request);
    switch (result)
    {
        case RF_ALARM_ACK_OK:
            http_send_json("{\"success\":true}", 16);
            break;
        case RF_ALARM_ACK_OFFLINE:
            http_send_error(428, "No fresh LIVE; explicit offline confirmation required");
            break;
        case RF_ALARM_ACK_STORAGE:
            http_send_error(503, "Acknowledgement not stored; alarm unchanged");
            break;
        case RF_ALARM_ACK_IOA_CONFLICT:
            http_send_error(409, "IEC104 configuration overlaps reserved acknowledgement IOAs");
            break;
        case RF_ALARM_ACK_BUSY:
            http_send_error(409, "Alarm delivery pending; refresh and retry");
            break;
        default:
            http_send_error(409, "Alarm changed; refresh before acknowledging");
            break;
    }
}

/** Send one RF line or all lines when line_filter is -1. */
static void send_rf_monitor_json(int line_filter)
{
    if ((-1 > line_filter) || (MAX_POWER_LINE_COUNT <= line_filter))
    {
        http_send_error(400, "Invalid line index");
        return;
    }
    size_t length = 0U;
    if ((NULL == handler_state.tx_buffer) ||
        (0 >= handler_state.tx_buffer_size))
    {
        http_send_error(500, "RF monitor buffer unavailable");
        return;
    }
    size_t capacity = (size_t)handler_state.tx_buffer_size;

    if (!rf_json_monitor_build(handler_state.tx_buffer, capacity,
                               (int32_t)line_filter, HAL_GetTick(), &length))
    {
        http_send_error(500, "RF monitor response does not fit");
        return;
    }
    /* The handler buffer bounds length to its configured 8192-byte size. */
    http_send_json(handler_state.tx_buffer, (int)length);
}

/**
 * @brief Send RF monitor JSON for a specific line
 *
 * @param line_id Line index (0..MAX_POWER_LINE_COUNT-1)
 */
void handle_get_rf_monitor_line_json(int line_id)
{
    CSLOG("[HTTP] GET RF monitor for line %d\r\n", line_id);
    send_rf_monitor_json(line_id);
}

/**
 * @brief Handle GET /r?ayiriciRFMonitor - Read RF monitor data (read-only)
 *
 * Supports optional ?line=X parameter to fetch single line:
 * - /r?ayiriciRFMonitor -> all lines (0..MAX_POWER_LINE_COUNT-1)
 * - /r?ayiriciRFMonitor&line=0 -> only line 1 (index 0)
 * - /r?ayiriciRFMonitor&line=N -> only line N+1 (index N)
 */
void handle_get_rf_monitor_json(void)
{
    CSLOG("[HTTP] GET /r?ayiriciRFMonitor - Reading RF monitor data\r\n");

    int line_filter = -1; /* -1 = all lines */
    if (handler_state.current_query_string)
    {
        char line_value[8];
        if (http_get_query_param(handler_state.current_query_string, "line",
                                 line_value, sizeof(line_value)))
        {
            line_filter = xstrtoi(line_value);
            CSLOG("[HTTP] Line filter: %d\r\n", line_filter);
            if (line_filter < 0 || line_filter >= MAX_POWER_LINE_COUNT)
            {
                CSLOG_ERR("[HTTP] ERROR: Invalid line index: %d\r\n",
                          line_filter);
                http_send_error(400, "Invalid line index (out of range)");
                return;
            }
        }
    }

    send_rf_monitor_json(line_filter);
}

/* ============================================================================
 * FIRMWARE UPDATE HANDLERS
 * ============================================================================
 */

/* Firmware update state */
static fw_update_callbacks_t fw_callbacks = {0};
static bool fw_callbacks_registered = false;
static struct
{
    uint32_t total_size;
    uint32_t received_bytes;
    uint32_t total_chunks;
    uint32_t received_chunks;
    uint32_t checksum_errors;
    uint32_t retry_count;
    uint32_t start_time;      /* Transfer start timestamp (tick count) */
    uint32_t last_chunk_time; /* Last chunk receive time */
    uint32_t
        first_chunk_size; /* Size of first chunk (for chunk_num calculation) */
    uint32_t file_hash;   /* Fletcher-16 of first 64 bytes - file identity for
                             resume detection */
    uint32_t fw_crc;      /* BL-21: downloaded image app_crc (0 = unbound) */
    uint32_t fw_size;     /* BL-21: downloaded image app_size (0 = unbound) */
    bool in_progress;
    bool complete;
} fw_state = {0};

/* FW Update timeout configuration (in milliseconds) */
#define FW_UPDATE_CHUNK_TIMEOUT_MS 30000  /* 30 seconds between chunks */
#define FW_UPDATE_TOTAL_TIMEOUT_MS 600000 /* 10 minutes total transfer */
#define FW_UPDATE_CHUNK_SIZE 4096 /* Recommended chunk size for client */
#define FW_UPDATE_DEFAULT_CHUNK_SIZE                                           \
    1024 /* Default chunk size if not detected */

/* Firmware version info - can be overridden by application */
static const char *fw_version = NULL;
static const char *fw_build_date = __DATE__;
static const char *fw_hardware = "TROIKA-SCB-v1";

void fw_update_reset(void)
{
    memset(&fw_state, 0, sizeof(fw_state));
}

void fw_update_register_callbacks(const fw_update_callbacks_t *callbacks)
{
    if (callbacks)
    {
        fw_callbacks = *callbacks; /* Copy the structure */
        fw_callbacks_registered = true;
        CSLOG("[FW] Firmware update callbacks registered\r\n");
    }
}

void fw_update_set_version_info(const char *version, const char *build_date,
                                const char *hardware)
{
    if (version)
    {
        fw_version = version;
    }
    if (build_date)
    {
        fw_build_date = build_date;
    }
    if (hardware)
    {
        fw_hardware = hardware;
    }
}

void handle_get_fault_records_json(void)
{
    CSLOG("[HTTP] GET /faults - Reading IEC104 fault records\r\n");

    int feeder = -1;
    if (handler_state.current_query_string)
    {
        const char *fp = strstr(handler_state.current_query_string, "feeder=");
        if (fp)
        {
            feeder = xstrtoi(fp + 7);
        }
    }

    if (feeder < 0 || feeder >= MAX_POWER_LINE_COUNT)
    {
        CSLOG("[HTTP] /faults: invalid feeder=%d\r\n", feeder);
        http_send_bad_request();
        return;
    }

    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    uint8_t tc[3], pc[3];
    for (int ph = 0; ph < 3; ph++)
    {
        tc[ph] = fault_log_get_temp_count((uint8_t)feeder, (uint8_t)ph);
        pc[ph] = fault_log_get_perm_count((uint8_t)feeder, (uint8_t)ph);
    }

    (void)json_write_object_begin(&writer);
    web_json_int32(&writer, "feeder", feeder);

    static const char *const fnames[3][2] = {
        {"L1T", "L1P"}, {"L2T", "L2P"}, {"L3T", "L3P"}};

    for (int ph = 0; ph < 3; ph++)
    {
        for (int ty = 0; ty < 2; ty++)
        {
            uint8_t count = (ty == 0) ? tc[ph] : pc[ph];
            fault_log_type_t log_type =
                (ty == 0) ? FAULT_LOG_TYPE_TEMPORARY : FAULT_LOG_TYPE_PERMANENT;

            web_json_key(&writer, fnames[ph][ty]);
            (void)json_write_string_begin(&writer);

            fault_log_t log;
            for (uint8_t n = 0; n < count; n++)
            {
                if (!fault_log_read_nth((uint8_t)feeder, (uint8_t)ph, log_type,
                                        n, &log))
                {
                    continue;
                }
                uint16_t yr = 2000u + cp56time2a_get_year(&log.tm);
                uint8_t mo = cp56time2a_get_month(&log.tm);
                uint8_t dy = cp56time2a_get_day(&log.tm);
                uint8_t hr = cp56time2a_get_hour(&log.tm);
                uint8_t mn = cp56time2a_get_minute(&log.tm);
                uint8_t sc = cp56time2a_get_second(&log.tm);
                uint16_t ms = cp56time2a_get_ms(&log.tm);

                char line[160];
                (void)xsnprintf(
                    line, sizeof(line),
                    "%u %04u/%02u/%02u %02u:%02u:%02u:%03u %.3fA %ums %u %u\n",
                    (unsigned)(n + 1u), (unsigned)yr, (unsigned)mo,
                    (unsigned)dy, (unsigned)hr, (unsigned)mn, (unsigned)sc,
                    (unsigned)ms, (double)fault_log_current_amps(&log),
                    (unsigned)log.fault_duration_ms,
                    (unsigned)log.info.nominal_current_status,
                    (unsigned)log.info.power_status);
                web_json_text(&writer, line);
            }

            (void)json_write_string_end(&writer);
        }
    }

    web_json_array(&writer, "tc");
    for (size_t phase = 0U; phase < 3U; ++phase)
    {
        (void)json_write_uint32(&writer, tc[phase]);
    }
    (void)json_write_array_end(&writer);
    web_json_array(&writer, "pc");
    for (size_t phase = 0U; phase < 3U; ++phase)
    {
        (void)json_write_uint32(&writer, pc[phase]);
    }
    (void)json_write_array_end(&writer);
    (void)json_write_object_end(&writer);
    http_json_send(&writer);
}

void handle_get_fw_version(void)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    CSLOG("[FW] GET /r?fwVersion\r\n");

    (void)json_write_object_begin(&writer);
    if (NULL == fw_version)
    {
        (void)json_write_key(&writer, web_json_span("version"));
        {
            char text[80];
            (void)xsnprintf(
                text, sizeof(text), "%u.%u.%u", (unsigned int)VERSION_MAJOR,
                (unsigned int)VERSION_MINOR, (unsigned int)VERSION_PATCH);
            (void)json_write_string(&writer, web_json_span(text));
        }
    }
    else
    {
        web_json_string(&writer, "version", fw_version);
    }
    web_json_string(&writer, "buildDate", fw_build_date);
    web_json_string(&writer, "hardware", fw_hardware);
    (void)json_write_object_end(&writer);

    http_json_send(&writer);
}

void handle_get_fw_status(void)
{
    json_writer_t writer;
    if (!http_json_init(&writer))
    {
        return;
    }

    /* TODO: fw_state yalniz RAM'de - reset sonrasi "ready" durumu kaybolur.
     * NVRAM rfwu_nvram_t'ye su alanlari kalici yap:
     *   - received_bytes (4 KB hizali kontrol noktasi - yarida kalan
     *     transfer kaldigi yerden devam etsin)
     *   - total_size + file_hash (dosya kimligi - ayni dosya mi kontrolu)
     *   - tamamlanmis indirme isareti (acilista "apply hazir" geri yukle)
     * Resume akisi: fw_start'ta hash eslesirse received_bytes'tan devam;
     * eslesmezse (farkli dosya) sifirdan basla. */

    if (fw_state.in_progress)
    {
        (void)json_write_object_begin(&writer);
        web_json_bool(&writer, "active", true);
        web_json_uint32(&writer, "received",
                        (uint32_t)(fw_state.received_bytes));
        web_json_uint32(&writer, "total", (uint32_t)(fw_state.total_size));
        web_json_uint32(&writer, "fh", (uint32_t)(fw_state.file_hash));
        (void)json_write_object_end(&writer);
    }
    else if (fw_state.complete)
    {
        /* Transfer bitti, apply bekliyor - UI butonu gostersin */
        (void)json_write_object_begin(&writer);
        web_json_bool(&writer, "active", false);
        web_json_bool(&writer, "ready", true);
        web_json_uint32(&writer, "received",
                        (uint32_t)(fw_state.received_bytes));
        web_json_uint32(&writer, "total", (uint32_t)(fw_state.total_size));
        web_json_uint32(&writer, "fh", (uint32_t)(fw_state.file_hash));
        (void)json_write_object_end(&writer);
    }
    else
    {
        (void)json_write_object_begin(&writer);
        web_json_bool(&writer, "active", false);
        (void)json_write_object_end(&writer);
    }

    http_json_send(&writer);
}

static bool parse_fw_metadata(const char *body, uint32_t *size,
                              uint32_t *chunks, uint32_t *hash)
{
    json_reader_t reader;
    json_token_t token;
    char scratch[64];
    json_span_t input = {body, strlen(body)};
    if (json_reader_init(&reader, scratch, sizeof(scratch)) != JSON_OK ||
        json_reader_feed(&reader, input, true) != JSON_OK ||
        json_read(&reader, &token) != JSON_OK ||
        token.type != JSON_OBJECT_BEGIN)
    {
        return false;
    }
    for (;;)
    {
        if (json_read(&reader, &token) != JSON_OK)
        {
            return false;
        }
        if (token.type == JSON_OBJECT_END)
        {
            return json_read(&reader, &token) == JSON_DONE;
        }
        if (token.type != JSON_KEY)
        {
            return false;
        }
        uint32_t *value = NULL;
        if (json_token_equals(&token, "s") || json_token_equals(&token, "size"))
        {
            value = size;
        }
        else if (json_token_equals(&token, "c") ||
                 json_token_equals(&token, "chunks"))
        {
            value = chunks;
        }
        else if (json_token_equals(&token, "h"))
        {
            value = hash;
        }
        else
        {
            if (json_skip_value(&reader) != JSON_OK)
            {
                return false;
            }
            continue;
        }
        if (json_read(&reader, &token) != JSON_OK ||
            json_token_uint32(&token, value) != JSON_ERROR_NONE)
        {
            return false;
        }
    }
}

void handle_fw_start(const char *json_body)
{
    CSLOG("[FW] POST /s (fw_start)\r\n");

    if (NULL == json_body)
    {
        http_send_bad_request();
        return;
    }

    if (!fw_callbacks_registered || !fw_callbacks.fw_init)
    {
        CSLOG_ERR("[FW] ERROR: Callbacks not registered\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"not supported\"}");
        return;
    }

    /* Parse JSON: { "s": X, "c": Y, "h": Z } */
    uint32_t size = 0U, chunks = 0U, hash = 0U;

    if (!parse_fw_metadata(json_body, &size, &chunks, &hash))
    {
        http_send_bad_request();
        return;
    }

    if (size == 0U)
    {
        CSLOG_ERR("[FW] ERROR: Invalid size\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"invalid size\"}");
        return;
    }

    /* --- Resume detection ---
     * If an upload for the exact same file is already in progress
     * (matching total_size and file_hash), skip flash re-init and return
     * the current progress so the client continues from where it stopped.
     * hash == 0 means legacy client (no hash sent): fall back to
     * size-only matching for backward compatibility. */
    if (fw_state.in_progress && (fw_state.total_size == size) &&
        (hash == 0U || fw_state.file_hash == hash))
    {
        CCSLOG(
            XCOLOR_GREEN,
            "[FW] RESUME: same file detected, continuing from %u/%u bytes\r\n",
            fw_state.received_bytes, fw_state.total_size);

        {
            json_writer_t writer;
            if (!http_json_init(&writer))
            {
                return;
            }
            (void)json_write_object_begin(&writer);
            web_json_string(&writer, "status", "ok");
            web_json_int32(&writer, "cs", 4096);
            web_json_uint32(&writer, "received",
                            (uint32_t)(fw_state.received_bytes));
            (void)json_write_object_end(&writer);

            http_json_send(&writer);
        }
        return;
    }

    /* --- Fresh start ---
     * Different file (size or hash mismatch) or no transfer in progress:
     * reset all state and re-initialise flash. */
    if (fw_state.in_progress)
    {
        CSLOG_WARN("[FW] Different file - resetting previous upload (was %u/%u "
                   "bytes)\r\n",
                   fw_state.received_bytes, fw_state.total_size);
    }

    CCSLOG(XCOLOR_CYAN, "[FW] ========== FIRMWARE UPDATE START ==========\r\n");
    CSLOG("[FW] Total Size: %lu bytes\r\n", size);
    CSLOG("[FW] Total Chunks: %lu\r\n", chunks);
    CSLOG("[FW] File Hash: 0x%04lX\r\n", hash);

    int result = fw_callbacks.fw_init(size);
    if (result != 0)
    {
        CSLOG_ERR("[FW] ERROR: Flash init failed: %d\r\n", result);
        http_json_reply("{\"status\":\"error\",\"error\":\"flash init\"}");
        return;
    }

    fw_state.total_size = size;
    fw_state.total_chunks = chunks;
    fw_state.received_bytes = 0U;
    fw_state.received_chunks = 0U;
    fw_state.checksum_errors = 0U;
    fw_state.retry_count = 0U;
    fw_state.start_time = 0U;
    fw_state.last_chunk_time = 0U;
    fw_state.first_chunk_size = 0U;
    fw_state.file_hash = hash;
    fw_state.fw_crc = 0U;
    fw_state.fw_size = 0U;
    fw_state.in_progress = true;
    fw_state.complete = false;

    CCSLOG(XCOLOR_GREEN, "[FW] Flash initialized, ready for chunks\r\n");
    http_json_reply("{\"status\":\"ok\",\"cs\":4096,\"received\":0}");
}

/* Fletcher-16 checksum - fast, no lookup table, good error detection */
static uint16_t fletcher16(const uint8_t *data, uint32_t len)
{
    uint16_t sum1 = 0, sum2 = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        sum1 = (uint16_t)((sum1 + data[i]) % 255U);
        sum2 = (uint16_t)((sum2 + sum1) % 255U);
    }
    return (sum2 << 8) | sum1;
}

void handle_fw_chunk(uint32_t offset, const uint8_t *data, uint32_t size,
                     bool has_checksum, uint16_t expected_checksum)
{
    if (!fw_callbacks_registered || !fw_callbacks.fw_write)
    {
        http_json_reply("{\"status\":\"error\",\"error\":\"not supported\"}");
        return;
    }

    if (!fw_state.in_progress)
    {
        CSLOG_ERR("[FW] ERROR: No update in progress\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"no update\"}");
        return;
    }

    /* Remember first chunk size for accurate chunk number calculation */
    if (offset == 0 && size > 0)
    {
        fw_state.first_chunk_size = size;
        CSLOG("[FW] First chunk size: %lu bytes\r\n", size);
    }

    /* Calculate chunk number - use received_chunks for current position
     * This is the most reliable method as it doesn't depend on chunk size
     * calculation. For retries (offset < received_bytes), calculate from
     * offset. */
    uint32_t chunk_num;
    if (offset < fw_state.received_bytes)
    {
        /* This is a retry - calculate chunk number from offset */
        uint32_t chunk_size = fw_state.first_chunk_size;
        if (chunk_size == 0)
        {
            chunk_size = FW_UPDATE_DEFAULT_CHUNK_SIZE;
        }
        chunk_num = offset / chunk_size;
    }
    else
    {
        /* Normal flow - use received_chunks counter */
        chunk_num = fw_state.received_chunks;
    }

    /* Verify checksum if provided - ALWAYS log checksum info */
    uint16_t calc_checksum = fletcher16(data, size);

    if (has_checksum)
    {
        if (calc_checksum != expected_checksum)
        {
            fw_state.checksum_errors++;
            CSLOG_ERR("[FW] CHECKSUM FAIL! Chunk #%u @offset=%u\r\n", chunk_num,
                      offset);
            CSLOG_ERR("[FW]   Expected: 0x%04X\r\n", expected_checksum);
            CSLOG_ERR("[FW]   Received: 0x%04X\r\n", calc_checksum);
            CSLOG_ERR("[FW]   Size: %u bytes, Total CS errors: %u\r\n", size,
                      fw_state.checksum_errors);

            /* Request retry - send error with expected chunk number */
            {
                json_writer_t writer;
                if (!http_json_init(&writer))
                {
                    return;
                }
                (void)json_write_object_begin(&writer);
                web_json_string(&writer, "status", "error");
                web_json_string(&writer, "error", "checksum");
                web_json_uint32(&writer, "chunk", (uint32_t)(chunk_num));
                web_json_bool(&writer, "retry", true);
                (void)json_write_object_end(&writer);

                http_json_send(&writer);
            }
            return;
        }
        /* Checksum OK - log for debugging */
        CSLOG("[FW] Chunk #%lu: offset=%lu, size=%lu, CS=0x%04X OK\r\n",
              chunk_num, offset, size, calc_checksum);
    }
    else
    {
        /* No checksum provided - log warning */
        CSLOG_WARN("[FW] Chunk #%u: offset=%u, size=%u (NO CHECKSUM)\r\n",
                   chunk_num, offset, size);
    }

    /* Handle retry: if offset is before received_bytes, it's a retry */
    if (offset < fw_state.received_bytes)
    {
        fw_state.retry_count++;
        /* Check if it's the previous chunk being retried */
        if (offset + size == fw_state.received_bytes)
        {
            CSLOG_WARN("[FW] Retry chunk #%u accepted (total retries: %u)\r\n",
                       chunk_num, fw_state.retry_count);
            {
                json_writer_t writer;
                if (!http_json_init(&writer))
                {
                    return;
                }
                (void)json_write_object_begin(&writer);
                web_json_string(&writer, "status", "ok");
                web_json_uint32(&writer, "chunk", (uint32_t)(chunk_num));
                (void)json_write_object_end(&writer);

                http_json_send(&writer);
            }
            return;
        }
        /* Offset is in the middle of received data - could be partial retry */
        CSLOG_WARN("[FW] Late retry? offset=%u < received=%u (chunk #%u)\r\n",
                   offset, fw_state.received_bytes, chunk_num);
        /* Don't update received_bytes, just ACK */
        {
            json_writer_t writer;
            if (!http_json_init(&writer))
            {
                return;
            }
            (void)json_write_object_begin(&writer);
            web_json_string(&writer, "status", "ok");
            web_json_uint32(&writer, "chunk", (uint32_t)(chunk_num));
            (void)json_write_object_end(&writer);

            http_json_send(&writer);
        }
        return;
    }

    /* Check for gap in data (offset > received_bytes) */
    if (offset > fw_state.received_bytes)
    {
        uint32_t expected_chunk = fw_state.received_chunks;
        CSLOG_ERR("[FW] GAP DETECTED! offset=%u, expected=%u\r\n", offset,
                  fw_state.received_bytes);
        CSLOG_ERR("[FW]   Missing %u bytes (chunk #%u expected)\r\n",
                  offset - fw_state.received_bytes, expected_chunk);
        {
            json_writer_t writer;
            if (!http_json_init(&writer))
            {
                return;
            }
            (void)json_write_object_begin(&writer);
            web_json_string(&writer, "status", "error");
            web_json_string(&writer, "error", "gap");
            web_json_uint32(&writer, "expected",
                            (uint32_t)(fw_state.received_bytes));
            (void)json_write_object_end(&writer);

            http_json_send(&writer);
        }
        return;
    }

    if (offset + size > fw_state.total_size)
    {
        CSLOG_ERR("[FW] OVERFLOW! offset=%u + size=%u > total=%u\r\n", offset,
                  size, fw_state.total_size);
        http_json_reply("{\"status\":\"error\",\"error\":\"overflow\"}");
        return;
    }

    /* Write chunk to flash */
    int result = fw_callbacks.fw_write(offset, data, size);
    if (result != 0)
    {
        CSLOG_ERR("[FW] FLASH WRITE FAIL! offset=%u, error=%d\r\n", offset,
                  result);
        fw_state.in_progress = false;
        http_json_reply("{\"status\":\"error\",\"error\":\"write fail\"}");
        return;
    }

    fw_state.received_bytes += size;
    fw_state.received_chunks++;

    /* Log progress every 10% or every chunk in verbose mode */
    uint32_t progress = (fw_state.received_bytes * 100) / fw_state.total_size;
    static uint32_t last_progress = 0;
    if (progress / 10 != last_progress / 10)
    {
        CCSLOG(XCOLOR_GREEN,
               "[FW] Progress: %u%% (%u/%u bytes, chunk %u/%u)\r\n", progress,
               fw_state.received_bytes, fw_state.total_size,
               fw_state.received_chunks, fw_state.total_chunks);
        last_progress = progress;
    }

    /* Send OK response with chunk number for client verification */
    {
        json_writer_t writer;
        if (!http_json_init(&writer))
        {
            return;
        }
        (void)json_write_object_begin(&writer);
        web_json_string(&writer, "status", "ok");
        web_json_uint32(&writer, "chunk", (uint32_t)(chunk_num));
        (void)json_write_object_end(&writer);

        http_json_send(&writer);
    }
}

void handle_fw_finish(const char *json_body)
{
    (void)json_body;

    CCSLOG(XCOLOR_CYAN,
           "[FW] ========== FIRMWARE TRANSFER FINISH ==========\r\n");

    if (!fw_callbacks_registered || !fw_callbacks.fw_finish)
    {
        CSLOG_ERR("[FW] ERROR: Callbacks not registered\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"not supported\"}");
        return;
    }

    if (!fw_state.in_progress)
    {
        CSLOG_ERR("[FW] ERROR: No update in progress\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"no update\"}");
        return;
    }

    /* Print detailed transfer summary */
    CSLOG("[FW] ---- Transfer Summary ----\r\n");
    CSLOG("[FW] Total Size:      %lu bytes\r\n", fw_state.total_size);
    CSLOG("[FW] Received:        %lu bytes\r\n", fw_state.received_bytes);
    CSLOG("[FW] Expected Chunks: %lu\r\n", fw_state.total_chunks);
    CSLOG("[FW] Received Chunks: %lu\r\n", fw_state.received_chunks);
    CSLOG("[FW] Checksum Errors: %lu\r\n", fw_state.checksum_errors);
    CSLOG("[FW] Retry Count:     %lu\r\n", fw_state.retry_count);

    if (fw_state.received_bytes != fw_state.total_size)
    {
        uint32_t missing = fw_state.total_size - fw_state.received_bytes;
        CSLOG_ERR("[FW] ERROR: Incomplete transfer!\r\n");
        CSLOG_ERR(
            "[FW]   Missing: %u bytes (%.1f%%)\r\n", missing,
            (double)((float)missing * 100.0f / (float)fw_state.total_size));

        {
            json_writer_t writer;
            if (!http_json_init(&writer))
            {
                return;
            }
            (void)json_write_object_begin(&writer);
            web_json_string(&writer, "status", "error");
            web_json_string(&writer, "error", "incomplete");
            web_json_uint32(&writer, "received",
                            (uint32_t)(fw_state.received_bytes));
            web_json_uint32(&writer, "expected",
                            (uint32_t)(fw_state.total_size));
            (void)json_write_object_end(&writer);

            http_json_send(&writer);
        }
        return;
    }

    CSLOG("[FW] Transfer complete, verifying...\r\n");

    /* Finalize - verification is done by callback if needed */
    int result = fw_callbacks.fw_finish(fw_state.total_size);
    if (result != 0)
    {
        CSLOG_ERR("[FW] ERROR: Verification failed! Code: %d\r\n", result);
        fw_state.in_progress = false;
        http_json_reply("{\"status\":\"error\",\"error\":\"verify fail\"}");
        return;
    }

    fw_state.in_progress = false;
    fw_state.complete = true;

    /* BL-21: capture the image identity actually stored in the download
     * section so /fw_apply can bind the update request to it.  Unbound
     * (0/0) on an unreadable header -- the bootloader install path still
     * ECDSA-verifies whatever it installs. */
    if (app_ipc_read_download_image_id(&fw_state.fw_crc, &fw_state.fw_size) !=
        0)
    {
        fw_state.fw_crc = 0U;
        fw_state.fw_size = 0U;
        CSLOG_WARN("[FW] Downloaded image header unreadable - apply will be "
                   "unbound\r\n");
    }
    else
    {
        CSLOG("[FW] Image bound: crc=0x%08lX size=%lu\r\n", fw_state.fw_crc,
              fw_state.fw_size);
    }

    /* Print success summary */
    CCSLOG(XCOLOR_GREEN, "[FW] ========================================\r\n");
    CCSLOG(XCOLOR_GREEN, "[FW] FW TRANSFER COMPLETE - IMAGE VERIFIED\r\n");
    CCSLOG(XCOLOR_GREEN, "[FW] ========================================\r\n");
    CSLOG("[FW] Total: %lu bytes in %lu chunks\r\n", fw_state.total_size,
          fw_state.received_chunks);
    if (fw_state.checksum_errors > 0 || fw_state.retry_count > 0)
    {
        CSLOG_WARN(
            "[FW] Note: %u checksum errors, %u retries during transfer\r\n",
            fw_state.checksum_errors, fw_state.retry_count);
    }
    CSLOG("[FW] Image NOT applied - use Apply Firmware (/fw_apply)\r\n");

    http_json_reply("{\"status\":\"ok\"}");
}

void handle_fw_reboot(void)
{
    CCSLOG(XCOLOR_CYAN, "[FW] ========== REBOOT REQUEST ==========\r\n");

    if (!fw_callbacks_registered || !fw_callbacks.fw_reboot)
    {
        CSLOG_ERR("[FW] ERROR: Reboot callback not registered\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"not supported\"}");
        return;
    }

    /* Send response before reboot */
    http_json_reply("{\"status\":\"ok\"}");

    fw_callbacks.fw_reboot();
}

void handle_fw_apply(void)
{
    CCSLOG(XCOLOR_CYAN, "[FW] ========== APPLY FW REQUEST ==========\r\n");

    if (!fw_state.complete)
    {
        CSLOG_ERR("[FW] ERROR: No firmware downloaded\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"no firmware\"}");
        return;
    }

    if (fw_state.received_bytes != fw_state.total_size)
    {
        CSLOG_ERR("[FW] ERROR: Download incomplete\r\n");
        http_json_reply("{\"status\":\"error\",\"error\":\"incomplete\"}");
        return;
    }

    CSLOG_WARN("[FW] Requesting bootloader update mode via IPC...\r\n");

    elog_log_fw_update(ELOG_FW_SRC_RFWU, ELOG_FW_RESULT_START, 0U);
    /* BL-21: bind to the image captured at transfer completion
     * (0/0 = unbound fallback). */
    int result =
        app_ipc_request_update(fw_state.fw_crc, fw_state.fw_size, false);

    if (result != APP_IPC_OK)
    {
        CSLOG_ERR("[FW] ERROR: IPC request failed (%d)\r\n", result);
        http_json_reply("{\"status\":\"error\",\"error\":\"ipc failed\"}");
        return;
    }

    CCSLOG(XCOLOR_GREEN, "[FW] Update armed - reset in 10 s\r\n");
    /* Response goes out before the countdown starts. */
    http_json_reply("{\"status\":\"ok\",\"reset_in\":10}");
    reboot_system_delayed(10000U);
}

/*** end of file ***/
