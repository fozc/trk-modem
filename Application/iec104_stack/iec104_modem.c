/**
 * @file iec104_modem.c
 * @brief Existing product data mapping; no second transport or storage queue.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_modem.h"

#include "breaker.h"
#include "cp56time2a.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "nvram.h"
#include "power_board_scp.h"
#include "rf.h"
#include "rtc.h"

#include <string.h>

/** Ordinal category order used by GI enumeration and periodic publication. */
typedef enum
{
    MODEM_LIVE_CURRENT     = 0U,
    MODEM_LIVE_ENERGY      = 1U,
    MODEM_LIVE_LOAD        = 2U,
    MODEM_LIVE_RF          = 3U,
    MODEM_LIVE_TRIP_FAILED = 4U,
    MODEM_LIVE_FIELD_COUNT = 5U
} modem_live_field_t;

/** Select categories independently for each GI group. */
typedef enum
{
    MODEM_FIELDS_CURRENT     = 1U << MODEM_LIVE_CURRENT,
    MODEM_FIELDS_ENERGY      = 1U << MODEM_LIVE_ENERGY,
    MODEM_FIELDS_LOAD        = 1U << MODEM_LIVE_LOAD,
    MODEM_FIELDS_RF          = 1U << MODEM_LIVE_RF,
    MODEM_FIELDS_TRIP_FAILED = 1U << MODEM_LIVE_TRIP_FAILED,
    MODEM_FIELDS_STATUS      = MODEM_FIELDS_ENERGY | MODEM_FIELDS_LOAD | MODEM_FIELDS_RF | MODEM_FIELDS_TRIP_FAILED,
    MODEM_FIELDS_ALL         = MODEM_FIELDS_CURRENT | MODEM_FIELDS_STATUS
} modem_live_fields_t;

/** Product policy only; APCI/ACT_CON/ACT_TERM stay in the server/core. */
typedef struct
{
    uint8_t qoi;
    uint8_t fields;
    bool with_time;
    bool include_battery;
} modem_live_group_t;

/* Change a group's contents here, without changing the data readers. */
static const modem_live_group_t gi_live_groups[] = {
    /* Product GI 20: untimed Type 13 currents and Type 1 status/battery.
     * Source timestamps can exist in the cache but are not sent here.
     */
    { .qoi = IEC104_QOI_STATION, .fields = MODEM_FIELDS_ALL, .with_time = false, .include_battery = true },
    /* GI 21: Type 36 currents, with the original RF receive timestamp. */
    { .qoi = IEC104_QOI_GROUP_1, .fields = MODEM_FIELDS_CURRENT, .with_time = true, .include_battery = false },
    /* GI 22: Type 30 status with time; battery stays untimed Type 1. */
    { .qoi = IEC104_QOI_GROUP_2, .fields = MODEM_FIELDS_STATUS, .with_time = true, .include_battery = true }
};

/* Internal publisher profile, never interpreted as a wire QOI command. */
static const modem_live_group_t spontaneous_live_group = { .fields          = MODEM_FIELDS_ALL,
                                                           .with_time       = true,
                                                           .include_battery = false };

/** Four fields belonging to one entry in the existing fault list. */
typedef enum
{
    MODEM_FAULT_CURRENT     = 0U,
    MODEM_FAULT_DURATION    = 1U,
    MODEM_FAULT_ENERGY      = 2U,
    MODEM_FAULT_LOAD        = 3U,
    MODEM_FAULT_FIELD_COUNT = 4U
} modem_fault_field_t;

#define MODEM_LIVE_POINTS (MAX_POWER_LINE_COUNT * PHASE_MAX * MODEM_LIVE_FIELD_COUNT)
#define MODEM_FAULT_POINTS (MAX_POWER_LINE_COUNT * PHASE_MAX * TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT)
#define MODEM_REPLAY_DELAY_MS 15000U

