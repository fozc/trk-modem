/*
 * json_config.c
 *
 *  Created on: 31 Eki 2025
 *      Author: fatih
 */

#include "json_config.h"
#include "bsp.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include "modem_config.h"
#include "iec104_util.h"
#include "iec104_config.h"
#include "modbus_config.h"
#include "modbus_process.h"
#include "rf_config.h"
#include "rf.h"
#include "system_status.h"


/* ============================================
 * HELPER FUNCTIONS
 * ============================================ */

/* Skip whitespace */
static const char* skip_whitespace(const char *str) {
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/* Skip value (for unknown fields) */
static bool skip_value(const char **str) {
    const char *s = skip_whitespace(*str);
    if (!s || !*s) return false;
    
    /* String */
    if (*s == '"') {
        s++;
        while (*s && *s != '"') {
            if (*s == '\\') {
                s++;
                if (!*s) return false;
            }
            s++;
        }
        if (*s != '"') return false;
        s++;
    }
    /* Number */
    else if (*s == '-' || isdigit((unsigned char)*s)) {
        if (*s == '-') s++;
        if (!isdigit((unsigned char)*s)) return false;
        while (isdigit((unsigned char)*s)) s++;
        if (*s == '.') {
            s++;
            while (isdigit((unsigned char)*s)) s++;
        }
    }
    /* Boolean */
    else if (strncmp(s, "true", 4) == 0) {
        s += 4;
    }
    else if (strncmp(s, "false", 5) == 0) {
        s += 5;
    }
    /* Null */
    else if (strncmp(s, "null", 4) == 0) {
        s += 4;
    }
    /* Array */
    else if (*s == '[') {
        s++;
        int depth = 1;
        while (*s && depth > 0) {
            if (*s == '[') depth++;
            else if (*s == ']') depth--;
            else if (*s == '"') {
                s++;
                while (*s && *s != '"') {
                    if (*s == '\\') s++;
                    if (*s) s++;
                }
            }
            if (*s) s++;
        }
        if (depth != 0) return false;
    }
    /* Object */
    else if (*s == '{') {
        s++;
        int depth = 1;
        while (*s && depth > 0) {
            if (*s == '{') depth++;
            else if (*s == '}') depth--;
            else if (*s == '"') {
                s++;
                while (*s && *s != '"') {
                    if (*s == '\\') s++;
                    if (*s) s++;
                }
            }
            if (*s) s++;
        }
        if (depth != 0) return false;
    }
    else {
        return false;
    }
    
    *str = s;
    return true;
}

/* Compare string with keyword */
static bool match_key(const char **str, const char *key) {
    const char *s = skip_whitespace(*str);
    
    if (!s || !*s) return false;
    if (*s != '"') return false;
    s++;
    
    size_t len = strlen(key);
    if (strncmp(s, key, len) != 0) return false;
    s += len;
    
    if (*s != '"') return false;
    s++;
    
    s = skip_whitespace(s);
    if (!s || *s != ':') return false;
    s++;
    
    *str = skip_whitespace(s);
    return true;
}

/* Read and skip key - for unknown keys */
static bool skip_unknown_key_value(const char **str) {
    const char *s = skip_whitespace(*str);
    
    if (!s || !*s || *s != '"') return false;
    
    /* Key'i atla */
    s++;
    while (*s && *s != '"') {
        if (*s == '\\') s++;
        if (*s) s++;
    }
    if (*s != '"') return false;
    s++;
    
    /* ':' bekle */
    s = skip_whitespace(s);
    if (!s || *s != ':') return false;
    s++;
    
    /* Value'yu atla */
    if (!skip_value(&s)) return false;
    
    *str = s;
    return true;
}

/* Parse string value with length validation */
static bool parse_string(const char **str, char *dest, size_t max_len) {
    const char *s = skip_whitespace(*str);
    
    if (*s != '"') return false;
    s++;
    
    size_t i = 0;
    bool truncated = false;
    
    while (*s && *s != '"') {
        if (i < max_len - 1) {
            if (*s == '\\') {
                s++;
                if (!*s) return false;
            }
            dest[i++] = *s;
        } else {
            truncated = true;
        }
        s++;
    }
    
    if (*s != '"') {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: String not properly terminated\r\n");
        return false;
    }
    s++;
    
    dest[i] = '\0';
    
    /* Warn if string was truncated */
    if (truncated) {
        xprintf("[JSON] WARNING: String truncated at %zu bytes (max: %zu)\r\n", i, max_len - 1);
    }
    
    *str = s;
    return true;
}

/* Parse integer value with overflow protection */
static bool parse_uint32(const char **str, uint32_t *value) {
    const char *s = skip_whitespace(*str);
    
    if (!isdigit((unsigned char)*s)) return false;
    
    *value = 0;
    while (isdigit((unsigned char)*s)) {
        uint32_t digit = (*s - '0');
        
        /* Overflow detection: check if next multiplication would overflow */
        if (*value > (UINT32_MAX - digit) / 10) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Integer overflow detected\r\n");
            return false;
        }
        
        *value = *value * 10 + digit;
        s++;
    }
    
    *str = s;
    return true;
}

/* Parse 16-bit integer value with range validation */
static bool parse_uint16(const char **str, uint16_t *value) {
    uint32_t temp;
    if (!parse_uint32(str, &temp)) return false;
    
    /* Range check for uint16_t */
    if (temp > 65535) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Value %u exceeds uint16_t max (65535)\r\n", temp);
        return false;
    }
    
    *value = (uint16_t)temp;
    return true;
}

/* Parse 8-bit integer value with range validation */
static bool parse_uint8(const char **str, uint8_t *value) {
    uint32_t temp;
    if (!parse_uint32(str, &temp)) return false;
    
    /* Range check for uint8_t */
    if (temp > 255) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Value %u exceeds uint8_t max (255)\r\n", temp);
        return false;
    }
    
    *value = (uint8_t)temp;
    return true;
}

/* Parse signed integer value with overflow protection */
static bool parse_int32(const char **str, int32_t *value) {
    const char *s = skip_whitespace(*str);
    
    bool negative = false;
    if (*s == '-') {
        negative = true;
        s++;
    }
    
    if (!isdigit((unsigned char)*s)) return false;
    
    uint32_t temp = 0;
    while (isdigit((unsigned char)*s)) {
        uint32_t digit = (*s - '0');
        
        /* Overflow detection */
        if (temp > (UINT32_MAX - digit) / 10) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Integer overflow detected\r\n");
            return false;
        }
        
        temp = temp * 10 + digit;
        s++;
    }
    
    /* Check int32_t range */
    if (negative) {
        if (temp > 2147483648U) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Value exceeds int32_t min\r\n");
            return false;
        }
        *value = -(int32_t)temp;
    } else {
        if (temp > 2147483647U) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Value exceeds int32_t max\r\n");
            return false;
        }
        *value = (int32_t)temp;
    }
    
    *str = s;
    return true;
}

/* Signed Parse 16-bit integer value with range validation */
static bool parse_int16(const char **str, int16_t *value) {
    int32_t temp;
    if (!parse_int32(str, &temp)) return false;
    
    /* Range check for int16_t */
    if (temp < -32768 || temp > 32767) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Value %d exceeds int16_t range (-32768 to 32767)\r\n", temp);
        return false;
    }
    
    *value = (int16_t)temp;
    return true;
}

/* Signed Parse 8-bit integer value with range validation */
static bool parse_int8(const char **str, int8_t *value) {
    int32_t temp;
    if (!parse_int32(str, &temp)) return false;
    
    /* Range check for int8_t */
    if (temp < -128 || temp > 127) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Value %d exceeds int8_t range (-128 to 127)\r\n", temp);
        return false;
    }
    
    *value = (int8_t)temp;
    return true;
}

/* Parse float value */
static bool parse_float(const char **str, float *value) {
    const char *s = skip_whitespace(*str);
    
    bool negative = false;
    if (*s == '-') {
        negative = true;
        s++;
    }
    
    if (!isdigit((unsigned char)*s)) return false;
    
    /* Integer part */
    float result = 0.0f;
    while (isdigit((unsigned char)*s)) {
        result = result * 10.0f + (*s - '0');
        s++;
    }
    
    /* Decimal part */
    if (*s == '.') {
        s++;
        float decimal = 0.0f;
        float divisor = 10.0f;
        while (isdigit((unsigned char)*s)) {
            decimal += (*s - '0') / divisor;
            divisor *= 10.0f;
            s++;
        }
        result += decimal;
    }
    
    *value = negative ? -result : result;
    
    /* NaN and Infinity validation */
    if (isnan(*value) || isinf(*value)) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Invalid float value (NaN or Inf)\r\n");
        return false;
    }
    
    *str = s;
    return true;
}

