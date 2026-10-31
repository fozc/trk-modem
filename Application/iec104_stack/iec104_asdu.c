/**
 * @file iec104_asdu.c
 * @brief Bounds-checked ASDU and little-endian value codec.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_asdu.h"

#include <float.h>
#include <string.h>

_Static_assert(sizeof(float) == 4U, "IEC104 requires binary32");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128, "IEC104 requires IEEE754 binary32");

uint16_t iec104_read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

uint32_t iec104_read_ioa(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
}

void iec104_write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

void iec104_write_ioa(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
}

void iec104_write_float(uint8_t *data, float value)
{
    /* Copy the binary32 representation, without numeric conversion or
     * pointer aliasing. write_ioa writes its low 24 bits; byte 3 adds
     * the high 8 bits, giving a four-byte little-endian float on the wire.
     */
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    iec104_write_ioa(data, bits);
    data[3] = (uint8_t)(bits >> 24U);
}

/** Encode host time fields only here; application code never uses wire offsets. */
static void write_time(uint8_t data[7], const iec104_time_t *time)
{
    iec104_write_u16(data, time->milliseconds);
    data[2] = (uint8_t)(time->minute | (time->invalid ? IEC104_CP56_INVALID : 0U));
    data[3] = (uint8_t)(time->hour | (time->summer_time ? IEC104_CP56_SUMMER_TIME : 0U));
    data[4] = (uint8_t)(time->day | (time->day_of_week << 5U));
    data[5] = time->month;
    data[6] = time->year;
}

bool iec104_point_set_encoded(iec104_point_t *point, uint32_t ioa, uint8_t type, const uint8_t *value, size_t length)
{
    if ((point == NULL) || (value == NULL) || (ioa > 0xFFFFFFU) || (type == 0U) || (length == 0U) ||
        (length > IEC104_POINT_VALUE_MAX))
    {
        return false;
    }
    /* Copy before metadata changes so the source may alias the previous point. */
    memmove(point->value, value, length);
    point->ioa        = ioa;
    point->type       = type;
    point->value_size = length;
    return true;
}

bool iec104_single_command_parse(iec104_single_command_t *command, uint8_t sco)
{
    if ((command == NULL) || ((sco & 0x02U) != 0U))
    {
        return false;
    }
    *command = (iec104_single_command_t){ .state     = (sco & 0x01U) != 0U,
                                          .select    = (sco & 0x80U) != 0U,
                                          .qualifier = (sco >> 2U) & 0x1FU };
    return true;
}

bool iec104_point_set_single_command(iec104_point_t *point, uint32_t ioa, bool state, bool select, uint8_t qualifier)
{
    if (qualifier > 31U)
    {
        return false;
    }
    uint8_t sco = (uint8_t)((state ? 1U : 0U) | (select ? 0x80U : 0U) | (qualifier << 2U));
    return iec104_point_set_encoded(point, ioa, IEC104_TYPE_C_SC_NA_1, &sco, 1U);
}

bool iec104_point_set_single(iec104_point_t *point, uint32_t ioa, bool value, uint8_t quality,
                             const iec104_time_t *time)
{
    if ((point == NULL) || (ioa > 0xFFFFFFU) || ((quality & 0x0FU) != 0U))
    {
        return false;
    }
    iec104_point_t result = { .ioa        = ioa,
                              .type       = time == NULL ? IEC104_TYPE_M_SP_NA_1 : IEC104_TYPE_M_SP_TB_1,
                              .value_size = time == NULL ? 1U : 8U };
    result.value[0]       = (uint8_t)((value ? 1U : 0U) | quality);
    if (time != NULL)
    {
        write_time(&result.value[1], time);
    }
    *point = result;
    return true;
}

bool iec104_point_set_float(iec104_point_t *point, uint32_t ioa, float value, uint8_t quality,
                            const iec104_time_t *time)
{
    if ((point == NULL) || (ioa > 0xFFFFFFU) || ((quality & 0x0EU) != 0U))
    {
        return false;
    }
    iec104_point_t result = { .ioa        = ioa,
                              .type       = time == NULL ? IEC104_TYPE_M_ME_NC_1 : IEC104_TYPE_M_ME_TF_1,
                              .value_size = time == NULL ? 5U : 12U };
    iec104_write_float(result.value, value);
    result.value[4] = quality;
    if (time != NULL)
    {
        write_time(&result.value[5], time);
    }
    *point = result;
    return true;
}

