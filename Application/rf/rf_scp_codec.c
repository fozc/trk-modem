/*
 * rf_scp_codec.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Decode R2 payloads without hardware, heap or persistent state.
 */

#include "rf_scp_codec.h"
#include "scp_endian.h"
#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4U && FLT_RADIX == 2 && FLT_MANT_DIG == 24
               && FLT_MAX_EXP == 128, "R1 requires IEEE-754 binary32");

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
         | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static int16_t read_i16(const uint8_t *data)
{
    uint16_t value = scp_unpack_u16(data);
    int32_t signed_value = (int32_t)value;

    if (32767U < value)
    {
        signed_value -= 65536;
    }
    return (int16_t)signed_value;
}

static int8_t read_i8(uint8_t value)
{
    int16_t signed_value = (int16_t)value;

    if (127U < value)
    {
        signed_value = (int16_t)(signed_value - 256);
    }
    return (int8_t)signed_value;
}

static float read_float(const uint8_t *data)
{
    uint32_t bits = read_u32(data);
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint16_t record_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t index = 0U; index < length; index++)
    {
        crc = (uint16_t)(crc ^ ((uint32_t)data[index] << 8U));
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if (0U != (crc & 0x8000U))
            {
                crc = (uint16_t)(((uint32_t)crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)((uint32_t)crc << 1U);
            }
        }
    }
    return crc;
}

uint16_t rf_scp_eui_hash(const uint8_t *eui64)
{
    if (NULL == eui64)
    {
        return 0U;
    }
    const uint16_t crc = record_crc(eui64, 8U);
    return (0U == crc) ? UINT16_MAX : crc;
}

rf_cmd_status_t rf_scp_decode_event(const uint8_t *data, size_t length,
                                      rf_scp_event_t *out)
{
    rf_scp_event_t event = {0};

    if ((NULL == data) || (NULL == out))
    {
        return RF_CMD_ERR_NULL;
    }
    if (RF_SCP_EVENT_SIZE != length)
    {
        return RF_CMD_ERR_LEN;
    }
    if (record_crc(data, 58U) != scp_unpack_u16(&data[58]))
    {
        return RF_CMD_ERR_CRC;
    }
    if (2U < data[55])
    {
        return RF_CMD_ERR_PARAM;
    }
    /* Diagnostic events may contain NaN and non-feeder source fields.
     * Unknown event IDs and invalid wall-clock timestamps are retained.
     */
    (void)memcpy(event.timestamp, data, sizeof(event.timestamp));
    event.event = data[7];
    event.zone = data[8];
    event.feeder = data[9];
    event.phase = data[10];
    event.fault_count = data[11];
    event.soh_ratio = data[12];
    event.nominal_current_status = data[13];
    event.energy_status = data[14];
    event.current_amps = read_float(&data[15]);
    event.di_dt = read_float(&data[19]);
    event.duration_ms = read_u32(&data[23]);
    event.trip_voltage = read_float(&data[27]);
    event.harvest_voltage = read_float(&data[31]);
    event.reference_voltage = read_float(&data[35]);
    event.temperature = read_i16(&data[39]);
    event.permanent_faults = scp_unpack_u16(&data[41]);
    event.temporary_faults = scp_unpack_u16(&data[43]);
    event.soh_idc = read_float(&data[45]);
    event.boot_counter = scp_unpack_u16(&data[49]);
    event.uptime_sec = read_u32(&data[51]);
    event.clock_quality = data[55];
    event.src_eui_hash = scp_unpack_u16(&data[56]);
    event.crc = scp_unpack_u16(&data[58]);
    *out = event;
    return RF_CMD_OK;
}

typedef struct
{
    size_t offset;
    float minimum;
    float maximum;
} float_range_t;

typedef struct
{
    size_t offset;
    uint16_t minimum;
    uint16_t maximum;
} integer_range_t;