/* Parse boolean value */
static bool parse_bool(const char **str, bool *value) {
    const char *s = skip_whitespace(*str);
    
    if (strncmp(s, "true", 4) == 0) {
        *value = true;
        *str = s + 4;
        return true;
    } else if (strncmp(s, "false", 5) == 0) {
        *value = false;
        *str = s + 5;
        return true;
    }
    
    return false;
}

/* ============================================
 * VALIDATION FUNCTIONS
 * ============================================ */

/* Validate IP address format (IPv4) */
static bool validate_ip_address(const char *ip) {
    if (!ip) return false;
    
    size_t len = strlen(ip);
    if (len < 7 || len > 15) {  /* Min: "0.0.0.0", Max: "255.255.255.255" */
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid IP length: %zu\r\n", len);
        return false;
    }
    
    int count = 0;
    int num = 0;
    int digit_count = 0;

    for (size_t i = 0; i <= len; i++) {
        if (ip[i] == '.' || ip[i] == '\0') {
            if (digit_count == 0 || digit_count > 3) {
                xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid IP format\r\n");
                return false;
            }
            if (num > 255) {
                xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid IP octet: %d (max: 255)\r\n", num);
                return false;
            }
            count++;
            num = 0;
            digit_count = 0;
            if (count > 4) {
                xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Too many IP octets\r\n");
                return false;
            }
        } else if (ip[i] >= '0' && ip[i] <= '9') {
            num = num * 10 + (ip[i] - '0');
            digit_count++;
        } else {
            xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid IP character: '%c'\r\n", ip[i]);
            return false;
        }
    }
    
    if (count != 4) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: IP must have 4 octets, got %d\r\n", count);
        return false;
    }
    
    return true;
}

/* Validate port number (1-65535) */
static bool validate_port(uint16_t port) {
    if (port == 0) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Port cannot be 0\r\n");
        return false;
    }
    /* port is uint16_t, so max is automatically 65535 */
    return true;
}

/* Validate zone ID (6-bit value: 0-63) */
static bool validate_zone_id(uint8_t zone_id) {
    if (zone_id > 63) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid zone_id: %u (max: 63)\r\n", zone_id);
        return false;
    }
    return true;
}

/* ============================================
 * BUSINESS LOGIC VALIDATION
 * ============================================ */

/* Validate IEC104 timeout values (must be logical: T0 < T1 < T2 < T3) */
static bool validate_iec_timeouts(const jiec_config_t *config) {
    if (config->t0_timeout < 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: T0 must be >= 1 second\r\n");
        return false;
    }
    if (config->t1_timeout < 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: T1 must be >= 1 second\r\n");
        return false;
    }
    if (config->t2_timeout < 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: T2 must be >= 1 second\r\n");
        return false;
    }
    if (config->t3_timeout < 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: T3 must be >= 1 second\r\n");
        return false;
    }
    if (config->t1_timeout >= config->t3_timeout) {
        xprintf("[VALIDATION] WARNING: T1 (%u) should be < T3 (%u) for optimal operation\r\n",
               config->t1_timeout, config->t3_timeout);
    }
    return true;
}

/* Validate IEC104 window parameters (K and W, uint8_t: 1-255) */
static bool validate_iec_windows(const jiec_config_t *config) {
    if (config->k_max < 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: K must be 1-255, got %u\r\n", config->k_max);
        return false;
    }
    if (config->w_max < 1) {
    	xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: W must be 1-255, got %u\r\n", config->w_max);
        return false;
    }
    if (config->w_max >= config->k_max) {
    	xcprintf(XCOLOR_RED, "[VALIDATION] WARNING: W (%u) should be < K (%u) for optimal flow control\r\n",
               config->w_max, config->k_max);
    }
    if (config->k_max > 100) {
        xprintf("[VALIDATION] WARNING: K=%u is very large, may impact performance\r\n", config->k_max);
    }
    if (config->w_max > 100) {
        xprintf("[VALIDATION] WARNING: W=%u is very large, may impact performance\r\n", config->w_max);
    }
    return true;
}

/* Validate IEC104 common address (1-65535) */
static bool validate_common_address(uint8_t common_address) {
    if (common_address == 0) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Common address cannot be 0\r\n");
        return false;
    }
    /* 255 is broadcast address, should be avoided for single device */
    if (common_address == 255) {
        xprintf("[VALIDATION] WARNING: Common address 255 is broadcast\r\n");
    }
    return true;
}

/* Validate RF working mode - Operating_Mode (spec R2 section 4.3: {0,1}) */
static bool validate_working_mode(uint8_t mode) {
    if (mode > 1) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Working mode %u invalid (0=threshold, 1=di/dt)\r\n", mode);
        return false;
    }
    return true;
}

/* Validate RF line frequency - HatFrekansi (spec R2 section 4.2: {50,60}) */
static bool validate_rf_frequency(uint32_t freq_hz) {
    if ((freq_hz != 50U) && (freq_hz != 60U)) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Line frequency %u Hz invalid (50 or 60 only)\r\n", freq_hz);
        return false;
    }
    return true;
}

/* Clamp-free RF numeric range check (spec R2 section 3): limit disi deger
 * reddedilir - cihaza asla gonderilmemelidir (spec section 5.3-2). */
static bool validate_rf_float(float value, float min, float max, const char *name) {
    if ((value < min) || (value > max)) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: %s %.3f out of range [%.3f, %.3f]\r\n",
               name, value, min, max);
        return false;
    }
    return true;
}

static bool validate_rf_uint(uint32_t value, uint32_t min, uint32_t max, const char *name) {
    if ((value < min) || (value > max)) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: %s %u out of range [%u, %u]\r\n",
               name, value, min, max);
        return false;
    }
    return true;
}

/* Validate discrete RF uint set (e.g. Trip_Mode {0,1}, CLP {0,1}) */
static bool validate_rf_uint_set(uint32_t value, const uint32_t *allowed, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (value == allowed[i]) {
            return true;
        }
    }
    xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: %s %u not one of allowed values\r\n", name, value);
    return false;
}

/* Validate timeout values (general) */
static bool validate_timeout(uint32_t timeout_sec, uint32_t min_sec, uint32_t max_sec, const char *name) {
    if (timeout_sec < min_sec) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: %s timeout %u is too short (min: %u seconds)\r\n",
               name, timeout_sec, min_sec);
        return false;
    }
    if (timeout_sec > max_sec) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: %s timeout %u is too long (max: %u seconds)\r\n",
               name, timeout_sec, max_sec);
        return false;
    }
    return true;
}

/* Validate baud rate (Modbus RTU) */
static bool validate_baud_rate(uint32_t baud) {
    /* Standard baud rates: 9600, 19200, 38400, 57600, 115200 */
    switch (baud) {
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
            return true;
        case 4800:
        case 14400:
        case 28800:
            xprintf("[VALIDATION] WARNING: Baud rate %u is uncommon but supported\r\n", baud);
            return true;
        default:
            xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Invalid baud rate %u (standard: 9600, 19200, 38400, 57600, 115200)\r\n", baud);
            return false;
    }
}

/* Validate Modbus device ID (1-247) */
static bool validate_modbus_device_id(uint8_t device_addr) {
    if (device_addr == 0) {
        xcprintf(XCOLOR_YELLOW, "[VALIDATION] WARNINIG: Modbus device ID 0 is broadcasting address\r\n");
    }
    if (device_addr > 247) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Modbus device ID %u exceeds valid range (1-247)\r\n", device_addr);
        return false;
    }
    return true;
}

/* Validate SIM card PIN (uint16_t alani: en az 4 basamak) */
static bool validate_sim_pin(uint16_t pin) {
    if (pin < 1000) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: SIM PIN %u invalid (expected 4+ digits)\r\n", pin);
        return false;
    }
    return true;
}

/* Validate timezone offset (-12 to +14 hours) */
static bool validate_timezone(int32_t timezone_offset) {
    if (timezone_offset < -12 || timezone_offset > 14) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Timezone offset %d invalid (range: -12 to +14)\r\n", timezone_offset);
        return false;
    }
    return true;
}

/* Check array start */
static bool expect_array_start(const char **str) {
    const char *s = skip_whitespace(*str);
    if (*s != '[') return false;
    *str = skip_whitespace(s + 1);
    return true;
}

/* Check array end */
static bool is_array_end(const char **str) {
    const char *s = skip_whitespace(*str);
    if (*s == ']') {
        *str = s + 1;
        return true;
    }
    return false;
}

/* Check object start */
static bool expect_object_start(const char **str) {
    const char *s = skip_whitespace(*str);
    if (*s != '{') return false;
    *str = skip_whitespace(s + 1);
    return true;
}

/* Check object end */
static bool is_object_end(const char **str) {
    const char *s = skip_whitespace(*str);
    if (*s == '}') {
        *str = s + 1;
        return true;
    }
    return false;
}

