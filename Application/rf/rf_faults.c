/*
 * rf_faults.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Keep phase outcomes separate from feeder-level opening counts.
 */
#include "rf_faults.h"
#include "cp56time2a.h"
#include "rf_inventory.h"
#include <string.h>

#define RF_ZONES 8U
#define RF_FEEDERS 4U
#define RF_OPENINGS 128U

typedef struct
{
    uint16_t boot_counter;
    uint32_t uptime_sec;
    uint8_t zone;
    bool has_event;
    bool breaker_opened;
    bool permanent;
    uint8_t eui64[8];
} fault_sequence_t;

typedef struct
{
    cp56time2a_t time;
    uint8_t zone;
    uint8_t feeder;
    rf_fault_class_t type;
    bool opened;
} fault_opening_t;

/* Cooperative process ownership; no ISR accesses. */
static fault_sequence_t sequences[RF_FEEDERS][3];
static rf_fault_stats_t stats[RF_ZONES][RF_FEEDERS + 1U];
static fault_opening_t openings[RF_OPENINGS];
static bool permanent_feeders[RF_ZONES][RF_FEEDERS];
static cp56time2a_t permanent_time[RF_ZONES][RF_FEEDERS];
static size_t opening_count;
static size_t opening_next;

void rf_faults_reset_sequences(void)
{
    (void)memset(sequences, 0, sizeof(sequences));
    (void)memset(permanent_feeders, 0, sizeof(permanent_feeders));
}

void rf_faults_init(void)
{
    rf_faults_reset_sequences();
    (void)memset(stats, 0, sizeof(stats));
    opening_count = 0U;
    opening_next = 0U;
}

void rf_faults_reset_phase(uint8_t feeder, uint8_t phase)
{
    if ((1U <= feeder) && (RF_FEEDERS >= feeder) &&
        (1U <= phase) && (3U >= phase))
    {
        sequences[feeder - 1U][phase - 1U] = (fault_sequence_t){0};
    }
}

static void increment(uint32_t *value)
{
    if (UINT32_MAX > *value)
    {
        (*value)++;
    }
}

static bool count_opening(const rf_scp_event_t *event, rf_fault_class_t type)
{
    rf_fault_stats_t *count = &stats[event->zone][event->feeder];
    cp56time2a_t time;
    (void)memcpy(&time, event->timestamp, sizeof(time));
    if ((1U != event->clock_quality) || !cp56time2a_is_valid(&time))
    {
        /* BQ-20: no invented exact match when timing evidence is absent. */
        increment(&count->uncertain);
        return false;
    }
    for (size_t index = 0U; index < opening_count; index++)
    {
        fault_opening_t *opening = &openings[index];
        if ((opening->zone == event->zone) &&
            (opening->feeder == event->feeder) && (opening->type == type))
        {
            const int32_t difference = cp56time2a_diff_ms(&time, &opening->time);
            if ((-1000 <= difference) && (1000 >= difference))
            {
                const bool opened = opening->opened;
                opening->opened = opened || (1U == event->event) ||
                                  (7U == event->event);
                return opened;
            }
        }
    }
    openings[opening_next] = (fault_opening_t)
    {
        .time = time, .zone = event->zone, .feeder = event->feeder, .type = type,
        .opened = (1U == event->event) || (7U == event->event)
    };
    opening_next = (opening_next + 1U) % RF_OPENINGS;
    if (RF_OPENINGS > opening_count)
    {
        opening_count++;
    }
    increment((RF_FAULT_PERMANENT == type) ? &count->permanent :
                                          &count->temporary);
    return false;
}

static bool is_sequence_event(uint8_t event)
{
    switch (event)
    {
        case 0U:
        case 1U:
        case 3U:
        case 4U:
        case 5U:
        case 6U:
        case 7U:
        case 100U:
        case 101U:
        case 104U:
            return true;
        default:
            return false;
    }
}

static rf_fault_class_t classify_sequence(const rf_scp_event_t *event,
                                           fault_sequence_t *sequence)
{
    rf_fault_class_t type = RF_FAULT_NONE;
    bool *permanent = &permanent_feeders[event->zone][event->feeder - 1U];
    cp56time2a_t time;
    (void)memcpy(&time, event->timestamp, sizeof(time));
    switch (event->event)
    {
        case 4U:
        case 5U:
            if ((1U == event->clock_quality) && cp56time2a_is_valid(&time) &&
                (!*permanent || (0 < cp56time2a_diff_ms(&time,
                    &permanent_time[event->zone][event->feeder - 1U]))))
            {
                *permanent = false;
            }
            sequence->breaker_opened = false;
            sequence->permanent = false;
            break;
        case 0U:
        case 104U:
            *permanent = false;
            sequence->breaker_opened = false;
            sequence->permanent = false;
            break;
        case 6U:
            sequence->breaker_opened = true;
            break;
        case 1U:
        case 7U:
        case 100U:
        case 101U:
            *permanent = true;
            permanent_time[event->zone][event->feeder - 1U] = time;
            sequence->permanent = true;
            type = RF_FAULT_PERMANENT;
            break;
        case 3U:
            if (sequence->breaker_opened && !sequence->permanent && !*permanent)
            {
                type = RF_FAULT_TEMPORARY;
            }
            sequence->breaker_opened = false;
            sequence->permanent = false;
            break;
        default:
            break;
    }
    return type;
}

rf_fault_class_t rf_faults_classify(const rf_scp_event_t *event)
{
    if ((NULL == event) || (RF_ZONES <= event->zone))
    {
        return RF_FAULT_NONE;
    }
    if ((117U == event->event) && (0U == event->feeder))
    {
        increment(&stats[event->zone][0].permanent);
        return RF_FAULT_PERMANENT;
    }
    if ((1U > event->feeder) || (RF_FEEDERS < event->feeder) ||
        (1U > event->phase) || (3U < event->phase) ||
        !is_sequence_event(event->event))
    {
        return RF_FAULT_NONE;
    }
    fault_sequence_t *sequence =
        &sequences[event->feeder - 1U][event->phase - 1U];
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    rf_inventory_entry_t binding;
    if (rf_inventory_get_binding(source, &binding))
    {
        if (0 != memcmp(sequence->eui64, binding.eui64, 8U))
        {
            *sequence = (fault_sequence_t){0};
            (void)memcpy(sequence->eui64, binding.eui64, 8U);
        }
    }
    if (sequence->has_event &&
        ((sequence->zone != event->zone) ||
         (sequence->boot_counter != event->boot_counter)))
    {
        sequence->has_event = false;
        sequence->breaker_opened = false;
        sequence->permanent = false;
    }
    if (sequence->has_event && (sequence->uptime_sec > event->uptime_sec))
    {
        /* An older record cannot complete the current sequence. */
        increment(&stats[event->zone][event->feeder].uncertain);
        return RF_FAULT_NONE;
    }
    sequence->has_event = true;
    sequence->zone = event->zone;
    sequence->boot_counter = event->boot_counter;
    sequence->uptime_sec = event->uptime_sec;
    rf_fault_class_t type = classify_sequence(event, sequence);
    if (RF_FAULT_NONE != type)
    {
        const bool opened = count_opening(event, type);
        if (opened && ((100U == event->event) || (101U == event->event)))
        {
            type = RF_FAULT_NONE;
        }
    }
    return type;
}

bool rf_faults_get_stats(uint8_t zone, uint8_t feeder, rf_fault_stats_t *out)
{
    if ((NULL == out) || (RF_ZONES <= zone) || (RF_FEEDERS < feeder))
    {
        return false;
    }
    *out = stats[zone][feeder];
    return true;
}

/*** end of file ***/