static void encode_time(uint8_t *data, const cp56time2a_t *time)
{
    /* Write CP56Time2a: ms[0..1], minute/IV[2], hour/SU[3],
     * day/weekday[4], month[5], year[6]. The complete bit map is beside
     * iec104_cp56_time_valid(); milliseconds also encode the seconds.
     */
    iec104_write_u16(data, time->milliseconds);
    data[2] = (uint8_t)(time->minute | (time->iv_bit != 0U ? IEC104_CP56_INVALID : 0U));
    data[3] = (uint8_t)(time->hour | (time->su_bit != 0U ? IEC104_CP56_SUMMER_TIME : 0U));
    data[4] = (uint8_t)(time->day | (time->dow << 5U));
    data[5] = time->month;
    data[6] = time->year;
}

static uint8_t battery_warning(uint32_t now_ms)
{
    power_board_snapshot_t power;
    if (!power_board_get_snapshot(now_ms, &power) || !power.has_summary ||
        ((power.valid_fields & POWER_VALID_ALARMS) == 0U))
    {
        return IEC104_QUALITY_INVALID;
    }
    const uint32_t alarms = (1UL << 0U) | (1UL << 2U) | (1UL << 3U) | (1UL << 4U) | (1UL << 5U) | (1UL << 7U) |
                            (1UL << 8U) | (1UL << 18U) | (1UL << 20U);
    return (power.active_alarms & alarms) != 0U ? 1U : 0U;
}

/** @brief Resolve a live category/phase to its configured host IOA.
 * @param[in] line Feeder IEC104 configuration from the NVRAM RAM image.
 * @param[in] field MODEM_LIVE_* category, excluding FIELD_COUNT.
 * @param[in] phase Zero-based L1/L2/L3 index.
 * @return Configured IOA, or zero for an unknown/unassigned category.
 * @note Performs no Flash read and does not read the measurement value.
 */
static uint32_t live_ioa(const iec104_line_config_t *line, size_t field, size_t phase)
{
    switch (field)
    {
        case MODEM_LIVE_CURRENT:
            return iec104_ioa_3byte_to_uint32(line->anlik_akim[phase]);
        case MODEM_LIVE_ENERGY:
            return iec104_ioa_3byte_to_uint32(line->enerji_varyok[phase]);
        case MODEM_LIVE_LOAD:
            return iec104_ioa_3byte_to_uint32(line->yuk_akimi_varyok[phase]);
        case MODEM_LIVE_RF:
            return iec104_ioa_3byte_to_uint32(line->rf_haberlesme_varyok[phase]);
        case MODEM_LIVE_TRIP_FAILED:
            return iec104_ioa_3byte_to_uint32(line->trip_failed[phase]);
        default:
            return 0U;
    }
}

/** @brief Encode one RF value, quality and original receive time into a point.
 * @param[in,out] point Descriptor with IOA/type/value_size already selected.
 * @param[in] data RF phase snapshot containing a LIVE sample.
 * @param[in] field Requested MODEM_LIVE_* category.
 * @note Builds a timed element layout; untimed groups send only its prefix.
 * Does not refresh the timestamp or submit a frame to the transport.
 */
static void live_value(iec104_point_t *point, const rf_phase_data_t *data, size_t field)
{
    uint8_t quality = 0U;
    if (field == MODEM_LIVE_CURRENT)
    {
        /* Timed layout: binary32[4], QDS[1], CP56Time2a[7]. */
        iec104_write_float(point->value, data->current_valid ? data->live.current_amps : 0.0F);
        quality         = (!data->current_valid || !data->is_online) ? IEC104_QUALITY_INVALID : 0U;
        point->value[4] = (uint8_t)(quality | (!data->is_online ? IEC104_QUALITY_NOT_TOPICAL : 0U));
        encode_time(&point->value[5], &data->received_time);
    }
    else
    {
        bool flag = field == MODEM_LIVE_ENERGY ? ((data->live.flags & 1U) != 0U) :
                    field == MODEM_LIVE_LOAD   ? ((data->live.flags & 2U) != 0U) :
                    field == MODEM_LIVE_RF     ? data->is_online :
                                                 data->trip_failed;
        if ((field == MODEM_LIVE_ENERGY) || (field == MODEM_LIVE_LOAD))
        {
            quality = (!data->is_online || data->uptime_stalled) ? IEC104_QUALITY_INVALID : 0U;
        }
        else if (field == MODEM_LIVE_TRIP_FAILED)
        {
            quality = data->received_time.iv_bit != 0U ? IEC104_QUALITY_INVALID : 0U;
        }
        else
        {
            /* RF off is known, even though the source is stale. */
        }
        /* Timed status layout: SIQ[1], CP56Time2a[7]. */
        point->value[0] = (uint8_t)((flag ? 1U : 0U) | quality |
                                    ((!data->is_online && (field != MODEM_LIVE_TRIP_FAILED)) ?
                                         IEC104_QUALITY_NOT_TOPICAL :
                                         0U));
        encode_time(&point->value[1], &data->received_time);
    }
}

