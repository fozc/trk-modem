/**
 * @file http_handlers.h
 * @brief HTTP endpoint handlers for smart breaker API
 * 
 * Implements all HTTP endpoints:
 * 
 * HTML Pages:
 * - GET  /index.html -> Device configuration page
 * - GET  /board_status.html -> Board status monitoring page
 * - GET  /iec_config.html -> IEC 104 configuration page
 * - GET  /modbus_config.html -> Modbus RTU configuration page
 * - GET  /rf_config.html -> RF disconnector configuration page
 * 
 * JSON API (New):
 * - GET  /r?deviceConfig -> device configuration JSON
 * - POST /w?deviceConfig -> set device configuration from JSON
 * - GET  /r?boardStatus -> board status JSON
 * - GET  /r?iecConfig -> IEC104 configuration JSON
 * - POST /w?iecConfig -> set IEC104 configuration from JSON
 * - GET  /r?modbusConfigs -> Modbus configuration JSON
 * - POST /w?modbusConfigs -> set Modbus configuration from JSON
 * - GET  /r?ayiriciRFConfig -> RF configuration JSON
 * - POST /w?ayiriciRFConfig -> set RF configuration from JSON
 * 
 * Legacy API (Backward compatibility):
 * - GET  /r?deviceInfo -> device information
 * - POST /w?deviceInfo=... -> set device information
 * - GET  /r?iec104Config -> IEC104 configuration
 * - POST /w?iec104Config=... -> set IEC104 configuration
 * - GET  /r?lines -> all line data (hex encoded)
 * - POST /w?lineConfigs=... -> set line configurations
 * - POST /echo -> echo body
 * 
 * Uses global tx_buffer for response assembly.
 */

#ifndef HTTP_HANDLERS_H_
#define HTTP_HANDLERS_H_

#include "http_request_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize handlers module
 * 
 * Sets up tx_buffer pointer for response assembly.
 * Must be called before any handler functions.
 * 
 * @param tx_buffer_ptr Pointer to global tx_buffer
 * @param tx_buffer_size Size of tx_buffer in bytes
 */
void http_handlers_init(char *tx_buffer_ptr, int tx_buffer_size);

/**
 * @brief Set current request query string
 * 
 * Stores query string pointer for handlers to access.
 * Should be called before each handler invocation.
 * 
 * @param query_string Query string from current request (can be NULL)
 */
void http_handlers_set_query_string(const char *query_string);

/**
 * @brief Reset connection state when TCP socket closes.
 *
 * Clears the per-request auth flag and the stale query string pointer.
 * The session token and username are preserved so the client can resume
 * using the same token after a socket reconnect.
 */
void http_handlers_reset(void);

/**
 * @brief Validate session token from request query string.
 *
 * Parses the "t=XXXXXXXX" parameter, compares it to the stored session
 * token and checks the idle TTL.  Sets the internal is_authenticated flag
 * accordingly.  Must be called once per request, before the auth gate.
 *
 * @param query_string Request query string (may be NULL)
 */
void http_handlers_set_auth_from_token(const char *query_string);

/**
 * @brief Get TX buffer pointer for response assembly
 * 
 * @param size Output parameter for buffer size
 * @return Pointer to TX buffer
 */
char* http_handlers_get_tx_buffer(int *size);

/**
 * @brief Get current query string
 * 
 * @return Current query string or NULL
 */
const char* http_handlers_get_query_string(void);

/**
 * @brief Check if current session is authenticated
 * 
 * @return true if authenticated, false otherwise
 */
bool http_handlers_is_authenticated(void);

/**
 * @brief Check if current session has admin role
 * 
 * @return true if authenticated as admin, false otherwise
 */
bool http_handlers_is_admin(void);


/* ============================================================================
 * GET HANDLERS
 * ============================================================================ */

/**
 * @brief Handle GET /
 * 
 * Serves index.html from flash memory.
 */
void handle_get_index(void);


/**
 * @brief Handle GET /fw_update.html
 * 
 * Serves fw_update.html from flash memory.
 */
void handle_get_fw_update(void);


/* ============================================================================
 * POST HANDLERS
 * ============================================================================ */

/**
 * @brief Handle POST /auth/login
 * 
 * Validates username and password against IP-derived credentials.
 * Expects JSON: {"username":"...","password":"..."}
 * Returns JSON: {"success":true} or {"success":false,"error":"..."}
 * 
 * @param json_body Request body (JSON)
 */
void handle_post_login(const char *json_body);

/**
 * @brief Handle POST /auth/logout
 * 
 * Clears session and logs out the current user.
 * Returns JSON: {"success":true}
 */
void handle_post_logout(void);

/**
 * @brief Handle POST /echo
 * 
 * Echoes back the request body (for testing).
 * Maximum body size: 512 bytes
 * 
 * @param body Request body
 * @param body_length Body length in bytes
 */
void handle_post_echo(const char *body, int body_length);


/* ============================================================================
 * JSON CONFIGURATION HANDLERS
 * ============================================================================ */

/**
 * @brief Handle GET /r?deviceConfig
 * 
 * Returns device configuration as JSON
 */
void handle_get_device_config_json(void);

/**
 * @brief Handle POST /w?deviceConfig
 * 
 * Updates device configuration from JSON body
 * 
 * @param json_body JSON request body
 */
void handle_post_device_config_json(const char *json_body);

/**
 * @brief Handle GET /r?boardStatus
 * 
 * Returns board status as JSON
 */
void handle_get_board_status_json(void);

/**
 * @brief Handle GET /syslogs?offset=X&limit=Y
 * 
 * Returns system logs as JSON with pagination
 */
void handle_get_syslogs_json(void);