rf_cmd_status_t rf_scp_validate_config(const uint8_t *data, size_t length)
{
    static const float_range_t float_ranges[] =
    {
        {3U, 2.0f, 200.0f}, {7U, 5.0f, 240.0f},
        {11U, 0.1f, 0.3f}, {15U, 1.0f, 2200.0f},
        {19U, 0.3f, 5.0f}, {32U, 1.0f, 15.0f},
        {47U, 1.0f, 3.0f}, {53U, 24.0f, 45.0f}
    };
    static const integer_range_t word_ranges[] =
    {
        {24U, 20U, 140U}, {26U, 10U, 300U}, {28U, 30U, 600U},
        {30U, 20U, 80U}, {36U, 80U, 200U}, {38U, 0U, 100U},
        {40U, 20U, 120U}, {51U, 100U, 12000U}
    };
    static const integer_range_t byte_ranges[] =
    {
        {42U, 1U, 4U}, {43U, 0U, 1U}, {44U, 0U, 1U},
        {45U, 10U, 40U}, {46U, 0U, 1U}
    };

    if (NULL == data)
    {
        return RF_CMD_ERR_NULL;
    }
    if (RF_SCP_CONFIG_SIZE != length)
    {
        return RF_CMD_ERR_LEN;
    }
    if ((1U > data[1]) || (4U < data[1]) ||
        ((50U != data[23]) && (60U != data[23])))
    {
        return RF_CMD_ERR_PARAM;
    }
    for (size_t index = 0U;
         index < sizeof(float_ranges) / sizeof(float_ranges[0]); index++)
    {
        const float_range_t *range = &float_ranges[index];
        float value = read_float(&data[range->offset]);

        if (!isfinite(value) || (range->minimum > value) ||
            (range->maximum < value))
        {
            return RF_CMD_ERR_PARAM;
        }
    }
    for (size_t index = 0U;
         index < sizeof(word_ranges) / sizeof(word_ranges[0]); index++)
    {
        const integer_range_t *range = &word_ranges[index];
        uint16_t value = scp_unpack_u16(&data[range->offset]);

        if ((range->minimum > value) || (range->maximum < value))
        {
            return RF_CMD_ERR_PARAM;
        }
    }
    for (size_t index = 0U;
         index < sizeof(byte_ranges) / sizeof(byte_ranges[0]); index++)
    {
        const integer_range_t *range = &byte_ranges[index];
        uint16_t value = data[range->offset];

        if ((range->minimum > value) || (range->maximum < value))
        {
            return RF_CMD_ERR_PARAM;
        }
    }
    /* Use the equivalent 6/5 ratio to retain the 200 A / 240 A limit. */
    if (read_float(&data[7]) * 5.0f < read_float(&data[3]) * 6.0f)
    {
        return RF_CMD_ERR_PARAM;
    }
    return RF_CMD_OK;
}