/* Skip comma */
static void skip_comma(const char **str) {
    const char *s = skip_whitespace(*str);
    if (*s == ',') {
        *str = skip_whitespace(s + 1);
    }
}

/* Consume any remaining array elements up to and including the closing ']'.
 * Called after a capacity-limited parse loop so that an input array longer
 * than the destination buffer does not leave the cursor mid-array and desync
 * all subsequent parsing. Also cleanly consumes ']' when nothing remains. */
static bool skip_array_remainder(const char **str) {
    const char *s = skip_whitespace(*str);
    while (*s != ']') {
        if (!*s) return false;               /* Unterminated array */
        if (!skip_value(str)) return false;  /* Discard one surplus element */
        skip_comma(str);
        s = skip_whitespace(*str);
    }
    *str = skip_whitespace(s + 1);           /* Consume ']' */
    return true;
}

/* Parse array of uint32 values */
static bool parse_uint32_array(const char **str, uint32_t *arr, int max_count) {
    if (!expect_array_start(str)) return false;
    
    int count = 0;
    const char *s = skip_whitespace(*str);
    while (*s != ']' && count < max_count) {
        if (!parse_uint32(str, &arr[count])) return false;
        count++;
        skip_comma(str);
        s = skip_whitespace(*str);
    }
    
    /* Drain any elements beyond max_count and consume the closing bracket */
    return skip_array_remainder(str);
}

/* Parse array of int32 values */
static bool parse_int32_array(const char **str, int32_t *arr, int max_count) {
    if (!expect_array_start(str)) return false;
    
    int count = 0;
    const char *s = skip_whitespace(*str);
    while (*s != ']' && count < max_count) {
        if (!parse_int32(str, &arr[count])) return false;
        count++;
        skip_comma(str);
        s = skip_whitespace(*str);
    }
    
    /* Drain any elements beyond max_count and consume the closing bracket */
    return skip_array_remainder(str);
}

/* Parse array of float values */
static bool parse_float_array(const char **str, float *arr, int max_count) {
    if (!expect_array_start(str)) return false;
    
    int count = 0;
    const char *s = skip_whitespace(*str);
    while (*s != ']' && count < max_count) {
        if (!parse_float(str, &arr[count])) return false;
        count++;
        skip_comma(str);
        s = skip_whitespace(*str);
    }
    
    /* Drain any elements beyond max_count and consume the closing bracket */
    return skip_array_remainder(str);
}

/* Parse array of bool values */
static bool parse_bool_array(const char **str, bool *arr, int max_count) {
    if (!expect_array_start(str)) return false;

    int count = 0;
    const char *s = skip_whitespace(*str);
    while (*s != ']' && count < max_count) {
        if (!parse_bool(str, &arr[count])) return false;
        count++;
        skip_comma(str);
        s = skip_whitespace(*str);
    }

    /* Drain any elements beyond max_count and consume the closing bracket */
    return skip_array_remainder(str);
}

/* Parse array of quoted strings (fixed-width rows, e.g. EUI-64 hex) */
static bool parse_str_array(const char **str, char arr[][RF_EUI64_HEX_LEN], int max_count) {
    if (!expect_array_start(str)) return false;

    int count = 0;
    const char *s = skip_whitespace(*str);
    while (*s != ']' && count < max_count) {
        if (!parse_string(str, arr[count], RF_EUI64_HEX_LEN)) return false;
        count++;
        skip_comma(str);
        s = skip_whitespace(*str);
    }

    /* Drain any elements beyond max_count and consume the closing bracket */
    return skip_array_remainder(str);
}

/* ============================================
 * PARSE FUNCTIONS
 * ============================================ */

static bool parse_device_config_internal(const char **str, modem_config_t *dev) {
    if (!expect_object_start(str)) return false;
    
    while (!is_object_end(str)) {
        /* RW Fields - Parse these. Degerler once yerel degiskene parse
         * edilir: modem_config_t packed oldugundan uye adresleri
         * hizasiz olabilir (-Waddress-of-packed-member). */
        uint16_t v16;
        uint32_t v32;
        int32_t i32;

        if (match_key(str, "WebArayuzuPortu")) {
            if (!parse_uint16(str, &v16)) return false;
            /* Port validation */
            if (!validate_port(v16)) {
                return false;
            }
            dev->web_interface_port = v16;
        } else if (match_key(str, "WebIlkVeriZamanAsimi")) {
            if (!parse_uint16(str, &v16)) return false;
            dev->web_first_data_timeout_sec = v16;
        } else if (match_key(str, "WebBostaKalmaZamanAsimi")) {
            if (!parse_uint16(str, &v16)) return false;
            dev->web_idle_timeout_sec = v16;
        } else if (match_key(str, "IEC104IlkVeriZamanAsimi")) {
            if (!parse_uint16(str, &v16)) return false;
            dev->iec104_first_data_timeout_sec = v16;
        } else if (match_key(str, "IEC104BostaKalmaZamanAsimi")) {
            if (!parse_uint16(str, &v16)) return false;
            dev->iec104_idle_timeout_sec = v16;
        } else if (match_key(str, "SimKartPin")) {
            if (!parse_uint16(str, &v16)) return false;
            dev->sim_card_pin = v16;
        } else if (match_key(str, "SimKartAPN")) {
            if (!parse_string(str, dev->apn.apn, MAX_APN_LEN)) return false;
        } else if (match_key(str, "SimKartAPNSifresi")) {
            if (!parse_string(str, dev->apn.user_pass, MAX_APN_PASSWORD_LEN)) return false;
        } else if (match_key(str, "SimKartAPNUsername")) {
            if (!parse_string(str, dev->apn.user_name, MAX_APN_USER_NAME_LEN)) return false;
        } else if (match_key(str, "NtpServer")) {
            if (!parse_string(str, dev->ntp_server, MAX_NTP_SERVER_LEN)) return false;
        } else if (match_key(str, "NtpServerPortu")) {
            if (!parse_uint16(str, &v16)) return false;
            /* Port validation */
            if (!validate_port(v16)) {
                return false;
            }
            dev->ntp_server_port = v16;
        } else if (match_key(str, "Time")) {
            if (!parse_uint32(str, &v32)) return false;
            dev->time = v32;
        } else if (match_key(str, "TimeZone")) {
            if (!parse_int32(str, &i32)) return false;
            dev->time_zone = i32;
        } else if (match_key(str, "PeriyodikModemResetPeriyodu")) {
            if (!parse_uint32(str, &v32)) return false;
            dev->periodic_modem_reset_period = v32;
        } else if (match_key(str, "DevreyeAlinmaZamani")) {
            if (!parse_uint32(str, &v32)) return false;
            dev->commissioning_time = v32;
        }
        /* RO Fields - Skip these (serial_number, production_date, lifetime,
         * modem_firmware_version, rf_firmware_version, installation_date,
         * coordinates) */
        else if (match_key(str, "SeriNumarasi") ||
                 match_key(str, "UretimTarihi") ||
                 match_key(str, "LifeTime") ||
                 match_key(str, "ModemYazilimVeriyonu") ||
                 match_key(str, "RFYazilimVeriyonu") ||
                 match_key(str, "KurulumTarihi") ||
                 match_key(str, "CihazKoordinati")) {
            /* Skip RO field value - these are not written from JSON */
            if (!skip_value(str)) return false;
        }
        /* Unknown fields */
        else {
            if (!skip_unknown_key_value(str)) return false;
        }
        skip_comma(str);
    }
    
    /* Validation */
    if (dev->sim_card_pin > 0) {
        if (!validate_sim_pin(dev->sim_card_pin)) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: SIM PIN validation failed\r\n");
            return false;
        }
    }
    if (!validate_timezone(dev->time_zone)) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Timezone validation failed\r\n");
        return false;
    }
    if (dev->periodic_modem_reset_period > 0) {
        if (!validate_timeout(dev->periodic_modem_reset_period, 3600, 2592000, "Modem reset period")) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Modem reset period validation failed\r\n");
            return false;
        }
    }
    
    return true;
}