bool iec104_point_set_double(iec104_point_t *point, uint32_t ioa, uint8_t value, uint8_t quality,
                             const iec104_time_t *time)
{
    if ((point == NULL) || (ioa > 0xFFFFFFU) || (value > 3U) || ((quality & 0x0FU) != 0U) || (time == NULL))
    {
        return false;
    }
    iec104_point_t result = { .ioa = ioa, .type = IEC104_TYPE_M_DP_TB_1, .value_size = 8U };
    result.value[0]       = (uint8_t)(value | quality);
    write_time(&result.value[1], time);
    *point = result;
    return true;
}

bool iec104_time_parse(iec104_time_t *time, const uint8_t data[7])
{
    if ((time == NULL) || !iec104_cp56_time_valid(data))
    {
        return false;
    }
    *time = (iec104_time_t){ .milliseconds = iec104_read_u16(data),
                             .minute       = data[2] & 0x3FU,
                             .hour         = data[3] & 0x1FU,
                             .day          = data[4] & 0x1FU,
                             .day_of_week  = data[4] >> 5U,
                             .month        = data[5],
                             .year         = data[6],
                             .summer_time  = (data[3] & IEC104_CP56_SUMMER_TIME) != 0U };
    return true;
}

/** Parse the ASDU portion, NOT the whole TCP APDU/frame.
 * The core passes frame + 6: the 68/length/control prefix is already removed.
 * All offsets below are relative to data[0], i.e. the start of the ASDU.
 * @code
 * offset   width   field
 *   0        1     Type ID (e.g. 1=single point, 100=interrogation)
 *   1        1     VSQ: bit 7=SQ, bits 6..0=object count (1..127)
 *   2        1     COT: bit 7=test, bit 6=P/N, bits 5..0=cause
 *   3        1     OA: originator address (second byte of the 2-byte COT)
 *   4..5     2     CA: common/station address, little-endian
 *   6..end   N     Information-object payload (IOA + value elements)
 *
 *   [ Type | VSQ | cause/flags | OA | CA-low | CA-high | payload ... ]
 *
 * Example: 01 01 03 00 01 00 64 00 00 01
 *          Type=1, SQ=0, count=1, cause=3, OA=0, CA=1, IOA=100, SIQ=1.
 * @endcode
 * This function decodes only the header and borrows the payload bytes.
 * Type-specific widths/count/SQ are checked by iec104_asdu_objects_valid().
 */
bool iec104_asdu_parse(iec104_asdu_t *asdu, const uint8_t *data, size_t length)
{
    if ((asdu == NULL) || (data == NULL) || (length < 6U) || (length > IEC104_CORE_ASDU_MAX) ||
        ((data[1] & 0x7FU) == 0U))
    {
        return false;
    }
    *asdu = (iec104_asdu_t){ .type        = data[0],
                             .count       = data[1] & 0x7FU,
                             .sequence    = (data[1] & 0x80U) != 0U,
                             .cause       = data[2] & 0x3FU,
                             .negative    = (data[2] & 0x40U) != 0U,
                             .test        = (data[2] & 0x80U) != 0U,
                             .originator  = data[3],
                             .address     = iec104_read_u16(&data[4]),
                             .payload     = &data[6],
                             .payload_len = length - 6U };
    return true;
}

/** Check payload extent; value_size excludes the three-byte IOA.
 * @code
 * SQ=0: [IOA0:3][value0:N] [IOA1:3][value1:N] ...
 *       payload length = count * (3 + N)
 * SQ=1: [IOA0:3][value0:N][value1:N] ...
 *       payload length = 3 + count * N
 *       Later IOAs are implicit: IOA0+1, IOA0+2, ... (no 24-bit overflow).
 * @endcode
 */