static rf_cmd_status_t request_length(uint8_t cmd, uint8_t type,
                                     uint8_t *length)
{
    switch (cmd)
    {
        case 0x00U: *length = 0U; break;
        case RF_SCP_CMD_GET_STATUS:
        case RF_SCP_CMD_GET_FRAM_STATS:
        case RF_SCP_CMD_SET_CONFIG:
        case RF_SCP_CMD_INVENTORY_END:
        case RF_SCP_CMD_LOG_READ_HEAD:
        case RF_SCP_CMD_PWR_RESULT:
        case RF_SCP_CMD_PWR_TELEMETRY: *length = 0U; break;
        case RF_SCP_CMD_INVENTORY_SET:
        case RF_SCP_CMD_INVENTORY_UPDATE: *length = 12U; break;
        case RF_SCP_CMD_TIME_SYNC: *length = 7U; break;
        case RF_SCP_CMD_CFG_READ_ALL: *length = 8U; break;
        case RF_SCP_CMD_CFG_WRITE: *length = 104U; break;
        case RF_SCP_CMD_CFG_COMMIT:
        case RF_SCP_CMD_CFG_ABORT:
        case RF_SCP_CMD_CFG_STATUS_GET:
        case RF_SCP_CMD_EPOCH_REFRESH: *length = 1U; break;
        case RF_SCP_CMD_LOG_READ_RECORD:
        case RF_SCP_CMD_LOG_CONSUME_TO:
        case RF_SCP_CMD_PWR_COMMAND: *length = 2U; break;
        case RF_SCP_CMD_LOG_READ_RANGE:
        case RF_SCP_CMD_LOG_CONSUME_IF: *length = 4U; break;
        case RF_SCP_CMD_PWR_CFG2:
            *length = (SCP_TYPE_GET == type) ? 0U : 16U;
            return ((SCP_TYPE_GET == type) || (SCP_TYPE_SET == type))
                 ? RF_CMD_OK : RF_CMD_ERR_TYPE;
        default: return RF_CMD_ERR_UNSUPPORTED;
    }
    uint8_t expected_type = SCP_TYPE_GET;

    if (0x00U == cmd)
    {
        expected_type = SCP_TYPE_PING;
    }
    else if ((RF_SCP_CMD_SET_CONFIG == cmd) ||
             (RF_SCP_CMD_INVENTORY_SET == cmd) ||
             (RF_SCP_CMD_INVENTORY_END == cmd) ||
             (RF_SCP_CMD_INVENTORY_UPDATE == cmd) ||
             (RF_SCP_CMD_TIME_SYNC == cmd) ||
             (RF_SCP_CMD_CFG_WRITE == cmd) ||
             (RF_SCP_CMD_CFG_COMMIT == cmd) ||
             (RF_SCP_CMD_CFG_ABORT == cmd) ||
             (RF_SCP_CMD_EPOCH_REFRESH == cmd) ||
             (RF_SCP_CMD_LOG_CONSUME_TO == cmd) ||
             (RF_SCP_CMD_LOG_CONSUME_IF == cmd) ||
             (RF_SCP_CMD_PWR_COMMAND == cmd))
    {
        expected_type = SCP_TYPE_SET;
    }
    else
    {
        /* Remaining commands are GET requests. */
    }
    return (expected_type == type) ? RF_CMD_OK : RF_CMD_ERR_TYPE;
}

static bool valid_time(const uint8_t *data)
{
    return (60000U > scp_unpack_u16(data)) && (0U == (data[2] & 0x80U))
        && (60U > (data[2] & 0x3FU)) && (24U > (data[3] & 0x1FU))
        && (0U < (data[4] & 0x1FU)) && (31U >= (data[4] & 0x1FU))
        && (0U < data[5]) && (12U >= data[5]) && (99U >= data[6]);
}

static bool valid_power_config(const uint8_t *data)
{
    uint16_t mask = scp_unpack_u16(&data[1]);
    uint8_t rate = data[10];
    uint8_t capacity = data[14];
    uint8_t period = data[15];

    /* Customer fields only: block bytes 9, 13 and 14. Preservation of
     * period bit 7 and first-write capacity require the prior GET state.
     */
    if (0U != (mask & (uint16_t)~0x6200U))
    {
        return false;
    }
    if ((0U != (mask & 0x0200U)) && (0U != rate) && (255U != rate) &&
        ((20U > rate) || (200U < rate)))
    {
        return false;
    }
    if ((0U != (mask & 0x2000U)) &&
        ((7U > capacity) || (54U < capacity)))
    {
        return false;
    }
    if (0U != (mask & 0x4000U))
    {
        return (0U == (period & 0x40U)) && (0U < (period & 0x3FU))
            && (10U >= (period & 0x3FU));
    }
    return true;
}