/* IEC Hat parse et */
static bool parse_iec_line_config(const char **str, jiec_line_config_t *hat) {
    xprintf("[JSON]   Parsing IEC Hatlar...\r\n");
    if (!expect_object_start(str)) {
        xcprintf(XCOLOR_RED, "[JSON]   ERROR: Expected '{' for Hatlar object\r\n");
        return false;
    }
    while (!is_object_end(str)) {
        if (match_key(str, "inUse")) {
            if (!parse_bool_array(str, hat->in_use, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->ioa_r_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->ioa_s_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->ioa_t_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->ioa_r_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->ioa_s_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->ioa_t_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->ioa_r_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->ioa_s_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->ioa_t_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->ioa_r_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->ioa_s_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->ioa_t_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_r_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_s_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_t_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_r_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_s_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_t_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_R_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_r_rfhab_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_S_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_s_rfhab_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "IOA_T_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->ioa_t_rfhab_varyok, MAX_ARRAYS)) return false;
        } else {
            xprintf("[JSON]   WARNING: Unknown key in Hatlar object, skipping...\r\n");
            if (!skip_unknown_key_value(str)) return false;
        }
        skip_comma(str);
    }
    xprintf("[JSON]   IEC Hatlar parsed successfully\r\n");
    return true;
}

/* IEC Config parse et */
static bool parse_iec_config_internal(const char **str, jiec_config_t *iec)
{
    if (!expect_object_start(str))
    	return false;

    while (!is_object_end(str))
    {
        if (match_key(str, "PeriodicSend"))
        {
            if (!parse_uint32(str, &iec->periodical_send_interval)) {
            	return false;
            }
        }
        else if (match_key(str, "Port"))
        {
            if (!parse_uint16(str, &iec->scada_port)){
            	return false;
            }
            if (!validate_port(iec->scada_port)){
                return false;
            }
        }
        else if (match_key(str, "T0"))
        {
            if (!parse_uint8(str, &iec->t0_timeout))
            	return false;
        }
        else if (match_key(str, "T1"))
        {
            if (!parse_uint8(str, &iec->t1_timeout))
            	return false;
        }
        else if (match_key(str, "T2"))
        {
            if (!parse_uint8(str, &iec->t2_timeout))
            	return false;
        }
        else if (match_key(str, "T3"))
        {
            if (!parse_uint8(str, &iec->t3_timeout))
            	return false;
        }
        else if (match_key(str, "K"))
        {
            if (!parse_uint8(str, &iec->k_max))
            	return false;
        }
        else if (match_key(str, "W"))
        {
            if (!parse_uint8(str, &iec->w_max))
            	return false;
        }
        else if (match_key(str, "OriginatorAddr"))
        {
            if (!parse_uint16(str, &iec->originator_address))
            	return false;
        }
        else if (match_key(str, "CommonAddr"))
        {
            if (!parse_uint8(str, &iec->common_address))
            	return false;
        }
        else if (match_key(str, "SBO"))
        {
            if (!parse_bool(str, &iec->sbo_active))
            	return false;
        }
        else if (match_key(str, "SBOTimeout"))
        {
            if (!parse_uint32(str, &iec->sbo_timeout))
            	return false;
        }
        else if (match_key(str, "AkuUyarisi"))
        {
            if (!parse_uint32(str, &iec->ioa_aku_uyarisi))
            	return false;
        }
        else if (match_key(str, "ModemReset"))
        {
            if (!parse_uint32(str, &iec->ioa_modem_reset))
            	return false;
        }
        else if (match_key(str, "Hatlar"))
        {
            if (!parse_iec_line_config(str, &iec->line))
            	return false;
        }
        else
        {
            if (!skip_unknown_key_value(str))
            	return false;
        }
        skip_comma(str);
    }

    if (!validate_iec_timeouts(iec)) {
    	xcprintf(XCOLOR_RED, "[JSON] ERROR: IEC timeout validation failed\r\n");
        return false;
    }
    if (!validate_iec_windows(iec)) {
    	xcprintf(XCOLOR_RED, "[JSON] ERROR: IEC window validation failed\r\n");
        return false;
    }
    if (!validate_common_address(iec->common_address)) {
    	xcprintf(XCOLOR_RED, "[JSON] ERROR: Common address validation failed\r\n");
        return false;
    }
    if (iec->sbo_active) {
        if (!validate_timeout(iec->sbo_timeout, 1, 300, "SBO")) {
        	xcprintf(XCOLOR_RED, "[JSON] ERROR: SBO timeout validation failed\r\n");
            return false;
        }
    }
    if (!validate_timeout(iec->periodical_send_interval, 1, 86400, "Periodical send")) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Periodical send interval validation failed\r\n");
        return false;
    }
    return true;
}

/* Modbus Hat parse et */
static bool parse_modbus_line_config(const char **str, jmodbus_line_config_t *hat) {
    if (!expect_object_start(str)) return false;
    
    while (!is_object_end(str)) {
        if (match_key(str, "inUse")) {
            if (!parse_bool_array(str, hat->in_use, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->addr_r_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->addr_s_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_ArizaAkimi")) {
            if (!parse_uint32_array(str, hat->addr_t_ariza_akimi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->addr_r_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->addr_s_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_ArizaSuresi")) {
            if (!parse_uint32_array(str, hat->addr_t_ariza_suresi, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->addr_r_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->addr_s_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_ArizaTuru")) {
            if (!parse_uint32_array(str, hat->addr_t_ariza_turu, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->addr_r_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->addr_s_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_AnlikAkim")) {
            if (!parse_uint32_array(str, hat->addr_t_anlik_akim, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->addr_r_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->addr_s_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_EnerjiVarYok")) {
            if (!parse_uint32_array(str, hat->addr_t_enerji_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->addr_r_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->addr_s_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_NominalAkimVarYok")) {
            if (!parse_uint32_array(str, hat->addr_t_nominal_akim_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_R_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->addr_r_rfhab_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_S_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->addr_s_rfhab_varyok, MAX_ARRAYS)) return false;
        } else if (match_key(str, "ADDR_T_RfhabVarYok")) {
            if (!parse_uint32_array(str, hat->addr_t_rfhab_varyok, MAX_ARRAYS)) return false;
        } else {
            /* Bilinmeyen key - atla */
            if (!skip_unknown_key_value(str)) return false;
        }
        skip_comma(str);
    }
    
    return true;
}

/* Modbus Config parse et */
static bool parse_modbus_config_internal(const char **str, jmodbus_configs_t *modbus) {
    if (!expect_object_start(str)) return false;
    while (!is_object_end(str)) {
        if (match_key(str, "CihazID")) {
            if (!parse_uint8(str, &modbus->device_addr)) return false;
        } else if (match_key(str, "SonHataKodu")) {
            if (!parse_uint8(str, &modbus->last_error_code)) return false;
        } else if (match_key(str, "BaudRate")) {
            if (!parse_uint32(str, &modbus->baud_rate)) return false;
        } else if (match_key(str, "Hat")) {
            if (!parse_modbus_line_config(str, &modbus->line)) return false;
        } else {
            if (!skip_unknown_key_value(str)) return false;
        }
        skip_comma(str);
    }
    if (!validate_modbus_device_id(modbus->device_addr)) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Modbus device ID validation failed\r\n");
        return false;
    }
    if (!validate_baud_rate(modbus->baud_rate)) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Modbus baud rate validation failed\r\n");
        return false;
    }
    return true;
}

/* RF Config parse - array format - Direct to rf_feeder_t via pointers.
 *
 * Dogrulama: spec R2 section 3/4 araliklari in-use hatlar icin ZORUNLU
 * tutulur (sinir disi deger REDDEDILIR - cihaza asla gonderilmez, spec
 * section 5.3-2). in-use olmayan hatlar aralik kontrolunden muaf tutulur.
 * Capraz alan (Nominal <-> Ia, spec section 4.6) ve EUI-64 benzersizligi
 * (spec section 1.2) tum array'ler parse edildikten sonra kontrol edilir.
 *
 * Yazimlar staging'e gider (rf_store_get_mutable; cagiran commit/abort
 * yonetir) - yarida kalan parse kalici store'u kirletmez.
 */
static bool rf_eui_duplicates_exist(rf_feeder_t *const *configs)
{
    const uint8_t *slots[MAX_ARRAYS * 3];
    int count = 0;

    for (int i = 0; i < MAX_ARRAYS; i++) {
        if (configs[i] == NULL) { continue; }
        slots[count++] = configs[i]->r_eui64;
        slots[count++] = configs[i]->s_eui64;
        slots[count++] = configs[i]->t_eui64;
    }

    for (int a = 0; a < count; a++) {
        if (rf_eui64_is_zero(slots[a])) { continue; }
        for (int b = a + 1; b < count; b++) {
            if (memcmp(slots[a], slots[b], RF_EUI64_LEN) == 0) { return true; }
        }
    }
    return false;
}

/*
 * Fider benzersizligi (spec R2 section 1.2 / 5.3-1): ayni (Fider_ID, Phase)
 * cifti iki cihaza atanamaz - cakisan adres iki cihazi ayni RF slotuna koyar.
 * Her fider uc fazi da kapsadigindan denetim "in_use fiderlerde HatID tekrari"
 * seklindedir. HatID=0 (provizyonsuz, spec 1.3) muaf tutulur: provizyonsuz
 * cihazlar turetilmis RF adresine girmez (section 6.3).
 */
static bool rf_hatid_duplicates_exist(rf_feeder_t *const *configs)
{
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if ((configs[i] == NULL) || (!configs[i]->in_use)) { continue; }
        if (configs[i]->config.fider_id == 0U) { continue; }

        for (int j = i + 1; j < MAX_ARRAYS; j++) {
            if ((configs[j] == NULL) || (!configs[j]->in_use)) { continue; }

            if (configs[j]->config.fider_id == configs[i]->config.fider_id) {
                xcprintf(XCOLOR_RED,
                         "[VALIDATION] ERROR: Lines %d and %d share Feeder ID %u\r\n",
                         i + 1, j + 1, (unsigned)configs[i]->config.fider_id);
                return true;
            }
        }
    }
    return false;
}

static bool parse_rf_config_internal(const char **str, rf_feeder_t *configs[MAX_POWER_LINE_COUNT]) {
    // Temporary buffers for parsing (reused for each field)
    uint32_t temp_uint[MAX_ARRAYS];
    float temp_float[MAX_ARRAYS];
    char temp_hex[MAX_ARRAYS][RF_EUI64_HEX_LEN];

    if (!expect_object_start(str)) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Expected '{' at start of RF config\r\n");
        return false;
    }

    while (!is_object_end(str)) {
        if (match_key(str, "inUse")) {
            xprintf("[JSON] Parsing inUse array...\r\n");
            if (!expect_array_start(str)) return false;
            int idx = 0;
            const char *s = skip_whitespace(*str);
            while (*s != ']' && idx < MAX_ARRAYS) {
                bool val;
                if (!parse_bool(str, &val)) return false;
                if (configs[idx]) configs[idx]->in_use = val;
                idx++;
                skip_comma(str);
                s = skip_whitespace(*str);
            }
            if (!skip_array_remainder(str)) return false;
            xprintf("[JSON] inUse parsed: %d elements\r\n", idx);

        } else if (match_key(str, "HatID")) {
            xprintf("[JSON] Parsing HatID array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse HatID array\r\n");
                return false;
            }
            /* Fider_ID 0-7 (0 = provizyonsuz, spec R2 section 3.2).
             * Aralik kontrolu in_use farketmeksizin - pasif hatta
             * girilen cöp deger sonradan aktiflesince devrede olur. */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (!validate_rf_uint(temp_uint[i], 0U, 7U, "HatID (Fider_ID)")) {
                        return false;
                    }
                    configs[i]->config.fider_id = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "ZoneID")) {
            xprintf("[JSON] Parsing ZoneID array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ZoneID array\r\n");
                return false;
            }
            /* Zone_ID 0-7 (spec R2 section 3.2). in_use farketmeksizin. */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (!validate_rf_uint(temp_uint[i], 0U, 7U, "ZoneID")) {
                        return false;
                    }
                    configs[i]->config.zone_id = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "R_DEVICEID")) {
            xprintf("[JSON] Parsing R_DEVICEID array (EUI-64 hex)...\r\n");
            if (!parse_str_array(str, temp_hex, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse R_DEVICEID array\r\n");
                return false;
            }
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i] && !rf_eui64_from_hex(configs[i]->r_eui64, temp_hex[i])) {
                    xcprintf(XCOLOR_RED, "[JSON] ERROR: Line %d R_DEVICEID '%s' is not 16-hex or empty\r\n",
                             i + 1, temp_hex[i]);
                    return false;
                }
            }

        } else if (match_key(str, "S_DEVICEID")) {
            xprintf("[JSON] Parsing S_DEVICEID array (EUI-64 hex)...\r\n");
            if (!parse_str_array(str, temp_hex, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse S_DEVICEID array\r\n");
                return false;
            }
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i] && !rf_eui64_from_hex(configs[i]->s_eui64, temp_hex[i])) {
                    xcprintf(XCOLOR_RED, "[JSON] ERROR: Line %d S_DEVICEID '%s' is not 16-hex or empty\r\n",
                             i + 1, temp_hex[i]);
                    return false;
                }
            }

        } else if (match_key(str, "T_DEVICEID")) {
            xprintf("[JSON] Parsing T_DEVICEID array (EUI-64 hex)...\r\n");
            if (!parse_str_array(str, temp_hex, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse T_DEVICEID array\r\n");
                return false;
            }
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i] && !rf_eui64_from_hex(configs[i]->t_eui64, temp_hex[i])) {
                    xcprintf(XCOLOR_RED, "[JSON] ERROR: Line %d T_DEVICEID '%s' is not 16-hex or empty\r\n",
                             i + 1, temp_hex[i]);
                    return false;
                }
            }

        } else if (match_key(str, "CalismaModu")) {
            xprintf("[JSON] Parsing CalismaModu array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse CalismaModu array\r\n");
                return false;
            }
            /* Operating_Mode {0,1} (spec R2 section 4.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_working_mode((uint8_t)temp_uint[i])) {
                        return false;
                    }
                    configs[i]->config.operating_mode = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "SistemNominalAkimi")) {
            xprintf("[JSON] Parsing SistemNominalAkimi array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse SistemNominalAkimi array\r\n");
                return false;
            }
            /* Nominal_Current [2.00, 240/1.2] (spec R2 section 4.6) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 2.0f, 200.0f, "Nominal_Current")) {
                        return false;
                    }
                    configs[i]->config.nominal_current = temp_float[i];
                }
            }

        } else if (match_key(str, "SetEdilebilirActirmaEsikAkimi")) {
            xprintf("[JSON] Parsing SetEdilebilirActirmaEsikAkimi array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse SetEdilebilirActirmaEsikAkimi array\r\n");
                return false;
            }
            /* Ia_Threshold [2.00x1.2, 240.0] (spec R2 section 4.6) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 2.4f, 240.0f, "Ia_Threshold")) {
                        return false;
                    }
                    configs[i]->config.ia_threshold = temp_float[i];
                }
            }

        } else if (match_key(str, "SetEdilebilirAcmaArizaSayisi")) {
            xprintf("[JSON] Parsing SetEdilebilirAcmaArizaSayisi array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse SetEdilebilirAcmaArizaSayisi array\r\n");
                return false;
            }
            /* Set_Count [1,4] (spec R2 section 3.4) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 1U, 4U, "Set_Count")) {
                        return false;
                    }
                    configs[i]->config.set_count = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "ArtimliAkimEsigi")) {
            xprintf("[JSON] Parsing ArtimliAkimEsigi array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ArtimliAkimEsigi array\r\n");
                return false;
            }
            /* di_dt_Threshold [250, 2500] A/s (spec R2 section 3.1 / 7.1) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 250.0f, 2500.0f, "di_dt_Threshold")) {
                        return false;
                    }
                    configs[i]->config.di_dt_threshold = temp_float[i];
                }
            }

        } else if (match_key(str, "HatKopukHatBosta")) {
            xprintf("[JSON] Parsing HatKopukHatBosta array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse HatKopukHatBosta array\r\n");
                return false;
            }
            /* Line_Break_Threshold [0.30, 5.00] A (spec R2 section 3.1) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 0.30f, 5.00f, "Line_Break_Threshold")) {
                        return false;
                    }
                    configs[i]->config.line_break_threshold = temp_float[i];
                }
            }

        } else if (match_key(str, "OluHatAkimiDogrulamaSuresi")) {
            xprintf("[JSON] Parsing OluHatAkimiDogrulamaSuresi array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse OluHatAkimiDogrulamaSuresi array\r\n");
                return false;
            }
            /* Dead_Line_Verify_ms [80, 200] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 80U, 200U, "Dead_Line_Verify_ms")) {
                        return false;
                    }
                    configs[i]->config.dead_line_verify_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "YenilenmeSifirlamaSuresi")) {
            xprintf("[JSON] Parsing YenilenmeSifirlamaSuresi array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse YenilenmeSifirlamaSuresi array\r\n");
                return false;
            }
            /* T_Reclaim_Sec [10, 300] saniye (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 10U, 300U, "T_Reclaim_Sec")) {
                        return false;
                    }
                    configs[i]->config.t_reclaim_sec = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "HatFrekansi")) {
            xprintf("[JSON] Parsing HatFrekansi array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse HatFrekansi array\r\n");
                return false;
            }
            /* Line_Frequency {50, 60} - ayrik kume (spec R2 section 4.2) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_frequency(temp_uint[i])) {
                        return false;
                    }
                    configs[i]->config.line_frequency = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "IsSafety")) {
            xprintf("[JSON] Parsing IsSafety array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse IsSafety array\r\n");
                return false;
            }
            /* Is_Safety [0.100, 0.300] A (spec R2 section 3.1) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 0.100f, 0.300f, "Is_Safety")) {
                        return false;
                    }
                    configs[i]->config.is_safety = temp_float[i];
                }
            }

        } else if (match_key(str, "ThresholdMs")) {
            xprintf("[JSON] Parsing ThresholdMs array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ThresholdMs array\r\n");
                return false;
            }
            /* Threshold_ms [20, 140] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 20U, 140U, "Threshold_ms")) {
                        return false;
                    }
                    configs[i]->config.threshold_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "TMemDeadSec")) {
            xprintf("[JSON] Parsing TMemDeadSec array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse TMemDeadSec array\r\n");
                return false;
            }
            /* T_Mem_Dead_Sec [30, 600] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 30U, 600U, "T_Mem_Dead_Sec")) {
                        return false;
                    }
                    configs[i]->config.t_mem_dead_sec = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "InrushTimerMs")) {
            xprintf("[JSON] Parsing InrushTimerMs array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse InrushTimerMs array\r\n");
                return false;
            }
            /* Inrush_Timer_ms [20, 80] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 20U, 80U, "Inrush_Timer_ms")) {
                        return false;
                    }
                    configs[i]->config.inrush_timer_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "InrushMultiplier")) {
            xprintf("[JSON] Parsing InrushMultiplier array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse InrushMultiplier array\r\n");
                return false;
            }
            /* Inrush_Multiplier [1.00, 15.00] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 1.0f, 15.0f, "Inrush_Multiplier")) {
                        return false;
                    }
                    configs[i]->config.inrush_multiplier = temp_float[i];
                }
            }

        } else if (match_key(str, "SyncTripDelayMs")) {
            xprintf("[JSON] Parsing SyncTripDelayMs array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse SyncTripDelayMs array\r\n");
                return false;
            }
            /* Sync_Trip_Delay_ms [0, 100] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 0U, 100U, "Sync_Trip_Delay_ms")) {
                        return false;
                    }
                    configs[i]->config.sync_trip_delay_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "TripPulseDurationMs")) {
            xprintf("[JSON] Parsing TripPulseDurationMs array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse TripPulseDurationMs array\r\n");
                return false;
            }
            /* Trip_Pulse_Duration_ms [20, 120] (spec R2 section 3.3) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 20U, 120U, "Trip_Pulse_Duration_ms")) {
                        return false;
                    }
                    configs[i]->config.trip_pulse_duration_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "TripMode")) {
            xprintf("[JSON] Parsing TripMode array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse TripMode array\r\n");
                return false;
            }
            /* Trip_Mode {0,1} (spec R2 section 4.4) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    const uint32_t allowed[2] = {0U, 1U};
                    if (configs[i]->in_use && !validate_rf_uint_set(temp_uint[i], allowed, 2, "Trip_Mode")) {
                        return false;
                    }
                    configs[i]->config.trip_mode = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "Inrush100HzRatio")) {
            xprintf("[JSON] Parsing Inrush100HzRatio array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse Inrush100HzRatio array\r\n");
                return false;
            }
            /* Inrush_100Hz_Ratio [10, 40] % (spec R2 section 3.4) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 10U, 40U, "Inrush_100Hz_Ratio")) {
                        return false;
                    }
                    configs[i]->config.inrush_100hz_ratio = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "ClpEnabled")) {
            xprintf("[JSON] Parsing ClpEnabled array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ClpEnabled array\r\n");
                return false;
            }
            /* CLP_Enabled {0,1} (spec R2 section 4.5) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    const uint32_t allowed[2] = {0U, 1U};
                    if (configs[i]->in_use && !validate_rf_uint_set(temp_uint[i], allowed, 2, "CLP_Enabled")) {
                        return false;
                    }
                    configs[i]->config.clp_enabled = (uint8_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "ClpMultiplier")) {
            xprintf("[JSON] Parsing ClpMultiplier array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ClpMultiplier array\r\n");
                return false;
            }
            /* CLP_Multiplier [1.00, 3.00] (spec R2 section 3.5) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 1.0f, 3.0f, "CLP_Multiplier")) {
                        return false;
                    }
                    configs[i]->config.clp_multiplier = temp_float[i];
                }
            }

        } else if (match_key(str, "ClpDurationMs")) {
            xprintf("[JSON] Parsing ClpDurationMs array...\r\n");
            if (!parse_uint32_array(str, temp_uint, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse ClpDurationMs array\r\n");
                return false;
            }
            /* CLP_Duration_MS [100, 12000] (spec R2 section 3.5) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_uint(temp_uint[i], 100U, 12000U, "CLP_Duration_MS")) {
                        return false;
                    }
                    configs[i]->config.clp_duration_ms = (uint16_t)temp_uint[i];
                }
            }

        } else if (match_key(str, "VtripTarget")) {
            xprintf("[JSON] Parsing VtripTarget array...\r\n");
            if (!parse_float_array(str, temp_float, MAX_ARRAYS)) {
                xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to parse VtripTarget array\r\n");
                return false;
            }
            /* Vtrip_Target [24.00, 45.00] V (spec R2 section 3.6) */
            for (int i = 0; i < MAX_ARRAYS; i++) {
                if (configs[i]) {
                    if (configs[i]->in_use && !validate_rf_float(temp_float[i], 24.0f, 45.0f, "Vtrip_Target")) {
                        return false;
                    }
                    configs[i]->config.vtrip_target = temp_float[i];
                }
            }

        } else {
            xprintf("[JSON] WARNING: Unknown key in RF config, skipping...\r\n");
            if (!skip_unknown_key_value(str)) return false;
        }
        skip_comma(str);
    }

    /* Spec R2 3.2/6.3: in_use bir hat provizyonsuz (fider_id=0) olamaz -
     * bu durumda hub'a body[1]=0 itilir ve RF koordinasyona girmez (Y7.4).
     * Tarayici JS'i engelliyordu ama dogrudan POST ile atlanabiliyordu. */
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if ((configs[i] != NULL) && configs[i]->in_use &&
            (configs[i]->config.fider_id == 0U)) {
            xcprintf(XCOLOR_RED,
                     "[VALIDATION] ERROR: Line %d is in use but has no Feeder ID\r\n",
                     i + 1);
            return false;
        }
    }

    /* Capraz dogrulama (spec R2 section 4.6): Ia >= 1.2 x Nominal.
     * Tum array'ler islendikten sonra yapilir - alan sirasi onemsiz. */
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if ((configs[i] != NULL) && configs[i]->in_use) {
            const float nom = configs[i]->config.nominal_current;
            const float ia = configs[i]->config.ia_threshold;
            if (ia < (nom * 1.2f) - 0.001f) {
                xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Line %d: Ia_Threshold %.2f < 1.2 x Nominal (%.2f)\r\n",
                         i + 1, ia, nom);
                return false;
            }
        }
    }

    /* EUI-64 benzersizligi (spec R2 section 1.2): ayni cihaz iki (fider,
     * faz) slotuna baglanamaz. */
    if (rf_eui_duplicates_exist(configs)) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Duplicate EUI-64 in RF config\r\n");
        return false;
    }

    /* Fider benzersizligi (spec R2 section 1.2 / 5.3-1): ayni Fider_ID'yi
     * iki in_use fider alamaz - RF adres cakismasi olusur. */
    if (rf_hatid_duplicates_exist(configs)) {
        xcprintf(XCOLOR_RED, "[VALIDATION] ERROR: Duplicate Feeder ID in RF config\r\n");
        return false;
    }

    xprintf("[JSON] RF config parsed and validated successfully\r\n");
    return true;
}

