/*
 * rf_scp_codec.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Stateless R2 request validation and inbound payload decoding.
 */

#ifndef RF_SCP_CODEC_H
#define RF_SCP_CODEC_H

#include "rf_scp.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_SCP_EVENT_SIZE 60U
#define RF_SCP_CONFIG_SIZE 96U
#define RF_SCP_BATCH_MAX 4U

typedef struct
{
    uint8_t source;
    uint32_t seq;
    uint32_t uptime_sec;
    uint8_t state;
    uint8_t fault_count;
    float current_amps;
    float trip_voltage;
    float harvest_voltage;
    int16_t temperature;
    int8_t rssi;
    uint8_t flags;
    bool fsm_error;
    bool trip_failed;
    uint8_t log_pending;
    uint8_t log_wrap_low;
    uint16_t boot_counter;
} rf_scp_live_t;

typedef struct
{
    uint8_t msg_type;
    uint8_t event;
    uint8_t zone;
    uint8_t source;
    uint8_t fault_count;
    uint8_t flags;
    float current_amps;
    uint16_t timestamp_ms;
} rf_scp_trip_t;

typedef struct
{
    uint8_t group_id;
    uint8_t state;
    uint8_t member_bitmap;
    uint8_t reason;
    uint16_t config_crc;
    uint8_t attempts;
} rf_scp_config_status_t;

typedef struct
{
    uint8_t source;
    uint8_t state;
    uint16_t window;
    uint32_t total;
    uint8_t path;
} rf_scp_anomaly_t;

typedef struct
{
    uint8_t seq;
    uint8_t flags;
    uint8_t flags2;
    uint8_t age_sec;
    uint8_t source;
    uint8_t charge_phase;
    uint8_t session;
    uint16_t pv_mv;
    uint16_t dc_mv;
    int16_t input_ma;
    uint16_t system_mv;
    uint16_t battery_mv;
    int16_t battery_ma;
    int16_t input_power_10mw;
    int16_t battery_power_10mw;
    int16_t system_power_10mw;
    int16_t soc_tenths;
    uint8_t soh_percent;
    int8_t battery_temperature;
    int8_t board_temperature;
    uint8_t capacity_ah;
    uint8_t charge_rate_permille;
    uint8_t last_gasp_count;
    uint32_t active_alarms;
    uint8_t soc_flags;
} rf_scp_power_summary_t;

typedef struct
{
    uint8_t timestamp[7];
    uint8_t event;
    uint8_t zone;
    uint8_t feeder;
    uint8_t phase;
    uint8_t fault_count;
    uint8_t soh_ratio;
    uint8_t nominal_current_status;
    uint8_t energy_status;
    float current_amps;
    float di_dt;
    uint32_t duration_ms;
    float trip_voltage;
    float harvest_voltage;
    float reference_voltage;
    int16_t temperature;
    uint16_t permanent_faults;
    uint16_t temporary_faults;
    float soh_idc;
    uint16_t boot_counter;
    uint32_t uptime_sec;
    uint8_t clock_quality;
    uint16_t src_eui_hash;
    uint16_t crc;
} rf_scp_event_t;

typedef struct
{
    uint8_t cmd;
    uint8_t type;
    union
    {
        rf_hub_status_t status;
        rf_scp_live_t live;
        rf_scp_trip_t trip;
        rf_scp_config_status_t config;
        rf_scp_power_summary_t power;
        struct
        {
            uint8_t major;
        } boot;
        struct
        {
            uint8_t eui64[8];
            int8_t rssi;
        } discovery;
        rf_scp_anomaly_t anomaly;
        struct
        {
            uint16_t head;
            uint16_t wrap;
            uint16_t free_slots;
            bool degraded;
        } fram;
        struct
        {
            uint16_t head;
            uint16_t wrap;
            uint32_t total;
            uint16_t tail;
        } log_head;
        struct
        {
            uint16_t pending;
            uint16_t head;
        } log_available;
        struct
        {
            uint16_t tail;
            uint16_t left;
            uint32_t tail_seq;
        } log_consume;
        struct
        {
            uint8_t count;
            uint8_t records[240];
        } log_batch;
        struct
        {
            uint8_t seq;
            uint8_t suppressed;
            uint8_t code;
            uint8_t state;
            uint32_t active;
            uint8_t value0;
            uint8_t value1;
        } alarm;
        struct
        {
            uint8_t block[16];
            bool echo_valid;
            uint8_t echo_gen;
            uint8_t mask1;
            uint8_t mask2;
            uint16_t rejected;
        } power_config;
        struct
        {
            uint8_t command;
            uint8_t seq;
        } command_ack;
        struct
        {
            uint8_t command;
            uint8_t seq;
            uint8_t result;
            uint8_t transmissions;
            uint8_t flags;
        } command_result;
        struct
        {
            uint8_t bytes[96];
        } telemetry;
        struct
        {
            uint8_t code;
            uint8_t extra_len;
            uint8_t extra[2];
        } error;
        struct
        {
            uint8_t gen;
        } config_ack;
    } body;
} rf_scp_message_t;

/* All APIs leave the output untouched on failure. Requests preserve SEQ.
 * Decode accepts only MH -> RTU ACK/ERROR/SET. It does not send replies.
 * Command-specific state, deadlines and validity flags belong to services.
 * Unsupported R1 requests (0x03, 0x20) may be built for diagnostics only.
 */
rf_cmd_status_t rf_scp_build_packet(const scp_packet_t *request,
                                    scp_packet_t *out);
rf_cmd_status_t rf_scp_decode_message(const scp_packet_t *packet,
                                      rf_scp_message_t *out);
rf_cmd_status_t rf_scp_decode_event(const uint8_t *data, size_t length,
                                    rf_scp_event_t *out);
rf_cmd_status_t rf_scp_validate_config(const uint8_t *data, size_t length);
/** R2 source summary: CCITT-FALSE over MSB-first EUI-64; zero maps to FFFF. */
uint16_t rf_scp_eui_hash(const uint8_t *eui64);

#ifdef __cplusplus
}
#endif

#endif /* RF_SCP_CODEC_H */

/*** end of file ***/