static rf_cmd_status_t validate_request_body(const scp_packet_t *packet)
{
    const uint8_t *data = packet->data;

    switch (packet->cmd)
    {
        case RF_SCP_CMD_INVENTORY_SET:
        case RF_SCP_CMD_INVENTORY_UPDATE:
            if ((7U < data[0]) || (7U < data[1]) ||
                ((0U == data[1]) &&
                 (RF_SCP_CMD_INVENTORY_SET == packet->cmd)) ||
                (1U > data[2]) || (3U < data[2]))
            {
                return RF_CMD_ERR_PARAM;
            }
            break;
        case RF_SCP_CMD_TIME_SYNC:
            return valid_time(data) ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_CFG_WRITE:
            return rf_scp_validate_config(&data[8], 96U);
        case RF_SCP_CMD_EPOCH_REFRESH:
            return ((1U <= data[0]) && (4U >= data[0]))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_LOG_READ_RECORD:
        case RF_SCP_CMD_LOG_CONSUME_TO:
            return (100U > scp_unpack_u16(data))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_LOG_READ_RANGE:
            return ((100U > scp_unpack_u16(data)) &&
                    (0U < scp_unpack_u16(&data[2])))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_PWR_CFG2:
            if (SCP_TYPE_SET == packet->type)
            {
                return valid_power_config(data)
                     ? RF_CMD_OK : RF_CMD_ERR_PARAM;
            }
            break;
        case RF_SCP_CMD_PWR_COMMAND:
            /* 1..4 are manufacturer service commands, not RTU commands. */
            return ((0U == data[0]) ||
                    ((5U == data[0]) && (0xA5U == data[1])))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        default: break;
    }
    return RF_CMD_OK;
}

rf_cmd_status_t rf_scp_build_packet(const scp_packet_t *request,
                                       scp_packet_t *out)
{
    uint8_t length = 0U;
    rf_cmd_status_t result;

    if ((NULL == request) || (NULL == out))
    {
        return RF_CMD_ERR_NULL;
    }
    result = request_length(request->cmd, request->type, &length);
    if (RF_CMD_OK != result)
    {
        return result;
    }
    if (length != request->data_len)
    {
        return RF_CMD_ERR_LEN;
    }
    result = validate_request_body(request);
    if (RF_CMD_OK != result)
    {
        return result;
    }
    *out = *request;
    out->dst = RF_SCP_ADDR_HUB;
    out->src = RF_SCP_ADDR_RTU;
    return RF_CMD_OK;
}