/* ============================================
 * MAIN CONFIG PARSER
 * ============================================ */

/* ============================================
 * PARTIAL PARSER FUNCTIONS
 * Parse subsections independently
 * ============================================ */

/* Parse only DeviceConfig */
/* ============================================
 * PUBLIC PARSER FUNCTIONS (Simplified Wrappers)
 * ============================================ */

int parse_device_config(const char *json_str, modem_config_t *config) {
    if (!json_str || !config) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: parse_device_config - null parameters\r\n");
        return 0;
    }
    
    xprintf("[JSON] Parsing device config...\r\n");

    const char *str = skip_whitespace(json_str);
    
    const modem_config_t *current = modem_config_get();
    if (current) {
        memcpy(config, current, sizeof(modem_config_t));
    }

    bool result = parse_device_config_internal(&str, config);
    if (!result) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Device config parse failed\r\n");
        return 0;
    }
    
    xprintf("[JSON] Device config parsed successfully\r\n");
    xprintf("[JSON]   WebArayuzuPortu: %u\r\n", config->web_interface_port);
    xprintf("[JSON]   SimKartPin: %u\r\n", config->sim_card_pin);
    xprintf("[JSON]   SimKartAPN: %s\r\n", config->apn.apn);
    xprintf("[JSON]   SimKartAPNUsername: %s\r\n", config->apn.user_name);
    xprintf("[JSON]   SimKartAPNSifresi: %s\r\n", config->apn.user_pass);
    xprintf("[JSON]   NtpServer: %s\r\n", config->ntp_server);
    xprintf("[JSON]   NtpServerPortu: %u\r\n", config->ntp_server_port);
    xprintf("[JSON]   Time: %u\r\n", config->time);
    xprintf("[JSON]   TimeZone: %d\r\n", config->time_zone);
    xprintf("[JSON]   PeriyodikModemResetPeriyodu: %u\r\n", config->periodic_modem_reset_period);
    xprintf("[JSON]   DevreyeAlinmaZamani: %u\r\n", config->commissioning_time);
    
    return 1;
}