/**
 * @brief Handle GET /faults?feeder=F
 *
 * Returns IEC104 fault records (temporary + permanent) for all 3 phases
 * of a given feeder as JSON.
 * Response fields: feeder, L1T, L1P, L2T, L2P, L3T, L3P, tc[3], pc[3]
 */
void handle_get_fault_records_json(void);

/**
 * @brief Handle GET /r?iecConfig
 * 
 * Returns IEC104 configuration as JSON
 */
void handle_get_iec_config_json(void);

/**
 * @brief Handle POST /w?iecConfig
 * 
 * Updates IEC104 configuration from JSON body
 * 
 * @param json_body JSON request body
 */
void handle_post_iec_config_json(const char *json_body);

/**
 * @brief Handle GET /r?modbusConfigs
 * 
 * Returns Modbus configuration as JSON
 */
void handle_get_modbus_config_json(void);

/**
 * @brief Handle POST /w?modbusConfigs
 * 
 * Updates Modbus configuration from JSON body
 * 
 * @param json_body JSON request body
 */
void handle_post_modbus_config_json(const char *json_body);

/**
 * @brief Handle GET /r?ayiriciRFConfig
 * 
 * Returns RF configuration as JSON
 */
void handle_get_rf_config_json(void);

/**
 * @brief Handle POST /w?ayiriciRFConfig
 * 
 * Updates RF configuration from JSON body
 * 
 * @param json_body JSON request body
 */
void handle_post_rf_config_json(const char *json_body);

/**
 * @brief Handle GET /r?ayiriciRFMonitor
 * 
 * Returns RF monitor data as JSON (read-only)
 */
void handle_get_rf_monitor_json(void);

/**
 * @brief Send RF monitor JSON for a specific line
 * 
 * @param line_id Line index (0-7)
 */
void handle_get_rf_monitor_line_json(int line_id);

/* ============================================================================
 * FIRMWARE UPDATE HANDLERS
 * ============================================================================ */

/**
 * @brief Firmware update callback type
 * 
 * Called by firmware update handlers to perform actual flash operations
 * Must be implemented by application
 */
typedef struct {
    /**
     * @brief Initialize firmware update
     * @param total_size Total firmware size in bytes
     * @return 0 on success, negative error code on failure
     */
    int (*fw_init)(uint32_t total_size);
    
    /**
     * @brief Write firmware chunk
     * @param offset Byte offset from start of firmware
     * @param data Pointer to chunk data
     * @param size Size of chunk in bytes
     * @return 0 on success, negative error code on failure
     */
    int (*fw_write)(uint32_t offset, const uint8_t *data, uint32_t size);
    
    /**
     * @brief Finish firmware update
     * @param total_size Total firmware size for verification
     * @return 0 on success, negative error code on failure
     */
    int (*fw_finish)(uint32_t total_size);
    
    /**
     * @brief Reboot device to apply firmware
     */
    void (*fw_reboot)(void);
} fw_update_callbacks_t;

/**
 * @brief Register firmware update callbacks
 * 
 * Must be called before firmware update handlers can be used
 * 
 * @param callbacks Pointer to callback structure (must remain valid)
 */
void fw_update_register_callbacks(const fw_update_callbacks_t *callbacks);

/**
 * @brief Handle GET /r?fwVersion
 * 
 * Returns current firmware version information
 */
void handle_get_fw_version(void);

/**
 * @brief Handle GET /fw_status
 *
 * Returns current firmware update state for resume support.
 * Response when active:   {"active":true,"received":N,"total":M}
 * Response when inactive: {"active":false}
 */
void handle_get_fw_status(void);

/**
 * @brief Handle POST /fw_start
 * 
 * Initialize firmware update session
 * JSON body: { "size": <bytes>, "chunks": <count>, "filename": <name> }
 * 
 * @param json_body JSON request body
 */
void handle_fw_start(const char *json_body);

/**
 * @brief Handle POST /fw_chunk?offset=X
 * 
 * Receive and write firmware chunk (1024 bytes)
 * Waits for chunk to be written, then returns OK as ACK
 * Verifies Fletcher-16 checksum if provided
 * 
 * @param offset Byte offset
 * @param data Raw binary data
 * @param size Data size
 * @param has_checksum True if checksum was provided in request
 * @param expected_checksum Fletcher-16 checksum from client
 */
void handle_fw_chunk(uint32_t offset, const uint8_t *data, uint32_t size,
                     bool has_checksum, uint16_t expected_checksum);

/**
 * @brief Handle POST /fw_finish
 * 
 * Finalize firmware update
 * JSON body: { "size": <bytes> }
 * 
 * @param json_body JSON request body
 */
void handle_fw_finish(const char *json_body);

/**
 * @brief Handle POST /fw_reboot
 * 
 * Reboot device to apply new firmware
 */
void handle_fw_reboot(void);

/**
 * @brief Handle POST /fw_apply
 *
 * Trigger firmware update via IPC — enters bootloader update mode.
 * Requires a completed firmware download.
 */
void handle_fw_apply(void);

/**
 * @brief Handle POST /serial - Receive command from web shell
 *
 * Passes received data to the registered rx callback and returns
 * immediately with {"ok":true}. Response data is delivered
 * asynchronously via GET /serial.
 * Expects JSON: {"tx":"data_to_send"}
 * Requires admin role.
 *
 * @param json_body JSON request body
 */
void handle_post_web_shell(const char *json_body);

/**
 * @brief Handle GET /serial - Poll for pending web shell response
 *
 * Flushes the web_shell TX buffer and returns any pending output
 * as JSON: {"rx":"..."}. Returns empty rx string if no data is
 * available yet.
 * Requires admin role.
 */
void handle_get_web_shell(void);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_HANDLERS_H_ */