/** @brief Produce one live/battery point selected by a group profile and ordinal.
 * @param[in] modem Current tick and global battery IOA.
 * @param[in] group Selected category mask, time policy and battery inclusion.
 * @param[in] index Stable field/feeder/phase ordinal, not an IOA.
 * @param[out] point Encoded descriptor on VALUE; missing RF is reported with IV.
 * @return VALUE for a point, SKIP for excluded/disabled/zero-IOA positions,
 * or END when the live range and optional battery point are exhausted.
 */
static iec104_point_result_t read_live(iec104_modem_t *modem, const modem_live_group_t *group, size_t index,
                                       iec104_point_t *point)
{
    if (index >= MODEM_LIVE_POINTS)
    {
        if ((index == MODEM_LIVE_POINTS) && group->include_battery && (modem->battery_ioa != 0U))
        {
            *point = (iec104_point_t){ .ioa        = modem->battery_ioa,
                                       .type       = IEC104_TYPE_M_SP_NA_1,
                                       .value_size = 1U,
                                       .value      = { battery_warning(modem->now_ms) } };
            return IEC104_POINT_VALUE;
        }
        return IEC104_POINT_END;
    }
    size_t field             = index / (MAX_POWER_LINE_COUNT * PHASE_MAX);
    size_t feeder            = (index / PHASE_MAX) % MAX_POWER_LINE_COUNT;
    size_t phase             = index % PHASE_MAX;
    const power_line_t *line = breaker_get_power_line_by_idx((uint32_t)feeder);
    if ((line == NULL) || (line->iec104.in_use == 0U) || ((group->fields & (1U << field)) == 0U))
    {
        return IEC104_POINT_SKIP;
    }
    *point = (iec104_point_t){ .ioa        = live_ioa(&line->iec104, field, phase),
                               .type       = field == MODEM_LIVE_CURRENT ?
                                                 (group->with_time ? IEC104_TYPE_M_ME_TF_1 : IEC104_TYPE_M_ME_NC_1) :
                                                 (group->with_time ? IEC104_TYPE_M_SP_TB_1 : IEC104_TYPE_M_SP_NA_1),
                               .value_size = field == MODEM_LIVE_CURRENT ? (group->with_time ? 12U : 5U) :
                                                                           (group->with_time ? 8U : 1U) };
    if (point->ioa == 0U)
    {
        return IEC104_POINT_SKIP;
    }
    rf_phase_data_t data;
    bool has_data = rf_get_phase_data(feeder, (phase_id_t)phase, modem->now_ms, &data);
    if (has_data && data.has_live)
    {
        /* live_value prepares a timed layout. value_size selects only
         * its untimed prefix for GI 20: 5 bytes (current) or 1 (status).
         * The builder copies value_size, never the whole value buffer.
         */
        live_value(point, &data, field);
    }
    else
    {
        size_t quality_index        = field == MODEM_LIVE_CURRENT ? 4U : 0U;
        point->value[quality_index] = IEC104_QUALITY_INVALID;
        if (point->value_size > 5U)
        {
            size_t time_index             = field == MODEM_LIVE_CURRENT ? 5U : 1U;
            point->value[time_index + 2U] = IEC104_CP56_INVALID;
        }
        if ((field == MODEM_LIVE_TRIP_FAILED) && has_data)
        {
            /* A latched alarm may exist before any LIVE packet. */
            cp56time2a_t time = cp56time2a_now();
            point->value[0]   = (uint8_t)((data.trip_failed ? 1U : 0U) |
                                        (time.iv_bit != 0U ? IEC104_QUALITY_INVALID : 0U));
            encode_time(&point->value[1], &time);
        }
    }
    return IEC104_POINT_VALUE;
}