bool iec104_asdu_objects_valid(const iec104_asdu_t *asdu, size_t value_size)
{
    if ((asdu == NULL) || (asdu->payload == NULL) || (asdu->count == 0U) || (asdu->count > 127U) ||
        (value_size > IEC104_CORE_ASDU_MAX))
    {
        return false;
    }
    size_t expected = (size_t)asdu->count * value_size;
    expected += asdu->sequence ? 3U : (size_t)asdu->count * 3U;
    if (expected != asdu->payload_len)
    {
        return false;
    }
    return !asdu->sequence || (iec104_read_ioa(asdu->payload) <= 0xFFFFFFU - ((uint32_t)asdu->count - 1U));
}

bool iec104_builder_begin(iec104_builder_t *builder, const iec104_asdu_t *header, size_t value_size)
{
    if ((builder == NULL) || (header == NULL) || (value_size > IEC104_CORE_ASDU_MAX - 9U) || (header->cause > 63U))
    {
        return false;
    }
    /* Same ASDU header offsets as iec104_asdu_parse(). Start with count=0;
     * builder_add appends objects and increments the low seven VSQ bits.
     */
    *builder         = (iec104_builder_t){ .length = 6U, .value_size = value_size };
    builder->data[0] = header->type;
    builder->data[1] = header->sequence ? 0x80U : 0U;
    builder->data[2] = (uint8_t)(header->cause | (header->negative ? 0x40U : 0U) | (header->test ? 0x80U : 0U));
    builder->data[3] = header->originator;
    iec104_write_u16(&builder->data[4], header->address);
    return true;
}

bool iec104_builder_add(iec104_builder_t *builder, uint32_t ioa, const uint8_t *value)
{
    if ((builder == NULL) || (value == NULL) || (ioa > 0xFFFFFFU) || (builder->length < 6U) ||
        (builder->length > IEC104_CORE_ASDU_MAX))
    {
        return false;
    }
    uint8_t count = builder->data[1] & 0x7FU;
    bool implied  = (count != 0U) && ((builder->data[1] & 0x80U) != 0U);
    size_t extra  = builder->value_size + (implied ? 0U : 3U);
    if ((count == 127U) || (extra > IEC104_CORE_ASDU_MAX - builder->length) ||
        (implied && (ioa != builder->last_ioa + 1U)))
    {
        return false;
    }
    if (!implied)
    {
        /* SQ=0 writes every IOA; SQ=1 writes only the first object's IOA. */
        iec104_write_ioa(&builder->data[builder->length], ioa);
        builder->length += 3U;
    }
    memcpy(&builder->data[builder->length], value, builder->value_size);
    builder->length += builder->value_size;
    builder->last_ioa = ioa;
    builder->data[1]++;
    return true;
}

/** Clock-set CP56Time2a layout, with offsets relative to time[0].
 * @code
 * byte     bits / value
 * 0..1     milliseconds within the minute (LE, 0..59999)
 * 2        [7:IV] [6:reserved=0] [5..0:minute]
 * 3        [7:SU] [6..5:reserved=0] [4..0:hour]
 * 4        [7..5:day-of-week, 0=unused] [4..0:day-of-month]
 * 5        [7..4:reserved=0] [3..0:month]
 * 6        [7:reserved=0] [6..0:year, 0..99 means 2000..2099]
 * @endcode
 * Seconds and subsecond milliseconds share bytes 0..1; there is no
 * separate second byte. IV=invalid time; SU=summer-time flag.
 * This validates a clock-setting command and rejects IV=1. Monitoring
 * data may legitimately carry IV=1 to report an unavailable source clock.
 */
bool iec104_cp56_time_valid(const uint8_t time[7])
{
    static const uint8_t month_days[] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };
    if ((time == NULL) || (iec104_read_u16(time) > 59999U) || ((time[2] & 0xC0U) != 0U) || (time[2] > 59U) ||
        ((time[3] & 0x60U) != 0U) || ((time[3] & 0x1FU) > 23U) || ((time[5] & 0xF0U) != 0U) || (time[5] == 0U) ||
        (time[5] > 12U) || (time[6] > 99U))
    {
        return false;
    }
    uint8_t days = month_days[time[5] - 1U];
    if ((time[5] == 2U) && ((time[6] % 4U) == 0U))
    {
        days++;
    }
    uint8_t day = time[4] & 0x1FU;
    return (day != 0U) && (day <= days);
}
/*** end of file ***/