int parse_iec_config(const char *json_str, jiec_config_t *iec) {
    if (!json_str || !iec) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: parse_iec_config - null parameters\r\n");
        return 0;
    }
    xprintf("[JSON] Parsing IEC config...\r\n");

    const char *str = skip_whitespace(json_str);
    bool result = parse_iec_config_internal(&str, iec);
    if (!result) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: IEC config parse failed\r\n");
        return 0;
    }
    xprintf("[JSON] IEC config parsed successfully\r\n");
    xprintf("[JSON]   PeriyodikGonderimZamani: %u\r\n", iec->periodical_send_interval );
    xprintf("[JSON]   ScadaIPAdresi: %s\r\n", iec->scada_ip_address);
    xprintf("[JSON]   ScadaPort: %u\r\n", iec->scada_port);
    xprintf("[JSON]   T0TimeoutSuresi: %u\r\n", iec->t0_timeout);
    xprintf("[JSON]   T1TimeoutSuresi: %u\r\n", iec->t1_timeout);
    xprintf("[JSON]   T2TimeoutSuresi: %u\r\n", iec->t2_timeout);
    xprintf("[JSON]   T3TimeoutSuresi: %u\r\n", iec->t3_timeout);
    xprintf("[JSON]   K: %u\r\n", iec->k_max);
    xprintf("[JSON]   W: %u\r\n", iec->w_max);
    xprintf("[JSON]   OriginatorAdresi: %u\r\n", iec->originator_address);
    xprintf("[JSON]   CommonAdres: %u\r\n", iec->common_address);
    xprintf("[JSON]   SBO: %s\r\n", iec->sbo_active ? "true" : "false");
    xprintf("[JSON]   SBOTimeout: %u\r\n", iec->sbo_timeout);
    xprintf("[JSON]   AkuUyarisi: %u\r\n", iec->ioa_aku_uyarisi);
    xprintf("[JSON]   ModemReset: %u\r\n", iec->ioa_modem_reset);
    
    /* Hat bilgilerini de yazdır */
    xprintf("[JSON]   Hatlar.inUse: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%s", iec->line.in_use[i] ? "true" : "false");
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_ArizaAkimi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_ariza_akimi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_ArizaAkimi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_ariza_akimi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_ArizaAkimi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_ariza_akimi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_ArizaSuresi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_ariza_suresi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_ArizaSuresi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_ariza_suresi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_ArizaSuresi: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_ariza_suresi[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_ArizaTuru: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_ariza_turu[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_ArizaTuru: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_ariza_turu[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_ArizaTuru: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_ariza_turu[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_AnlikAkim: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_anlik_akim[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_AnlikAkim: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_anlik_akim[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_AnlikAkim: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_anlik_akim[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_EnerjiVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_enerji_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_EnerjiVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_enerji_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_EnerjiVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_enerji_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_NominalAkimVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_nominal_akim_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_NominalAkimVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_nominal_akim_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_NominalAkimVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_nominal_akim_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_R_RfhabVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_r_rfhab_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_S_RfhabVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_s_rfhab_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");
    
    xprintf("[JSON]   Hatlar.IOA_T_RfhabVarYok: [");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        xprintf("%u", iec->line.ioa_t_rfhab_varyok[i]);
        if (i < MAX_ARRAYS - 1) xprintf(", ");
    }
    xprintf("]\r\n");

    return 1;
}

int parse_modbus_config(const char *json_str, jmodbus_configs_t *modbus) {
    if (!json_str || !modbus) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: parse_modbus_config - null parameters\r\n");
        return 0;
    }
    
    xprintf("[JSON] Parsing Modbus config...\r\n");

    const char *str = skip_whitespace(json_str);
    
    bool result = parse_modbus_config_internal(&str, modbus);
    if (!result) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: Modbus config parse failed\r\n");
        return 0;
    }
    
    xprintf("[JSON] Modbus config parsed successfully\r\n");
    xprintf("[JSON]   CihazAddr: %u\r\n", modbus->device_addr);
    xprintf("[JSON]   SonHataKodu: %u\r\n", modbus->last_error_code);
    xprintf("[JSON]   BaudRate: %u\r\n", modbus->baud_rate);
    return 1;
}

/* rf_feeder_t (tek-blok RAM modeli) -> jayirici_rf_config_t (SoA-JSON modeli)
 * tek satir. Uc ayri yerde tekrarlanan alan-alan kopyanin tek noktasi.
 * Maskeli topoloji (zone/fider) ve butun writable alanlar blk'den gelir. */
static void rf_config_to_jayirici(const rf_feeder_t *src, jayirici_rf_config_t *dst, int i)
{
    const rf_feeder_config_t *b = &src->config;

    dst->in_use[i] = src->in_use;
    dst->hat_id[i] = b->fider_id;
    dst->zone_id[i] = b->zone_id;
    rf_eui64_to_hex(src->r_eui64, dst->r_eui64[i]);
    rf_eui64_to_hex(src->s_eui64, dst->s_eui64[i]);
    rf_eui64_to_hex(src->t_eui64, dst->t_eui64[i]);
    dst->mode[i] = b->operating_mode;
    dst->sistem_nominal_akimi[i] = b->nominal_current;
    dst->set_edilebilir_actirma_esik_akimi[i] = b->ia_threshold;
    dst->set_edilebilir_acma_ariza_sayisi[i] = b->set_count;
    dst->artimli_akim_esigi[i] = b->di_dt_threshold;
    dst->hat_kopuk_hat_bosta[i] = b->line_break_threshold;
    dst->olu_hat_akimi_dogrulama_suresi[i] = b->dead_line_verify_ms;
    dst->yenilenme_sifirlama_suresi[i] = b->t_reclaim_sec;
    dst->hat_frekansi[i] = b->line_frequency;
    dst->is_safety[i] = b->is_safety;
    dst->threshold_ms[i] = b->threshold_ms;
    dst->t_mem_dead_sec[i] = b->t_mem_dead_sec;
    dst->inrush_timer_ms[i] = b->inrush_timer_ms;
    dst->inrush_multiplier[i] = b->inrush_multiplier;
    dst->sync_trip_delay_ms[i] = b->sync_trip_delay_ms;
    dst->trip_pulse_duration_ms[i] = b->trip_pulse_duration_ms;
    dst->trip_mode[i] = b->trip_mode;
    dst->inrush_100hz_ratio[i] = b->inrush_100hz_ratio;
    dst->clp_enabled[i] = b->clp_enabled;
    dst->clp_multiplier[i] = b->clp_multiplier;
    dst->clp_duration_ms[i] = b->clp_duration_ms;
    dst->vtrip_target[i] = b->vtrip_target;
}

int parse_rf_config(const char *json_str, jayirici_rf_config_t *rf) {
    if (!json_str || !rf) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: parse_rf_config - null parameters\r\n");
        return 0;
    }

    xprintf("[JSON] Parsing RF config...\r\n");

    // Get pointers to all rf_feeder_t structures from storage
    rf_feeder_t *configs[MAX_POWER_LINE_COUNT];
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        configs[i] = rf_store_get_mutable((feeder_id_t)i);
        if (!configs[i]) {
            xcprintf(XCOLOR_RED, "[JSON] ERROR: Failed to get mutable config pointer for line %d\r\n", i);
            return 0;
        }
    }

    const char *str = skip_whitespace(json_str);

    bool result = parse_rf_config_internal(&str, configs);
    if (!result) {
        xcprintf(XCOLOR_RED, "[JSON] ERROR: RF config parse failed\r\n");
        return 0;
    }

    // Copy data to jayirici_rf_config_t for backward compatibility
    memset(rf, 0, sizeof(jayirici_rf_config_t));
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if (configs[i]) {
            rf_config_to_jayirici(configs[i], rf, i);
        }
    }

    xprintf("[JSON] RF config parsed successfully\r\n");
    for (int i = 0; i < MAX_ARRAYS; i++) {
        if (rf->in_use[i]) {
            xprintf("[JSON]   Line %d: inUse=%d, HatID=%u, ZoneID=%u\r\n",
                    i + 1, rf->in_use[i], rf->hat_id[i], rf->zone_id[i]);
            xprintf("[JSON]     EUI-64: [%s, %s, %s]\r\n",
                    rf->r_eui64[i], rf->s_eui64[i], rf->t_eui64[i]);
            xprintf("[JSON]     CalismaModu: %u\r\n", rf->mode[i]);
            xprintf("[JSON]     SistemNominalAkimi: %.1f\r\n", rf->sistem_nominal_akimi[i]);
        }
    }
    return 1;
}