/** @brief Select the existing temporary/permanent fault-list IOA getter.
 * @param[in] fault Record carrying feeder, phase and temporary/permanent kind.
 * @param[in] record List position, zero=newest; replay uses position zero.
 * @param[in] field MODEM_FAULT_* element, excluding FIELD_COUNT.
 * @return Host IOA computed by the existing configuration helper.
 * @note Reads the NVRAM RAM mapping; does not read/consume the stored event.
 */
static uint32_t fault_ioa(const fault_log_t *fault, uint8_t record, uint8_t field)
{
    typedef ioa_3byte_t (*ioa_getter_t)(uint32_t, uint8_t, uint8_t);

    static const ioa_getter_t temporary[MODEM_FAULT_FIELD_COUNT] = {
        [MODEM_FAULT_CURRENT]  = iec104_get_feeder_temporary_fault_ariza_akimi_ioa,
        [MODEM_FAULT_DURATION] = iec104_get_feeder_temporary_fault_ariza_suresi_ioa,
        [MODEM_FAULT_ENERGY]   = iec104_get_feeder_temporary_fault_enerji_varyok_ioa,
        [MODEM_FAULT_LOAD]     = iec104_get_feeder_temporary_fault_yuk_akimi_varyok_ioa
    };
    static const ioa_getter_t permanent[MODEM_FAULT_FIELD_COUNT] = {
        [MODEM_FAULT_CURRENT]  = iec104_get_feeder_permanent_fault_ariza_akimi_ioa,
        [MODEM_FAULT_DURATION] = iec104_get_feeder_permanent_fault_ariza_suresi_ioa,
        [MODEM_FAULT_ENERGY]   = iec104_get_feeder_permanent_fault_enerji_varyok_ioa,
        [MODEM_FAULT_LOAD]     = iec104_get_feeder_permanent_fault_yuk_akimi_varyok_ioa
    };

    ioa_getter_t getter = fault->info.type != 0U ? permanent[field] : temporary[field];

    return iec104_ioa_3byte_to_uint32(getter(fault->info.feeder, fault->info.phase, record));
}

static void fault_value(const fault_log_t *fault, uint8_t field, iec104_point_t *point)
{
    point->type       = field < MODEM_FAULT_ENERGY ? IEC104_TYPE_M_ME_TF_1 : IEC104_TYPE_M_SP_TB_1;
    point->value_size = field < MODEM_FAULT_ENERGY ? 12U : 8U;
    if (field < MODEM_FAULT_ENERGY)
    {
        float value = field == MODEM_FAULT_CURRENT ? fault_log_current_amps(fault) : (float)fault->fault_duration_ms;
        iec104_write_float(point->value, value);
        encode_time(&point->value[5], &fault->tm);
    }
    else
    {
        point->value[0] = field == MODEM_FAULT_ENERGY ? fault->info.power_status : fault->info.nominal_current_status;
        encode_time(&point->value[1], &fault->tm);
    }
}

/** @brief Read and encode one GI 23/24 list field through the fault service.
 * @param[in] qoi GROUP_3=temporary or GROUP_4=permanent list.
 * @param[in] index Ordinal encoding feeder, phase, newest-first record and field.
 * @param[out] point Descriptor on VALUE; event time comes from the stored record.
 * @return VALUE, SKIP or END; ERROR for storage/record-identity failures.
 * @note May access storage via fault_log_read_nth(), but never marks records sent.
 */
