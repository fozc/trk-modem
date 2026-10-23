/**
 * @file modbus_line_data.c
 * @brief Feeder block reader for the fixed Modbus register map
 * @author Fatih Ozcan
 *         fatihozcan@gmail.com
 * @date 2026-10-15
 *
 * Resolves one register of the feeder blocks (modbus_map.h layouts) from the
 * RF live cache (rf_get_phase_data) and the fault log (fault_log_*). Feeder
 * n is the RF line whose Fider_ID assignment equals n; phases follow the
 * product L1/L2/L3 order. Sample times are unix seconds derived from the
 * cp56time2a acceptance timestamp stored with the live sample.
 */

#include "modbus_line_data.h"
#include "modbus_map.h"
#include "rf.h"
#include "rf_config.h"
#include "fault_log.h"
#include "cp56time2a.h"
#include "types.h"
#include <string.h>
#include <math.h>

/* Feeder sub-block spans in registers (single derivation from the layouts). */
#define TEMP_REGION_REGS \
    (3U * MODBUS_MAP_FAULT_RECORDS * MODBUS_MAP_RECORD_REGS)

/* High / low 16-bit words of a 32-bit value (ABCD word order). */
static uint16_t high_word(uint32_t value)
{
    return (uint16_t)(value >> 16);
}

static uint16_t low_word(uint32_t value)
{
    return (uint16_t)(value & 0xFFFFU);
}

/* One 16-bit word of an IEEE-754 float, big-endian high word first. */
static uint16_t float_word(float value, bool want_high)
{
    uint32_t bits = 0U;

    (void)memcpy(&bits, &value, sizeof(bits));
    return want_high ? high_word(bits) : low_word(bits);
}

/*
 * Unix seconds from a cp56time2a timestamp, same time base the rest of the
 * product publishes (fault logs, SonHataZamani). Returns 0 - the shared
 * "no valid time" marker - when the timestamp carries the invalid bit or an
 * impossible calendar date: a garbage clock must never surface as a
 * plausible measurement time. Days-from-civil algorithm; valid for every
 * date the RTC can produce (year 2000..2099).
 */
static uint32_t cp56_to_unix(const cp56time2a_t *tm)
{
    if (!cp56time2a_is_valid(tm))
    {
        return 0U;
    }

    const uint32_t month = (uint32_t)cp56time2a_get_month(tm);
    const uint32_t year = 2000U + (uint32_t)cp56time2a_get_year(tm);
    const uint32_t y = year - ((month <= 2U) ? 1U : 0U);
    const uint32_t era = y / 400U;
    const uint32_t yoe = y - (era * 400U);
    const uint32_t mp = (month > 2U) ? (month - 3U) : (month + 9U);
    const uint32_t doy = (((153U * mp) + 2U) / 5U)
                         + (uint32_t)cp56time2a_get_day(tm) - 1U;
    const uint32_t doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;
    const uint32_t days = (era * 146097U) + doe - 719468U;

    return (days * 86400U)
           + ((uint32_t)cp56time2a_get_hour(tm) * 3600U)
           + ((uint32_t)cp56time2a_get_minute(tm) * 60U)
           + (uint32_t)cp56time2a_get_second(tm);
}

/*
 * Feeder n data source: the RF line provisioned with Fider_ID == n. First
 * match wins; the web RF page keeps assignments unique, a duplicate is a
 * configuration error the RF side already rejects at its own door.
 */
static bool resolve_line(uint32_t fider_no, uint32_t *line_index)
{
    for (uint32_t i = 0U; i < MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *feeder_cfg = rf_store_get((feeder_id_t)i);

        if ((feeder_cfg != NULL) && feeder_cfg->in_use &&
            ((uint32_t)feeder_cfg->config.fider_id == fider_no))
        {
            *line_index = i;
            return true;
        }
    }

    return false;
}

/* Phase quality word: defined-ness plus the live-cache validity flags. The
 * sample-time bit is independent of the measurement bits: a live sample
 * taken with an unset/invalid clock is still a valid measurement with an
 * unusable timestamp. */