/* Device Config Getter/Setter - Now uses data_model */
const modem_config_t* get_device_config(void)
{
    return modem_config_get();
}

void set_device_config(const modem_config_t *config) {
    if(!config) {
    	return;
    }
    modem_config_set(config);
    int res = modem_config_sync();

    xcprintf(res == 0 ? XCOLOR_GREEN : XCOLOR_RED,
			res == 0 ? "[Modem Config] Modem config synchronized successfully\r\n"
					: "[Modem Config] ERROR: Modem config synchronization failed\r\n");
}

/* IEC Config Setter - Converts JSON format to NVRAM format */
void set_iec_config(const jiec_config_t *config)
{
    if(!config) {
    	return;
    }

    // Create local config struct from current NVRAM state
    iec104_config_t config_to_write = {0};
    const iec104_config_t *current_config = iec104_config_get();
    if (current_config) {
        config_to_write = *current_config;  // Copy current config
    }

    // Update fields from JSON config
    // Parse IP string to uint32_t (manual parsing, no sscanf)
    uint32_t ip = 0;
    const char *p = config->scada_ip_address;
    int octets[4] = {0, 0, 0, 0};
    int octet_idx = 0;
    int num = 0;
    
    while (*p && octet_idx < 4) {
        if (*p >= '0' && *p <= '9') {
            num = num * 10 + (*p - '0');
        } else if (*p == '.') {
            octets[octet_idx++] = num;
            num = 0;
        }
        p++;
    }
    if (octet_idx == 3) {  // Last octet without trailing dot
        octets[octet_idx] = num;
        ip = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    }
    config_to_write.scada_ip_address = ip;
    config_to_write.scada_port = config->scada_port;
    config_to_write.periodical_send_interval = config->periodical_send_interval;
    config_to_write.t0_max = config->t0_timeout;
    config_to_write.t1_max = config->t1_timeout;
    config_to_write.t2_max = config->t2_timeout;
    config_to_write.t3_max = config->t3_timeout;
    config_to_write.k_max = config->k_max;
    config_to_write.w_max = config->w_max;
    config_to_write.sbo_execute_timeout = config->sbo_timeout;
    config_to_write.is_sbo_active = config->sbo_active ? 1 : 0;
    config_to_write.originator_address = config->originator_address;
    config_to_write.common_address = config->common_address;
    config_to_write.ioa_aku_uyarisi = iec104_make_ioa_3byte(config->ioa_aku_uyarisi);
    config_to_write.ioa_modem_reset = iec104_make_ioa_3byte(config->ioa_modem_reset);

    // Write global config using config manager
    iec104_config_set(&config_to_write);

    // Write line config fields
    for(int i = 0; i < MAX_ARRAYS; i++)
    {
    	iec104_line_config_t line = {0};

    	if(!config->line.in_use[i])
		{
    		iec104_set_line_config(i, &line);
			continue;
		}

    	line.in_use = true;
		line.ariza_akimi[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_ariza_akimi[i]);
		line.ariza_akimi[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_ariza_akimi[i]);
		line.ariza_akimi[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_ariza_akimi[i]);
		
		line.ariza_suresi[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_ariza_suresi[i]);
		line.ariza_suresi[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_ariza_suresi[i]);
		line.ariza_suresi[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_ariza_suresi[i]);
		
		line.ariza_kalicimi[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_ariza_turu[i]);
		line.ariza_kalicimi[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_ariza_turu[i]);
		line.ariza_kalicimi[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_ariza_turu[i]);
		
		line.anlik_akim[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_anlik_akim[i]);
		line.anlik_akim[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_anlik_akim[i]);
		line.anlik_akim[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_anlik_akim[i]);
		
		line.enerji_varyok[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_enerji_varyok[i]);
		line.enerji_varyok[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_enerji_varyok[i]);
		line.enerji_varyok[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_enerji_varyok[i]);
		
		line.nominal_akim_varyok[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_nominal_akim_varyok[i]);
		line.nominal_akim_varyok[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_nominal_akim_varyok[i]);
		line.nominal_akim_varyok[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_nominal_akim_varyok[i]);
		
		line.rf_haberlesme_varyok[PHASE_L1] = iec104_make_ioa_3byte(config->line.ioa_r_rfhab_varyok[i]);
		line.rf_haberlesme_varyok[PHASE_L2] = iec104_make_ioa_3byte(config->line.ioa_s_rfhab_varyok[i]);
		line.rf_haberlesme_varyok[PHASE_L3] = iec104_make_ioa_3byte(config->line.ioa_t_rfhab_varyok[i]);

		iec104_set_line_config(i, &line);
	}

    int res =  iec104_config_sync();

    xcprintf(res == 0 ? XCOLOR_GREEN : XCOLOR_RED,
    		res == 0 ? "[IEC104] IEC config synchronized successfully\r\n"
    				: "[IEC104] ERROR: IEC config synchronization failed\r\n");
}