static iec104_point_result_t read_fault(uint8_t qoi, size_t index, iec104_point_t *point)
{
    if (index >= MODEM_FAULT_POINTS)
    {
        return IEC104_POINT_END;
    }
    size_t feeder            = index / (PHASE_MAX * TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT);
    uint8_t phase            = (uint8_t)((index / (TEMPORARY_FAULT_COUNT * MODEM_FAULT_FIELD_COUNT)) % PHASE_MAX);
    uint8_t record           = (uint8_t)((index / MODEM_FAULT_FIELD_COUNT) % TEMPORARY_FAULT_COUNT);
    uint8_t field            = (uint8_t)(index % MODEM_FAULT_FIELD_COUNT);
    const power_line_t *line = breaker_get_power_line_by_idx((uint32_t)feeder);
    fault_log_type_t type    = qoi == IEC104_QOI_GROUP_3 ? FAULT_LOG_TYPE_TEMPORARY : FAULT_LOG_TYPE_PERMANENT;
    uint32_t base            = line == NULL ? 0U :
                                              iec104_ioa_3byte_to_uint32(qoi == IEC104_QOI_GROUP_3 ? line->iec104.temporary_fault :
                                                                                                     line->iec104.permanent_fault);
    if ((line == NULL) || (line->iec104.in_use == 0U) || (base == 0U))
    {
        return IEC104_POINT_SKIP;
    }
    fault_log_t fault;
    uint8_t count = qoi == IEC104_QOI_GROUP_3 ? fault_log_get_temp_count((uint8_t)feeder, phase) :
                                                fault_log_get_perm_count((uint8_t)feeder, phase);
    if (record >= count)
    {
        return IEC104_POINT_SKIP;
    }
    if (!fault_log_read_nth((uint8_t)feeder, phase, type, record, &fault))
    {
        return IEC104_POINT_ERROR;
    }
    *point = (iec104_point_t){ 0 };
    /* Never route corrupted/mismatched storage identity to another IOA. */
    if ((fault.info.feeder != feeder) || (fault.info.phase != phase) || (fault.info.type != (uint8_t)type))
    {
        return IEC104_POINT_ERROR;
    }
    point->ioa = fault_ioa(&fault, record, field);
    fault_value(&fault, field, point);
    return point->ioa != 0U ? IEC104_POINT_VALUE : IEC104_POINT_SKIP;
}

/** @brief Dispatch the server's GI point request to live profiles or fault lists.
 * @param[in] context Modem instance registered by iec104_modem_init().
 * @param[in] qoi Accepted interrogation qualifier (20..24).
 * @param[in] index Caller-owned query ordinal; this callback does not advance it.
 * @param[out] point One value/IOA descriptor, not a complete frame.
 * @return Provider result; an unsupported group returns ERROR.
 */
static iec104_point_result_t read_point(void *context, uint8_t qoi, size_t index, iec104_point_t *point)
{
    iec104_modem_t *modem = context;
    if ((qoi == IEC104_QOI_GROUP_3) || (qoi == IEC104_QOI_GROUP_4))
    {
        return read_fault(qoi, index, point);
    }
    for (size_t group = 0U; group < sizeof(gi_live_groups) / sizeof(gi_live_groups[0]); group++)
    {
        if (gi_live_groups[group].qoi == qoi)
        {
            return read_live(modem, &gi_live_groups[group], index, point);
        }
    }
    return IEC104_POINT_ERROR;
}

static bool write_frame(void *context, const uint8_t *frame, size_t length)
{
    iec104_modem_t *modem = context;
    return modem->io.write(modem->io.context, frame, length);
}

static bool set_clock(void *context, const uint8_t time[7])
{
    (void)context;
    uint16_t milliseconds = iec104_read_u16(time);
    rtc_t clock           = { .millisec = (uint16_t)(milliseconds % 1000U),
                              .second   = (uint8_t)(milliseconds / 1000U),
                              .minute   = time[2] & 0x3FU,
                              .hour     = time[3] & 0x1FU,
                              .day      = time[4] & 0x1FU,
                              .month    = time[5] & 0x0FU,
                              .year     = time[6] };
    return rtc_sync(&clock);
}