static uint16_t phase_quality(bool defined, const rf_phase_data_t *data)
{
    uint16_t quality = 0U;

    if (defined)
    {
        quality |= MODBUS_MAP_PH_Q_DEFINED;
    }
    if ((data != NULL) && data->has_live)
    {
        quality |= MODBUS_MAP_PH_Q_HAS_LIVE;
        if (data->is_online)
        {
            quality |= MODBUS_MAP_PH_Q_FRESH;
        }
        if (data->current_valid)
        {
            quality |= MODBUS_MAP_PH_Q_CURRENT_VALID;
        }
        if (data->trip_voltage_valid)
        {
            quality |= MODBUS_MAP_PH_Q_TRIP_V_VALID;
        }
        if (data->harvest_voltage_valid)
        {
            quality |= MODBUS_MAP_PH_Q_HARVEST_V_VALID;
        }
        if (cp56time2a_is_valid(&data->received_time))
        {
            quality |= MODBUS_MAP_PH_Q_TIME_VALID;
        }
    }

    return quality;
}

/*
 * One register of a phase sub-block. rel is the offset inside
 * modbus_phase_map_t; case labels are compile-time layout offsets. Without a
 * live sample every data register reads 0 and the quality word carries only
 * the defined bit - zero values must never be read as measurements. A sample
 * with an unusable RMS still publishes FLOAT32 NaN (quality bit clear).
 */
static void resolve_phase_field(bool defined, const rf_phase_data_t *data,
                                uint16_t rel, uint16_t *value)
{
    const bool has_live = (data != NULL) && data->has_live;

    switch (rel)
    {
        case MODBUS_MAP_PHASE_OFF(kalite):
            *value = phase_quality(defined, data);
            break;
        case MODBUS_MAP_PHASE_OFF(anlik_akim):
            *value = !has_live ? 0U
                    : (!data->current_valid
                           ? float_word(NAN, true)
                           : float_word(data->live.current_amps, true));
            break;
        case MODBUS_MAP_PHASE_OFF(anlik_akim) + 1U:
            *value = !has_live ? 0U
                    : (!data->current_valid
                           ? float_word(NAN, false)
                           : float_word(data->live.current_amps, false));
            break;
        case MODBUS_MAP_PHASE_OFF(anlik_zaman):
        case MODBUS_MAP_PHASE_OFF(enerji_zaman):
        case MODBUS_MAP_PHASE_OFF(yuk_zaman):
        case MODBUS_MAP_PHASE_OFF(rf_zaman):
        case MODBUS_MAP_PHASE_OFF(trip_zaman):
            *value = high_word(has_live
                         ? cp56_to_unix(&data->received_time) : 0U);
            break;
        case MODBUS_MAP_PHASE_OFF(anlik_zaman) + 1U:
        case MODBUS_MAP_PHASE_OFF(enerji_zaman) + 1U:
        case MODBUS_MAP_PHASE_OFF(yuk_zaman) + 1U:
        case MODBUS_MAP_PHASE_OFF(rf_zaman) + 1U:
        case MODBUS_MAP_PHASE_OFF(trip_zaman) + 1U:
            *value = low_word(has_live
                         ? cp56_to_unix(&data->received_time) : 0U);
            break;
        case MODBUS_MAP_PHASE_OFF(enerji_varyok):
            *value = (has_live && (0U != (data->live.flags & 1U))) ? 1U : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(yuk_akimi_varyok):
            *value = (has_live && (0U != (data->live.flags & 2U))) ? 1U : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(rf_haberlesme_varyok):
            *value = ((data != NULL) && data->is_online) ? 1U : 0U;
            break;
    /*
     * BQ-01/BQ-16: the trip-failure alarm opens from an event without
     * requiring a LIVE sample, so its validity is independent of has_live;
     * publish straight from the phase cache.
     */
    case MODBUS_MAP_PHASE_OFF(trip_failed):
        *value = ((data != NULL) && data->trip_failed) ? 1U : 0U;
        break;
        case MODBUS_MAP_PHASE_OFF(trip_gerilimi):
            *value = has_live ? float_word(data->live.trip_voltage, true) : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(trip_gerilimi) + 1U:
            *value = has_live ? float_word(data->live.trip_voltage, false) : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(harvest_gerilimi):
            *value = has_live
                         ? float_word(data->live.harvest_voltage, true) : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(harvest_gerilimi) + 1U:
            *value = has_live
                         ? float_word(data->live.harvest_voltage, false) : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(sicaklik):
            *value = has_live ? (uint16_t)(int16_t)data->live.temperature : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(rssi):
            *value = has_live ? (uint16_t)(int16_t)data->live.rssi : 0U;
            break;
        case MODBUS_MAP_PHASE_OFF(fault_count):
            *value = has_live ? (uint16_t)data->live.fault_count : 0U;
            break;
        default:
            /* Dense layout: only reachable if a member loses its case. */
            *value = 0U;
            break;
    }
}