/* Modbus Config Setter - Converts JSON format to NVRAM format */
int set_modbus_config(const jmodbus_configs_t *config)
{
    if(!config) {
    	return -1;
    }

    // Create local config struct from current NVRAM state
    modbus_configs_t config_to_write = {0};

    const modbus_configs_t *current_config = modbus_config_get();
    if (current_config) {
        config_to_write = *current_config;  // Copy current config
    }

    // Update fields from JSON config
    config_to_write.device_addr = config->device_addr;
    config_to_write.last_error_code = config->last_error_code;
    config_to_write.baud_rate = config->baud_rate;
    config_to_write.addr_aku_uyarisi = (uint16_t)config->addr_aku_uyarisi;
    config_to_write.addr_modem_reset = (uint16_t)config->addr_modem_reset;

    // Write global config using config manager
    modbus_config_set(&config_to_write);

    // Write line config fields
    for(int i = 0; i < MAX_ARRAYS; i++)
    {
		modbus_line_config_t line = {0};

		if(!config->line.in_use[i])
		{
			modbus_set_line_config(i, &line);
			continue;
		}

		line.in_use = true;
		line.ariza_akimi[PHASE_L1] = config->line.addr_r_ariza_akimi[i];
		line.ariza_akimi[PHASE_L2] = config->line.addr_s_ariza_akimi[i];
		line.ariza_akimi[PHASE_L3] = config->line.addr_t_ariza_akimi[i];
		line.ariza_suresi[PHASE_L1] = config->line.addr_r_ariza_suresi[i];
		line.ariza_suresi[PHASE_L2] = config->line.addr_s_ariza_suresi[i];
		line.ariza_suresi[PHASE_L3] = config->line.addr_t_ariza_suresi[i];
		line.ariza_kalicimi[PHASE_L1] = config->line.addr_r_ariza_turu[i];
		line.ariza_kalicimi[PHASE_L2] = config->line.addr_s_ariza_turu[i];
		line.ariza_kalicimi[PHASE_L3] = config->line.addr_t_ariza_turu[i];
		line.anlik_akim[PHASE_L1] = config->line.addr_r_anlik_akim[i];
		line.anlik_akim[PHASE_L2] = config->line.addr_s_anlik_akim[i];
		line.anlik_akim[PHASE_L3] = config->line.addr_t_anlik_akim[i];
		line.enerji_varyok[PHASE_L1] = config->line.addr_r_enerji_varyok[i];
		line.enerji_varyok[PHASE_L2] = config->line.addr_s_enerji_varyok[i];
		line.enerji_varyok[PHASE_L3] = config->line.addr_t_enerji_varyok[i];
		line.nominal_akim_varyok[PHASE_L1] = config->line.addr_r_nominal_akim_varyok[i];
		line.nominal_akim_varyok[PHASE_L2] = config->line.addr_s_nominal_akim_varyok[i];
		line.nominal_akim_varyok[PHASE_L3] = config->line.addr_t_nominal_akim_varyok[i];
		line.rf_haberlesme_varyok[PHASE_L1] = config->line.addr_r_rfhab_varyok[i];
		line.rf_haberlesme_varyok[PHASE_L2] = config->line.addr_s_rfhab_varyok[i];
		line.rf_haberlesme_varyok[PHASE_L3] = config->line.addr_t_rfhab_varyok[i];

		modbus_set_line_config(i, &line);
    }

    int res =  modbus_config_sync();
	xcprintf(res == 0 ? XCOLOR_GREEN : XCOLOR_RED,
			res == 0 ? "[MODBUS] Modbus config synchronized successfully\r\n"
					: "[MODBUS] ERROR: Modbus config synchronization failed\r\n");

	if (res == 0) {
		modbus_process_notify_config_changed();
		return 0;
	}

	return -1;
}