static rf_cmd_status_t incoming_length(const scp_packet_t *packet)
{
    uint8_t length = 0U;
    bool notification = false;
    bool both = false;

    switch (packet->cmd)
    {
        case 0x00U: length = 0U; break;
        case RF_SCP_CMD_GET_STATUS: length = 25U; break;
        case RF_SCP_CMD_GET_FRAM_STATS: length = 8U; break;
        case RF_SCP_CMD_INVENTORY_SET:
        case RF_SCP_CMD_INVENTORY_END:
        case RF_SCP_CMD_INVENTORY_UPDATE:
        case RF_SCP_CMD_TIME_SYNC:
        case RF_SCP_CMD_CFG_WRITE:
        case RF_SCP_CMD_CFG_COMMIT:
        case RF_SCP_CMD_CFG_ABORT:
        case RF_SCP_CMD_EPOCH_REFRESH: length = 0U; break;
        case RF_SCP_CMD_BOOT_NOTIFY: length = 1U; notification = true; break;
        case RF_SCP_CMD_DISCOVERY_REPORT:
            length = 9U; notification = true; break;
        case RF_SCP_CMD_TRIP_NOTIFY:
            length = 12U; notification = true; break;
        case RF_SCP_CMD_LIVE_DATA:
            length = 33U; notification = true; break;
        case RF_SCP_CMD_ANOMALY_REPORT:
            length = 10U; notification = true; break;
        case RF_SCP_CMD_CFG_STATUS_NOTIFY:
            length = 8U; notification = true; break;
        case RF_SCP_CMD_CFG_STATUS_GET: length = 8U; break;
        case RF_SCP_CMD_LOG_READ_HEAD:
            if (10U > packet->data_len)
            {
                return RF_CMD_ERR_LEN;
            }
            length = packet->data_len;
            break;
        case RF_SCP_CMD_LOG_CONSUME_TO: length = 4U; break;
        case RF_SCP_CMD_LOG_CONSUME_IF: length = 8U; break;
        case RF_SCP_CMD_LOG_AVAILABLE:
            length = 4U; notification = true; break;
        case RF_SCP_CMD_PWR_SUMMARY:
            if (39U > packet->data_len)
            {
                return RF_CMD_ERR_LEN;
            }
            length = packet->data_len;
            notification = true;
            break;
        case RF_SCP_CMD_PWR_ALARM:
            length = 11U; notification = true; break;
        case RF_SCP_CMD_PWR_CFG2:
            if ((1U != packet->data_len) && (23U != packet->data_len))
            {
                return RF_CMD_ERR_LEN;
            }
            length = packet->data_len;
            break;
        case RF_SCP_CMD_PWR_COMMAND: length = 2U; break;
        case RF_SCP_CMD_PWR_RESULT: length = 5U; both = true; break;
        case RF_SCP_CMD_PWR_TELEMETRY: length = 96U; both = true; break;
        case RF_SCP_CMD_LOG_READ_RECORD:
        case RF_SCP_CMD_LOG_READ_RANGE:
            if ((0U == packet->data_len) || (240U < packet->data_len) ||
                (0U != (packet->data_len % RF_SCP_EVENT_SIZE)) ||
                ((RF_SCP_CMD_LOG_READ_RECORD == packet->cmd) &&
                 (RF_SCP_EVENT_SIZE != packet->data_len)))
            {
                return RF_CMD_ERR_LEN;
            }
            length = packet->data_len;
            break;
        default: return RF_CMD_ERR_UNSUPPORTED;
    }
    if (length != packet->data_len)
    {
        return RF_CMD_ERR_LEN;
    }
    uint8_t expected_type = notification ? SCP_TYPE_SET : SCP_TYPE_ACK;

    return ((expected_type == packet->type) ||
            (both && (SCP_TYPE_SET == packet->type)))
         ? RF_CMD_OK : RF_CMD_ERR_TYPE;
}

static void decode_live(const uint8_t *data, rf_scp_live_t *out)
{
    out->source = data[0];
    out->seq = read_u32(&data[1]);
    out->uptime_sec = read_u32(&data[5]);
    out->state = data[9];
    out->fault_count = data[10];
    out->current_amps = read_float(&data[11]);
    out->trip_voltage = read_float(&data[15]);
    out->harvest_voltage = read_float(&data[19]);
    out->temperature = read_i16(&data[23]);
    out->rssi = read_i8(data[25]);
    out->flags = data[26];
    out->fsm_error = (0U != data[27]);
    out->trip_failed = (0U != data[28]);
    out->log_pending = data[29];
    out->log_wrap_low = data[30];
    out->boot_counter = scp_unpack_u16(&data[31]);
}

static void decode_power(const uint8_t *data, rf_scp_power_summary_t *out)
{
    out->seq = data[1];
    out->flags = data[2];
    out->flags2 = data[3];
    out->age_sec = data[4];
    out->source = data[5];
    out->charge_phase = data[6];
    out->session = data[7];
    out->pv_mv = scp_unpack_u16(&data[8]);
    out->dc_mv = scp_unpack_u16(&data[10]);
    out->input_ma = read_i16(&data[12]);
    out->system_mv = scp_unpack_u16(&data[14]);
    out->battery_mv = scp_unpack_u16(&data[16]);
    out->battery_ma = read_i16(&data[18]);
    out->input_power_10mw = read_i16(&data[20]);
    out->battery_power_10mw = read_i16(&data[22]);
    out->system_power_10mw = read_i16(&data[24]);
    out->soc_tenths = read_i16(&data[26]);
    out->soh_percent = data[28];
    out->battery_temperature = read_i8(data[29]);
    out->board_temperature = read_i8(data[30]);
    out->capacity_ah = data[31];
    out->charge_rate_permille = data[32];
    out->last_gasp_count = data[33];
    out->active_alarms = read_u32(&data[34]);
    out->soc_flags = data[38];
}