/*
 * One register of a fault record mirror. Slots beyond the stored record
 * count read back 0 (zaman == 0 marks the empty slot) so a master can
 * bulk-read the whole region in a single FC03 window.
 */
static void resolve_fault_record(uint32_t line, uint8_t phase,
                                 fault_log_type_t type, uint16_t record,
                                 uint16_t rel, uint16_t *value)
{
    fault_log_t log;

    if (!fault_log_read_nth((uint8_t)line, phase, type, (uint8_t)record,
                            &log))
    {
        *value = 0U;
        return;
    }

    switch (rel)
    {
        case MODBUS_MAP_RECORD_OFF(ariza_akimi):
            *value = float_word(fault_log_current_amps(&log), true);
            break;
        case MODBUS_MAP_RECORD_OFF(ariza_akimi) + 1U:
            *value = float_word(fault_log_current_amps(&log), false);
            break;
        case MODBUS_MAP_RECORD_OFF(ariza_suresi_ms):
            *value = high_word(log.fault_duration_ms);
            break;
        case MODBUS_MAP_RECORD_OFF(ariza_suresi_ms) + 1U:
            *value = low_word(log.fault_duration_ms);
            break;
        case MODBUS_MAP_RECORD_OFF(enerji_varyok):
            *value = log.info.power_status ? 1U : 0U;
            break;
        case MODBUS_MAP_RECORD_OFF(yuk_akimi_varyok):
            *value = log.info.nominal_current_status ? 1U : 0U;
            break;
        case MODBUS_MAP_RECORD_OFF(zaman):
            *value = high_word(cp56_to_unix(&log.tm));
            break;
        case MODBUS_MAP_RECORD_OFF(zaman) + 1U:
            *value = low_word(cp56_to_unix(&log.tm));
            break;
        default:
            *value = 0U;
            break;
    }
}