static void request_reset(void *context)
{
    iec104_modem_t *modem = context;
    modem->io.request_reset(modem->io.context);
}

static void link_event(void *context, iec104_core_event_t event)
{
    iec104_modem_t *modem = context;
    if (event == IEC104_CORE_LINK_STARTED)
    {
        modem->started_ms = modem->now_ms;
    }
    else if (event != IEC104_CORE_LINK_STOPPED)
    {
        modem->replay_loaded  = false;
        modem->replay_waiting = false;
        modem->replay_stage   = 0U;
        (void)iec104_event_log_sync();
        modem->replay_dirty = false;
        if (event != IEC104_CORE_DISCONNECTED)
        {
            modem->io.request_close(modem->io.context);
        }
    }
    else
    {
        /* Preserve publication cursors across STOP/START. */
    }
    if (modem->io.event != NULL)
    {
        modem->io.event(modem->io.context, event);
    }
}

/** @brief Consume the replay record only after its final ASDU is acknowledged.
 * @param[in,out] context Modem instance with the pending journal sequence.
 * @param[in] tag Core ACK tag; must match replay_sequence + 1 (zero is unused).
 * @note Clears the in-flight record and marks journal state dirty for sync.
 * Transport acceptance alone never enters this callback.
 */
static void delivered(void *context, uint32_t tag)
{
    iec104_modem_t *modem = context;
    if (modem->replay_waiting && (tag == (uint32_t)modem->replay_sequence + 1U))
    {
        iec104_event_log_mark_sent(modem->replay_sequence);
        modem->replay_loaded  = false;
        modem->replay_waiting = false;
        modem->replay_stage   = 0U;
        modem->replay_dirty   = true;
    }
}

bool iec104_modem_init(iec104_modem_t *modem, const iec104_modem_io_t *io)
{
    const iec104_config_t *config = nvram_get_iec104_config();
    if ((modem == NULL) || (io == NULL) || (config == NULL) || (io->write == NULL) || (io->request_close == NULL) ||
        (io->request_reset == NULL) || (config->periodical_send_interval > INT32_MAX / 1000U))
    {
        return false;
    }
    iec104_core_config_t link   = { .k     = config->k_max,
                                    .w     = config->w_max,
                                    .t1_ms = (uint32_t)config->t1_max * 1000U,
                                    .t2_ms = (uint32_t)config->t2_max * 1000U,
                                    .t3_ms = ((uint32_t)config->t3_max + 2U) * 1000U };
    iec104_server_config_t app  = { .common_address = config->common_address,
                                    .originator     = config->originator_address,
                                    .reset_ioa      = iec104_ioa_3byte_to_uint32(config->ioa_modem_reset),
                                    .point_limit    = MODEM_FAULT_POINTS };
    iec104_server_io_t services = { .context       = modem,
                                    .write         = write_frame,
                                    .read_point    = read_point,
                                    .set_clock     = set_clock,
                                    .request_reset = request_reset,
                                    .event         = link_event,
                                    .delivered     = delivered };
    if (!iec104_server_init(&modem->server, &link, &app, &services))
    {
        return false;
    }
    modem->io             = *io;
    modem->battery_ioa    = iec104_ioa_3byte_to_uint32(config->ioa_aku_uyarisi);
    modem->period_ms      = config->periodical_send_interval * 1000U;
    modem->replay_loaded  = false;
    modem->replay_waiting = false;
    modem->replay_dirty   = false;
    modem->replay_stage   = 0U;
    modem->replay_ready   = false;
    return true;
}

void iec104_modem_open(iec104_modem_t *modem, uint32_t now_ms)
{
    if (modem != NULL)
    {
        modem->now_ms = now_ms;
        iec104_server_open(&modem->server, now_ms);
        modem->server.query_qoi = 0U;
        /* Initial snapshot starts at RF, followed by trip-failure fields. */
        modem->snapshot_index   = MODEM_LIVE_RF * MAX_POWER_LINE_COUNT * PHASE_MAX;
        modem->snapshot_done    = false;
        modem->periodic_pending = false;
        modem->periodic_since   = now_ms;
        modem->battery_sent     = false;
        modem->replay_ready     = false;
    }
}