static bool valid_source(uint8_t source)
{
    uint8_t feeder = (source >> 2U) & 0x07U;
    uint8_t phase = source & 0x03U;

    return (0U < feeder) && (0U < phase);
}

static rf_cmd_status_t decode_notifications(const scp_packet_t *packet,
                                            rf_scp_message_t *out)
{
    const uint8_t *data = packet->data;

    switch (packet->cmd)
    {
        case RF_SCP_CMD_BOOT_NOTIFY: out->body.boot.major = data[0]; break;
        case RF_SCP_CMD_DISCOVERY_REPORT:
            (void)memcpy(out->body.discovery.eui64, data, 8U);
            out->body.discovery.rssi = read_i8(data[8]);
            break;
        case RF_SCP_CMD_LIVE_DATA:
            if (!valid_source(data[0]) || (6U < data[9]) ||
                (1U < data[27]) || (1U < data[28]) || (100U < data[29]))
            {
                return RF_CMD_ERR_PARAM;
            }
            decode_live(data, &out->body.live);
            break;
        case RF_SCP_CMD_TRIP_NOTIFY:
            if (!valid_source(data[3]) || (7U < data[2]))
            {
                return RF_CMD_ERR_PARAM;
            }
            out->body.trip.msg_type = data[0];
            out->body.trip.event = data[1];
            out->body.trip.zone = data[2];
            out->body.trip.source = data[3];
            out->body.trip.fault_count = data[4];
            out->body.trip.flags = data[5];
            out->body.trip.current_amps = read_float(&data[6]);
            out->body.trip.timestamp_ms = scp_unpack_u16(&data[10]);
            break;
        case RF_SCP_CMD_ANOMALY_REPORT:
            if (((0xFFU != data[0]) && !valid_source(data[0])) ||
                (2U < data[1]) || (1U < data[8]))
            {
                return RF_CMD_ERR_PARAM;
            }
            out->body.anomaly.source = data[0];
            out->body.anomaly.state = data[1];
            out->body.anomaly.window = scp_unpack_u16(&data[2]);
            out->body.anomaly.total = read_u32(&data[4]);
            out->body.anomaly.path = data[8];
            break;
        case RF_SCP_CMD_LOG_AVAILABLE:
            out->body.log_available.pending = scp_unpack_u16(data);
            out->body.log_available.head = scp_unpack_u16(&data[2]);
            if ((100U <= out->body.log_available.pending) ||
                (100U <= out->body.log_available.head))
            {
                return RF_CMD_ERR_PARAM;
            }
            break;
        default: return RF_CMD_ERR_UNSUPPORTED;
    }
    return RF_CMD_OK;
}