bool modbus_line_data_read(uint16_t reg_addr, uint32_t now_ms, uint16_t *value)
{
    const uint32_t offset = (uint32_t)reg_addr - MODBUS_MAP_LINE_BASE;

    if ((NULL == value) || (offset >= (MODBUS_MAP_LINE_COUNT *
                                       MODBUS_MAP_LINE_STRIDE)))
    {
        return false;
    }

    const uint32_t fider_no = (offset / MODBUS_MAP_LINE_STRIDE) + 1U;
    const uint16_t w = (uint16_t)(offset % MODBUS_MAP_LINE_STRIDE);
    uint32_t line = 0U;
    const bool defined = resolve_line(fider_no, &line);

    *value = 0U;

    if (0U == w)
    {
        /* Feeder quality: defined plus any-phase live evidence. */
        uint16_t quality = 0U;

        if (defined)
        {
            quality |= MODBUS_MAP_FD_Q_DEFINED;
            for (uint8_t phase = 0U; phase < 3U; phase++)
            {
                rf_phase_data_t data;

                if (rf_get_phase_data(line, (phase_id_t)phase, now_ms,
                                      &data) && data.has_live)
                {
                    quality |= MODBUS_MAP_FD_Q_ANY_LIVE;
                    break;
                }
            }
        }
        *value = quality;
    }
    else if ((w >= MODBUS_MAP_FEEDER_OFF(faz))
             && (w < (uint16_t)(MODBUS_MAP_FEEDER_OFF(faz)
                                + (3U * MODBUS_MAP_PHASE_REGS))))
    {
        const uint16_t rel_in =
            (uint16_t)(w - MODBUS_MAP_FEEDER_OFF(faz));
        const uint8_t phase = (uint8_t)(rel_in / MODBUS_MAP_PHASE_REGS);
        const uint16_t rel =
            (uint16_t)(rel_in % MODBUS_MAP_PHASE_REGS);
        rf_phase_data_t data;
        const rf_phase_data_t *data_ptr = NULL;

        if (defined
            && rf_get_phase_data(line, (phase_id_t)phase, now_ms, &data))
        {
            data_ptr = &data;
        }
        resolve_phase_field(defined, data_ptr, rel, value);
    }
    else if ((w >= MODBUS_MAP_FEEDER_OFF(temp_fault_count))
             && (w < MODBUS_MAP_FEEDER_OFF(perm_fault_count)))
    {
        const uint8_t phase =
            (uint8_t)(w - MODBUS_MAP_FEEDER_OFF(temp_fault_count));

        *value = defined ? fault_log_get_temp_count((uint8_t)line, phase) : 0U;
    }
    else if ((w >= MODBUS_MAP_FEEDER_OFF(perm_fault_count))
             && (w < MODBUS_MAP_FEEDER_OFF(reserved_82_99)))
    {
        const uint8_t phase =
            (uint8_t)(w - MODBUS_MAP_FEEDER_OFF(perm_fault_count));

        *value = defined ? fault_log_get_perm_count((uint8_t)line, phase) : 0U;
    }
    else if ((w >= MODBUS_MAP_FEEDER_OFF(temp_fault))
             && (w < (uint16_t)(MODBUS_MAP_FEEDER_OFF(temp_fault)
                                + TEMP_REGION_REGS)))
    {
        const uint16_t rel_in =
            (uint16_t)(w - MODBUS_MAP_FEEDER_OFF(temp_fault));
        const uint8_t phase =
            (uint8_t)(rel_in / (MODBUS_MAP_FAULT_RECORDS
                                * MODBUS_MAP_RECORD_REGS));
        const uint16_t rest =
            (uint16_t)(rel_in % (MODBUS_MAP_FAULT_RECORDS
                                 * MODBUS_MAP_RECORD_REGS));
        const uint8_t record = (uint8_t)(rest / MODBUS_MAP_RECORD_REGS);
        const uint16_t rel = (uint16_t)(rest % MODBUS_MAP_RECORD_REGS);

        if (defined)
        {
            resolve_fault_record(line, phase, FAULT_LOG_TYPE_TEMPORARY,
                                 record, rel, value);
        }
    }
    else if ((w >= MODBUS_MAP_FEEDER_OFF(perm_fault))
             && (w < (uint16_t)(MODBUS_MAP_FEEDER_OFF(perm_fault)
                                + TEMP_REGION_REGS)))
    {
        const uint16_t rel_in =
            (uint16_t)(w - MODBUS_MAP_FEEDER_OFF(perm_fault));
        const uint8_t phase =
            (uint8_t)(rel_in / (MODBUS_MAP_FAULT_RECORDS
                                * MODBUS_MAP_RECORD_REGS));
        const uint16_t rest =
            (uint16_t)(rel_in % (MODBUS_MAP_FAULT_RECORDS
                                 * MODBUS_MAP_RECORD_REGS));
        const uint8_t record = (uint8_t)(rest / MODBUS_MAP_RECORD_REGS);
        const uint16_t rel = (uint16_t)(rest % MODBUS_MAP_RECORD_REGS);

        if (defined)
        {
            resolve_fault_record(line, phase, FAULT_LOG_TYPE_PERMANENT,
                                 record, rel, value);
        }
    }
    else
    {
        /* Reserved gap: reads back as 0, bulk reads never fail. */
    }

    return true;
}

/*** end of file ***/