void iec104_modem_close(iec104_modem_t *modem)
{
    if (modem != NULL)
    {
        iec104_server_close(&modem->server);
    }
}

size_t iec104_modem_on_data_received(iec104_modem_t *modem, const uint8_t *data, size_t length, uint32_t now_ms)
{
    if (modem == NULL)
    {
        return 0U;
    }
    modem->now_ms = now_ms;
    return iec104_server_process_data(&modem->server, data, length, now_ms);
}

/** @brief Publish one same-type live batch for snapshot/periodic enumeration.
 * @param[in,out] modem Server and current product data sources.
 * @param[in,out] index Next ordinal; advances on accepted data or skipped points.
 * @param[in] end Exclusive ordinal limit for this publication range.
 * @return True once the range is exhausted; false means more work/backpressure,
 * not necessarily a failed send (a partial batch may already be accepted).
 * @note At most 32 positions are scanned and 16 points collected per call.
 */
static bool publish_live(iec104_modem_t *modem, size_t *index, size_t end)
{
    iec104_point_t points[16];
    size_t count  = 0U;
    size_t cursor = *index;
    for (size_t work = 0U; (work < 32U) && (cursor < end); work++)
    {
        iec104_point_t point;
        /* Select all timed categories for spontaneous publication. */
        iec104_point_result_t result = read_live(modem, &spontaneous_live_group, cursor, &point);
        if (result == IEC104_POINT_VALUE)
        {
            if ((count != 0U) && (point.type != points[0].type))
            {
                break;
            }
            points[count++] = point;
            if (count == 16U)
            {
                cursor++;
                break;
            }
        }
        cursor++;
    }
    if ((count == 0U) || iec104_server_publish(&modem->server, points, count, 0U))
    {
        *index = cursor;
    }
    return *index >= end;
}

/** @brief Encode the current durable record's next homogeneous ASDU point set.
 * @param[in] modem Cached record and current two-frame fault replay stage.
 * @param[out] points Up to two descriptors; alarm records produce one.
 * @param[out] count Number of descriptors on success.
 * @param[out] last True when this set completes the record's ASDU sequence.
 * @return False if the record kind/identity/configuration cannot be represented.
 * @note No send or journal consumption occurs here; the final set gets the tag.
 */
static bool replay_points(iec104_modem_t *modem, iec104_point_t points[2], size_t *count, bool *last)
{
    const iec104_event_record_t *record = &modem->replay_record;
    if (record->kind == IEC104_EVENT_TRIP_FAILURE)
    {
        const iec104_alarm_record_t *alarm = &record->payload.alarm;
        const power_line_t *line           = breaker_get_power_line_by_idx(alarm->feeder);
        if ((line == NULL) || (line->iec104.in_use == 0U) || (alarm->phase >= PHASE_MAX))
        {
            return false;
        }
        points[0] = (iec104_point_t){ .type       = IEC104_TYPE_M_SP_TB_1,
                                      .value_size = 8U,
                                      .ioa        = live_ioa(&line->iec104, MODEM_LIVE_TRIP_FAILED, alarm->phase),
                                      .value      = { alarm->active != 0U ? 1U : 0U } };

        encode_time(&points[0].value[1], &alarm->time);
        *count = 1U;
        *last  = true;
        return points[0].ioa != 0U;
    }
    if (record->kind != IEC104_EVENT_FAULT)
    {
        return false;
    }
    const fault_log_t *fault = &record->payload.fault;
    const power_line_t *line = breaker_get_power_line_by_idx(fault->info.feeder);
    if ((line == NULL) || (line->iec104.in_use == 0U) || (fault->info.phase >= PHASE_MAX) ||
        (iec104_ioa_3byte_to_uint32(fault->info.type != 0U ? line->iec104.permanent_fault :
                                                             line->iec104.temporary_fault) == 0U))
    {
        return false;
    }
    for (uint8_t index = 0U; index < 2U; index++)
    {
        uint8_t field = (uint8_t)(modem->replay_stage * 2U + index);
        points[index] = (iec104_point_t){ .ioa = fault_ioa(fault, 0U, field) };
        fault_value(fault, field, &points[index]);
    }
    *count = 2U;
    *last  = modem->replay_stage != 0U;
    return true;
}