static rf_cmd_status_t decode_power_family(const scp_packet_t *packet,
                                           rf_scp_message_t *out)
{
    const uint8_t *data = packet->data;

    switch (packet->cmd)
    {
        case RF_SCP_CMD_PWR_SUMMARY:
            if (1U != data[0])
            {
                return RF_CMD_ERR_VERSION;
            }
            decode_power(data, &out->body.power);
            break;
        case RF_SCP_CMD_PWR_ALARM:
            if (1U != data[0])
            {
                return RF_CMD_ERR_VERSION;
            }
            if (3U < data[4])
            {
                return RF_CMD_ERR_PARAM;
            }
            out->body.alarm.seq = data[1];
            out->body.alarm.suppressed = data[2];
            out->body.alarm.code = data[3];
            out->body.alarm.state = data[4];
            out->body.alarm.active = read_u32(&data[5]);
            out->body.alarm.value0 = data[9];
            out->body.alarm.value1 = data[10];
            break;
        case RF_SCP_CMD_PWR_CFG2:
            if (1U == packet->data_len)
            {
                out->body.config_ack.gen = data[0];
                break;
            }
            if (1U != data[0])
            {
                return RF_CMD_ERR_VERSION;
            }
            if (1U < data[17])
            {
                return RF_CMD_ERR_PARAM;
            }
            (void)memcpy(out->body.power_config.block, &data[1], 16U);
            out->body.power_config.echo_valid = (0U != data[17]);
            out->body.power_config.echo_gen = data[18];
            out->body.power_config.mask1 = data[19];
            out->body.power_config.mask2 = data[20];
            out->body.power_config.rejected = scp_unpack_u16(&data[21]);
            break;
        case RF_SCP_CMD_PWR_COMMAND:
            out->body.command_ack.command = data[0];
            out->body.command_ack.seq = data[1];
            break;
        case RF_SCP_CMD_PWR_RESULT:
            out->body.command_result.command = data[0];
            out->body.command_result.seq = data[1];
            out->body.command_result.result = data[2];
            out->body.command_result.transmissions = data[3];
            out->body.command_result.flags = data[4];
            break;
        case RF_SCP_CMD_PWR_TELEMETRY:
            (void)memcpy(out->body.telemetry.bytes, data, 96U);
            break;
        default: return RF_CMD_ERR_UNSUPPORTED;
    }
    return RF_CMD_OK;
}

static rf_cmd_status_t decode_log(const scp_packet_t *packet,
                                 rf_scp_message_t *out)
{
    const uint8_t *data = packet->data;

    switch (packet->cmd)
    {
        case RF_SCP_CMD_LOG_READ_HEAD:
            out->body.log_head.head = scp_unpack_u16(data);
            out->body.log_head.wrap = scp_unpack_u16(&data[2]);
            out->body.log_head.total = read_u32(&data[4]);
            out->body.log_head.tail = scp_unpack_u16(&data[8]);
            return ((100U > out->body.log_head.head) &&
                    (100U > out->body.log_head.tail))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_LOG_CONSUME_TO:
        case RF_SCP_CMD_LOG_CONSUME_IF:
            out->body.log_consume.tail = scp_unpack_u16(data);
            out->body.log_consume.left = scp_unpack_u16(&data[2]);
            if (RF_SCP_CMD_LOG_CONSUME_IF == packet->cmd)
            {
                out->body.log_consume.tail_seq = read_u32(&data[4]);
            }
            return ((100U > out->body.log_consume.tail) &&
                    (100U > out->body.log_consume.left))
                 ? RF_CMD_OK : RF_CMD_ERR_PARAM;
        case RF_SCP_CMD_LOG_READ_RECORD:
        case RF_SCP_CMD_LOG_READ_RANGE:
            /* Preserve each raw record, including reserved bytes. Record
             * CRC is checked per slot by decode_event before consumption.
             */
            out->body.log_batch.count =
                (uint8_t)(packet->data_len / RF_SCP_EVENT_SIZE);
            (void)memcpy(out->body.log_batch.records, data, packet->data_len);
            break;
        default: return RF_CMD_ERR_UNSUPPORTED;
    }
    return RF_CMD_OK;
}

static rf_cmd_status_t decode_system(const scp_packet_t *packet,
                                    rf_scp_message_t *out)
{
    const uint8_t *data = packet->data;

    switch (packet->cmd)
    {
        case RF_SCP_CMD_GET_STATUS:
            return rf_scp_decode_status(packet, &out->body.status);
        case RF_SCP_CMD_GET_FRAM_STATS:
            if (1U < data[6])
            {
                return RF_CMD_ERR_PARAM;
            }
            out->body.fram.degraded = (0U != data[6]);
            if (!out->body.fram.degraded)
            {
                out->body.fram.head = scp_unpack_u16(data);
                out->body.fram.wrap = scp_unpack_u16(&data[2]);
                out->body.fram.free_slots = scp_unpack_u16(&data[4]);
                if (100U <= out->body.fram.head)
                {
                    return RF_CMD_ERR_PARAM;
                }
            }
            break;
        case RF_SCP_CMD_CFG_STATUS_NOTIFY:
        case RF_SCP_CMD_CFG_STATUS_GET:
            if ((4U < data[1]) || (7U < data[2]))
            {
                return RF_CMD_ERR_PARAM;
            }
            out->body.config.group_id = data[0];
            out->body.config.state = data[1];
            out->body.config.member_bitmap = data[2];
            out->body.config.reason = data[3];
            out->body.config.config_crc = scp_unpack_u16(&data[4]);
            out->body.config.attempts = data[6];
            break;
        default:
            if (0U != packet->data_len)
            {
                return RF_CMD_ERR_UNSUPPORTED;
            }
            break;
    }
    return RF_CMD_OK;
}

rf_cmd_status_t rf_scp_decode_message(const scp_packet_t *packet,
                                rf_scp_message_t *out)
{
    rf_scp_message_t message = {0};
    rf_cmd_status_t result;

    if ((NULL == packet) || (NULL == out))
    {
        return RF_CMD_ERR_NULL;
    }
    if ((RF_SCP_ADDR_HUB != packet->src) ||
        (RF_SCP_ADDR_RTU != packet->dst))
    {
        return RF_CMD_ERR_ADDRESS;
    }
    if (0xF0U <= packet->cmd)
    {
        return RF_CMD_ERR_UNSUPPORTED;
    }
    message.cmd = packet->cmd;
    message.type = packet->type;
    if (SCP_TYPE_ERROR == packet->type)
    {
        if ((0U == packet->data_len) || (3U < packet->data_len) ||
            ((RF_SCP_CMD_PWR_CFG2 != packet->cmd) &&
             (1U != packet->data_len)))
        {
            return RF_CMD_ERR_LEN;
        }
        if ((1U < packet->data_len) &&
            !(((RF_SCP_ERR_BUSY == packet->data[0]) &&
               (2U == packet->data_len)) ||
              ((RF_SCP_ERR_INVALID_PARAM == packet->data[0]) &&
               (3U == packet->data_len))))
        {
            return RF_CMD_ERR_LEN;
        }
        message.body.error.code = packet->data[0];
        message.body.error.extra_len = (uint8_t)(packet->data_len - 1U);
        (void)memcpy(message.body.error.extra, &packet->data[1],
                     message.body.error.extra_len);
        result = RF_CMD_OK;
    }
    else
    {
        result = incoming_length(packet);
        if (RF_CMD_OK != result)
        {
            return result;
        }
        if (RF_SCP_CMD_PWR_SUMMARY <= packet->cmd)
        {
            result = decode_power_family(packet, &message);
        }
        else if ((RF_SCP_CMD_LOG_READ_HEAD == packet->cmd) ||
                 (RF_SCP_CMD_LOG_READ_RECORD == packet->cmd) ||
                 (RF_SCP_CMD_LOG_READ_RANGE == packet->cmd) ||
                 (RF_SCP_CMD_LOG_CONSUME_IF == packet->cmd) ||
                 (RF_SCP_CMD_LOG_CONSUME_TO == packet->cmd))
        {
            result = decode_log(packet, &message);
        }
        else if ((RF_SCP_CMD_BOOT_NOTIFY == packet->cmd) ||
                 (RF_SCP_CMD_DISCOVERY_REPORT == packet->cmd) ||
                 (RF_SCP_CMD_TRIP_NOTIFY == packet->cmd) ||
                 (RF_SCP_CMD_LIVE_DATA == packet->cmd) ||
                 (RF_SCP_CMD_ANOMALY_REPORT == packet->cmd) ||
                 (RF_SCP_CMD_LOG_AVAILABLE == packet->cmd))
        {
            result = decode_notifications(packet, &message);
        }
        else
        {
            result = decode_system(packet, &message);
        }
    }
    if (RF_CMD_OK == result)
    {
        *out = message;
    }
    return result;
}

/*** end of file ***/