/** @brief Progress one newest-first durable record, respecting final-frame ACK.
 * @param[in,out] modem Cache/stage/waiting state for at most one record.
 * @note Keeps the record/stage on backpressure, waits after the final send,
 * and leaves unrepresentable events unsent. Only delivered() consumes storage.
 */
static void replay(iec104_modem_t *modem)
{
    if (modem->replay_waiting)
    {
        return;
    }
    if (!modem->replay_loaded)
    {
        modem->replay_loaded = iec104_event_log_read_newest_unsent(&modem->replay_record, &modem->replay_sequence);
        if (!modem->replay_loaded)
        {
            if (modem->replay_dirty)
            {
                if (iec104_event_log_sync() == 0)
                {
                    modem->replay_dirty = false;
                }
            }
            return;
        }
    }
    iec104_point_t points[2];
    size_t count = 0U;
    bool last    = false;
    if (!replay_points(modem, points, &count, &last))
    {
        /* Keep unrepresentable records durable; never claim delivery. */
        return;
    }
    uint32_t tag = last ? (uint32_t)modem->replay_sequence + 1U : 0U;
    if (iec104_server_publish(&modem->server, points, count, tag))
    {
        modem->replay_waiting = last;
        modem->replay_stage++;
    }
}

void iec104_modem_poll(iec104_modem_t *modem, uint32_t now_ms)
{
    if (modem == NULL)
    {
        return;
    }
    modem->now_ms = now_ms;
    iec104_server_poll(&modem->server, now_ms);
    if ((modem->server.core.state != IEC104_CORE_STARTED) || (modem->server.query_state != IEC104_QUERY_IDLE) ||
        modem->server.response_pending || modem->server.initialization_pending)
    {
        return;
    }
    if (!modem->snapshot_done)
    {
        modem->snapshot_done = publish_live(modem, &modem->snapshot_index, MODEM_LIVE_POINTS);
        return;
    }
    iec104_point_t battery = {
        .ioa = modem->battery_ioa, .type = IEC104_TYPE_M_SP_NA_1, .value_size = 1U, .value = { battery_warning(now_ms) }
    };
    if ((battery.ioa != 0U) && (!modem->battery_sent || (modem->battery_value != battery.value[0])))
    {
        if (iec104_server_publish(&modem->server, &battery, 1U, 0U))
        {
            modem->battery_value = battery.value[0];
            modem->battery_sent  = true;
        }
        return;
    }
    if (modem->replay_ready || ((uint32_t)(now_ms - modem->started_ms) >= MODEM_REPLAY_DELAY_MS) ||
        (modem->server.query_qoi >= IEC104_QOI_GROUP_3))
    {
        replay(modem);
    }
    if ((modem->period_ms != 0U) && ((uint32_t)(now_ms - modem->periodic_since) >= modem->period_ms) &&
        !modem->periodic_pending)
    {
        modem->periodic_pending = true;
        modem->periodic_index   = 0U;
    }
    /* Periodic publication stops before the trip-failure category. */
    if (modem->periodic_pending &&
        publish_live(modem, &modem->periodic_index, MODEM_LIVE_TRIP_FAILED * MAX_POWER_LINE_COUNT * PHASE_MAX))
    {
        modem->periodic_pending = false;
        modem->periodic_since   = now_ms;
    }
}

void iec104_modem_notify_event(iec104_modem_t *modem)
{
    if (modem != NULL)
    {
        modem->replay_ready = true;
    }
}
/*** end of file ***/
