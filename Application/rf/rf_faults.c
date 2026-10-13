/*
 * rf_faults.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Keep phase outcomes and BQ-21/22 feeder sequence evidence separate.
 */
#include "rf_faults.h"
#include "cp56time2a.h"
#include "rf_inventory.h"
#include "rf_config.h"
#include <string.h>

#define RF_ZONES 8U
#define RF_FEEDERS 4U
#define RF_GROUPS 128U
#define RF_PHASES 3U
#define RF_LIVE_FRESH_MS 30000U
#define NO_GROUP RF_GROUPS

typedef enum
{
    CLOSURE_IDLE,
    CLOSURE_PENDING,
    CLOSURE_READY
} closure_state_t;

typedef struct
{
    uint8_t eui64[8];
    uint16_t boot;
    uint16_t crc;
    uint8_t event;
    uint32_t event_uptime;
    uint32_t opening_uptime;
    uint32_t live_uptime;
    uint32_t live_seq;
    uint32_t live_ms;
    uint64_t ready_version;
    closure_state_t closure;
    bool present;
    bool has_record;
    bool phase_saved;
} fault_participant_t;

typedef struct
{
    cp56time2a_t start;
    cp56time2a_t end;
    cp56time2a_t opening_time;
    cp56time2a_t opening_end;
    fault_participant_t phases[RF_PHASES];
    uint8_t zone;
    uint8_t feeder;
    uint8_t opening_count;
    bool used;
    bool finalized;
    bool uncertain;
    bool opened;
    bool failed;
    bool temporary;
    bool opening_ambiguous;
} fault_group_t;

typedef struct
{
    cp56time2a_t start;
    uint32_t uptime;
    uint32_t opening_uptime;
    uint16_t boot;
    uint8_t zone;
    uint8_t quality;
    uint8_t eui64[8];
    size_t group_index;
    bool has_event;
    bool active;
    bool has_opening;
} fault_sequence_t;

/* All accesses belong to cooperative process/shell context, never an ISR. */
static fault_sequence_t sequences[RF_FEEDERS][RF_PHASES];
static fault_group_t groups[RF_GROUPS];
static rf_fault_stats_t archived[RF_ZONES][RF_FEEDERS + 1U];
static uint64_t evidence_version;
/* Even an idle phase may still be forwarding its delayed result records. */
static bool phase_has_pending[RF_FEEDERS][RF_PHASES];
static uint64_t phase_cleared_version[RF_FEEDERS][RF_PHASES];

static void increment(uint32_t *value)
{
    if (UINT32_MAX > *value)
    {
        (*value)++;
    }
}

static void reset_sequence(fault_sequence_t *sequence)
{
    *sequence = (fault_sequence_t){.group_index = NO_GROUP};
}

void rf_faults_reset_sequences(void)
{
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].used && !groups[index].finalized)
        {
            groups[index].uncertain = true;
        }
    }
    for (size_t feeder = 0U; RF_FEEDERS > feeder; feeder++)
    {
        for (size_t phase = 0U; RF_PHASES > phase; phase++)
        {
            reset_sequence(&sequences[feeder][phase]);
        }
    }
    evidence_version++;
}

void rf_faults_init(void)
{
    (void)memset(groups, 0, sizeof(groups));
    (void)memset(archived, 0, sizeof(archived));
    (void)memset(phase_has_pending, 0, sizeof(phase_has_pending));
    (void)memset(phase_cleared_version, 0, sizeof(phase_cleared_version));
    evidence_version = 0U;
    rf_faults_reset_sequences();
}

void rf_faults_reset_phase(uint8_t feeder, uint8_t phase)
{
    if ((1U > feeder) || (RF_FEEDERS < feeder) ||
        (1U > phase) || (RF_PHASES < phase))
    {
        return;
    }
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        fault_group_t *group = &groups[index];
        if (group->used && !group->finalized && (feeder == group->feeder) &&
            group->phases[phase - 1U].present)
        {
            group->uncertain = true;
            group->phases[phase - 1U].closure = CLOSURE_IDLE;
        }
    }
    reset_sequence(&sequences[feeder - 1U][phase - 1U]);
    evidence_version++;
}

static rf_fault_class_t group_type(const fault_group_t *group)
{
    if (group->opened || group->failed)
    {
        return RF_FAULT_PERMANENT;
    }
    return group->temporary ? RF_FAULT_TEMPORARY : RF_FAULT_NONE;
}

static void add_group_count(const fault_group_t *group, rf_fault_stats_t *out)
{
    if (group->uncertain || group->opening_ambiguous)
    {
        increment(&out->uncertain);
    }
    else if (RF_FAULT_PERMANENT == group_type(group))
    {
        increment(&out->permanent);
    }
    else if (RF_FAULT_TEMPORARY == group_type(group))
    {
        increment(&out->temporary);
    }
    else
    {
        /* No feeder fault outcome. */
    }
    if (!group->finalized)
    {
        increment(&out->pending);
    }
}

static size_t reserve_group(void)
{
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (!groups[index].used)
        {
            return index;
        }
    }
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].finalized)
        {
            fault_group_t *group = &groups[index];
            add_group_count(group, &archived[group->zone][group->feeder]);
            group->used = false;
            return index;
        }
    }
    return NO_GROUP; /* Never overwrite an unresolved sequence. */
}

static bool overlaps(const fault_group_t *a, const fault_group_t *b)
{
    return (0 <= cp56time2a_diff_ms(&a->end, &b->start)) &&
           (0 <= cp56time2a_diff_ms(&b->end, &a->start));
}

static bool groups_match(const fault_group_t *a, const fault_group_t *b)
{
    if ((a->zone != b->zone) || (a->feeder != b->feeder) ||
        a->uncertain || b->uncertain)
    {
        return false;
    }
    if (a->temporary || b->temporary)
    {
        return overlaps(a, b);
    }
    if (a->opened || b->opened)
    {
        const cp56time2a_t *ta = a->opened ? &a->opening_time : &a->end;
        const cp56time2a_t *tb = b->opened ? &b->opening_time : &b->end;
        const cp56time2a_t *ea = a->opened ? &a->opening_end : ta;
        const cp56time2a_t *eb = b->opened ? &b->opening_end : tb;
        /* Keep a connected opening window independent of arrival order.
         * A chain spanning more than one second is not an exact count. */
        return (1000 >= cp56time2a_diff_ms(ta, eb)) &&
               (1000 >= cp56time2a_diff_ms(tb, ea));
    }
    return overlaps(a, b);
}

static void merge_group(fault_group_t *target, const fault_group_t *other)
{
    if (0 > cp56time2a_diff_ms(&other->start, &target->start))
    {
        target->start = other->start;
    }
    if (0 < cp56time2a_diff_ms(&other->end, &target->end))
    {
        target->end = other->end;
    }
    if (other->opened && (!target->opened ||
        (0 > cp56time2a_diff_ms(&other->opening_time, &target->opening_time))))
    {
        target->opening_time = other->opening_time;
    }
    if (other->opened && (!target->opened ||
        (0 < cp56time2a_diff_ms(&other->opening_end, &target->opening_end))))
    {
        target->opening_end = other->opening_end;
    }
    target->opened = target->opened || other->opened;
    target->opening_ambiguous = target->opening_ambiguous ||
        other->opening_ambiguous || (target->opened &&
        (1000 < cp56time2a_diff_ms(&target->opening_end,
                                  &target->opening_time)));
    target->failed = target->failed || other->failed;
    target->temporary = target->temporary || other->temporary;
    target->uncertain = target->uncertain || other->uncertain;
    if (target->opening_count < other->opening_count)
    {
        target->opening_count = other->opening_count;
    }
    for (size_t phase = 0U; RF_PHASES > phase; phase++)
    {
        const fault_participant_t *next = &other->phases[phase];
        fault_participant_t *current = &target->phases[phase];
        if (next->present)
        {
            if (current->present && ((current->boot != next->boot) ||
                (0 != memcmp(current->eui64, next->eui64, 8U))))
            {
                target->uncertain = true;
            }
            if (!current->present || (!current->has_record && next->has_record) ||
                ((current->has_record == next->has_record) &&
                 (current->event_uptime <= next->event_uptime)))
            {
                *current = *next;
            }
        }
    }
    /* A new record invalidates every previous closure barrier. */
    target->finalized = false;
    for (size_t phase = 0U; RF_PHASES > phase; phase++)
    {
        target->phases[phase].closure = CLOSURE_IDLE;
    }
}

static void redirect_sequences(size_t from, size_t to)
{
    for (size_t feeder = 0U; RF_FEEDERS > feeder; feeder++)
    {
        for (size_t phase = 0U; RF_PHASES > phase; phase++)
        {
            if (from == sequences[feeder][phase].group_index)
            {
                sequences[feeder][phase].group_index = to;
            }
        }
    }
}

static size_t collect_group(fault_group_t *candidate, size_t own)
{
    size_t target = own;
    bool merged;
    do
    {
        merged = false;
        for (size_t index = 0U; RF_GROUPS > index; index++)
        {
            if (groups[index].used && (index != own) &&
                groups_match(candidate, &groups[index]))
            {
                if (NO_GROUP == target)
                {
                    target = index;
                }
                merge_group(candidate, &groups[index]);
                groups[index].used = false;
                redirect_sequences(index, target);
                merged = true;
            }
        }
        /* Each pass consumes at least one group; at most RF_GROUPS passes. */
    } while (merged);
    if (NO_GROUP == target)
    {
        target = reserve_group();
    }
    if (NO_GROUP != target)
    {
        candidate->used = true;
        groups[target] = *candidate;
    }
    else
    {
        increment(&archived[candidate->zone][candidate->feeder].uncertain);
    }
    return target;
}

static bool is_sequence_event(uint8_t event)
{
    switch (event)
    {
        case 0U: case 1U: case 3U: case 4U: case 5U: case 6U: case 7U:
        case 100U: case 101U: case 104U: case 106U: case 117U:
        case 141U: case 142U:
            return true;
        default:
            return false;
    }
}

static rf_fault_class_t classify_event(uint8_t event)
{
    switch (event)
    {
        case 1U: case 7U: case 100U: case 101U:
            return RF_FAULT_PERMANENT;
        case 142U:
            return RF_FAULT_TEMPORARY;
        default:
            return RF_FAULT_NONE;
    }
}

static bool sequence_accept(fault_sequence_t *sequence,
                            const rf_scp_event_t *event)
{
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    rf_inventory_entry_t binding;
    if (rf_inventory_get_binding(source, &binding) &&
        (0 != memcmp(sequence->eui64, binding.eui64, 8U)))
    {
        if (sequence->has_event)
        {
            rf_faults_reset_phase(event->feeder, event->phase);
        }
        else
        {
            reset_sequence(sequence);
        }
        (void)memcpy(sequence->eui64, binding.eui64, 8U);
    }
    if (sequence->has_event && ((sequence->zone != event->zone) ||
        (sequence->boot != event->boot_counter)))
    {
        const uint8_t identity[8] =
            {sequence->eui64[0], sequence->eui64[1], sequence->eui64[2],
             sequence->eui64[3], sequence->eui64[4], sequence->eui64[5],
             sequence->eui64[6], sequence->eui64[7]};
        rf_faults_reset_phase(event->feeder, event->phase);
        (void)memcpy(sequence->eui64, identity, sizeof(identity));
    }
    if (sequence->has_event && (sequence->uptime > event->uptime_sec))
    {
        increment(&archived[event->zone][event->feeder].uncertain);
        return false;
    }
    sequence->has_event = true;
    sequence->zone = event->zone;
    sequence->boot = event->boot_counter;
    sequence->uptime = event->uptime_sec;
    return true;
}

static fault_participant_t *find_record(const rf_scp_event_t *event,
                                        bool *opened)
{
    rf_inventory_entry_t binding;
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    if (!rf_inventory_get_binding(source, &binding))
    {
        return NULL;
    }
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        fault_group_t *group = &groups[index];
        fault_participant_t *phase = &group->phases[event->phase - 1U];
        if (group->used && (group->zone == event->zone) &&
            (group->feeder == event->feeder) && phase->present &&
            (0U != event->boot_counter) && (phase->event == event->event) &&
            (phase->boot == event->boot_counter) &&
            (phase->event_uptime == event->uptime_sec) &&
            (phase->crc == event->crc) &&
            (0 == memcmp(phase->eui64, binding.eui64, 8U)))
        {
            *opened = group->opened;
            return phase;
        }
    }
    return NULL;
}

void rf_faults_record_stored(const rf_scp_event_t *event)
{
    if ((NULL == event) || (1U > event->feeder) ||
        (RF_FEEDERS < event->feeder) || (1U > event->phase) ||
        (RF_PHASES < event->phase))
    {
        return;
    }
    bool opened = false;
    fault_participant_t *phase = find_record(event, &opened);
    if (NULL != phase)
    {
        phase->phase_saved = true;
    }
}

static bool record_result(fault_sequence_t *sequence,
                          const rf_scp_event_t *event,
                          const cp56time2a_t *time)
{
    size_t own = sequence->group_index;
    if ((RF_GROUPS <= own) || !groups[own].used)
    {
        own = NO_GROUP;
    }
    fault_group_t candidate = (NO_GROUP == own) ? (fault_group_t){0} :
                                                               groups[own];
    const bool opened = (1U == event->event) || (7U == event->event);
    const cp56time2a_t start = sequence->active ? sequence->start : *time;
    if ((NO_GROUP == own) ||
        (0 > cp56time2a_diff_ms(&start, &candidate.start)))
    {
        candidate.start = start;
    }
    if ((NO_GROUP == own) || (0 < cp56time2a_diff_ms(time, &candidate.end)))
    {
        candidate.end = *time;
    }
    candidate.zone = event->zone;
    candidate.feeder = event->feeder;
    candidate.failed = candidate.failed || (100U == event->event) ||
                       (101U == event->event);
    candidate.temporary = candidate.temporary || (142U == event->event);
    candidate.uncertain = candidate.uncertain ||
        (1U != event->clock_quality) || !cp56time2a_is_valid(time) ||
        (0U == event->boot_counter) ||
        (sequence->active && (1U != sequence->quality)) ||
        (sequence->active && (!cp56time2a_is_valid(&sequence->start) ||
         (0 > cp56time2a_diff_ms(time, &sequence->start)))) ||
        ((142U == event->event) && !sequence->active);
    if (opened)
    {
        if (!candidate.opened)
        {
            candidate.opening_time = *time;
            candidate.opening_end = *time;
        }
        else
        {
            if (0 > cp56time2a_diff_ms(time, &candidate.opening_time))
            {
                candidate.opening_time = *time;
            }
            if (0 < cp56time2a_diff_ms(time, &candidate.opening_end))
            {
                candidate.opening_end = *time;
            }
        }
        candidate.opened = true;
        candidate.opening_ambiguous = candidate.opening_ambiguous ||
            (1000 < cp56time2a_diff_ms(&candidate.opening_end,
                                      &candidate.opening_time));
    }
    if (candidate.opening_count < event->fault_count)
    {
        candidate.opening_count = event->fault_count;
    }
    fault_participant_t *phase = &candidate.phases[event->phase - 1U];
    *phase = (fault_participant_t){.present = true, .has_record = true,
        .boot = event->boot_counter, .crc = event->crc, .event = event->event,
        .event_uptime = event->uptime_sec,
        .opening_uptime = opened ? event->uptime_sec :
            (sequence->has_opening ? sequence->opening_uptime :
                                     event->uptime_sec)};
    (void)memcpy(phase->eui64, sequence->eui64, 8U);
    candidate.finalized = false;
    phase->closure = CLOSURE_IDLE;
    const size_t index = collect_group(&candidate, own);
    sequence->group_index = index;
    return (RF_GROUPS > index) && groups[index].opened;
}

rf_fault_class_t rf_faults_classify(const rf_scp_event_t *event)
{
    if ((NULL == event) || (RF_ZONES <= event->zone))
    {
        return RF_FAULT_NONE;
    }
    if ((117U == event->event) && (0U == event->feeder))
    {
        increment(&archived[event->zone][0].permanent);
        return RF_FAULT_PERMANENT;
    }
    if ((1U > event->feeder) || (RF_FEEDERS < event->feeder) ||
        (1U > event->phase) || (RF_PHASES < event->phase) ||
        !is_sequence_event(event->event))
    {
        return RF_FAULT_NONE;
    }
    fault_sequence_t *sequence =
        &sequences[event->feeder - 1U][event->phase - 1U];
    bool previously_opened = false;
    fault_participant_t *saved = find_record(event, &previously_opened);
    if ((RF_FAULT_NONE != classify_event(event->event)) && (NULL != saved))
    {
        if (saved->phase_saved || (previously_opened &&
            ((100U == event->event) || (101U == event->event))))
        {
            return RF_FAULT_NONE; /* Raw archive still retains the copy. */
        }
        return classify_event(event->event);
    }
    if (!sequence_accept(sequence, event))
    {
        return RF_FAULT_NONE;
    }
    cp56time2a_t time;
    (void)memcpy(&time, event->timestamp, sizeof(time));
    rf_fault_class_t type = classify_event(event->event);
    evidence_version++;
    if ((4U == event->event) || (5U == event->event) || (6U == event->event))
    {
        if (!sequence->active)
        {
            sequence->start = time;
            sequence->quality = event->clock_quality;
            sequence->active = true;
            sequence->group_index = NO_GROUP;
            sequence->has_opening = false;
        }
        if (1U != event->clock_quality)
        {
            sequence->quality = event->clock_quality;
        }
        if (6U == event->event)
        {
            sequence->has_opening = true;
            sequence->opening_uptime = event->uptime_sec;
        }
    }
    if ((RF_FAULT_NONE != type) ||
        (sequence->active && (RF_GROUPS > sequence->group_index)))
    {
        const bool opened = record_result(sequence, event, &time);
        if (opened && ((100U == event->event) || (101U == event->event)))
        {
            type = RF_FAULT_NONE;
        }
    }
    if ((0U == event->event) || (1U == event->event) ||
        (3U == event->event) || (7U == event->event) ||
        (104U == event->event) || (142U == event->event))
    {
        sequence->active = false;
        sequence->group_index = NO_GROUP;
    }
    return type;
}

static void observe_participant(fault_group_t *group, size_t index,
                                 const rf_scp_live_t *live, uint32_t now_ms)
{
    fault_participant_t *phase = &group->phases[index];
    rf_inventory_entry_t binding;
    if (!rf_inventory_get_binding(live->source, &binding) ||
        (binding.zone != group->zone) ||
        (0 != memcmp(binding.eui64, phase->eui64, 8U)) ||
        (live->boot_counter != phase->boot) || (0U == live->boot_counter))
    {
        group->uncertain = true;
        phase->closure = CLOSURE_IDLE;
        return;
    }
    const rf_feeder_t *feeder = rf_store_get((feeder_id_t)binding.line_index);
    if ((NULL == feeder) || !feeder->in_use ||
        (live->uptime_sec <= phase->event_uptime) || live->fsm_error ||
        (1U != live->state) || (0U != live->fault_count))
    {
        phase->closure = CLOSURE_IDLE;
        return;
    }
    if ((live->uptime_sec < phase->opening_uptime) ||
        ((live->uptime_sec - phase->opening_uptime) <
         feeder->config.t_mem_dead_sec))
    {
        phase->closure = CLOSURE_IDLE;
        return;
    }
    if ((CLOSURE_IDLE != phase->closure) &&
        (live->uptime_sec < phase->live_uptime))
    {
        group->uncertain = true;
        phase->closure = CLOSURE_IDLE;
        return;
    }
    if ((CLOSURE_IDLE != phase->closure) &&
        ((live->uptime_sec == phase->live_uptime) ||
         (live->seq == phase->live_seq)))
    {
        return; /* Replayed or non-advancing LIVE cannot advance the barrier. */
    }
    if (CLOSURE_IDLE == phase->closure)
    {
        phase->closure = CLOSURE_PENDING;
    }
    else if (0U == live->log_pending)
    {
        phase->closure = CLOSURE_READY;
        phase->ready_version = evidence_version;
    }
    else
    {
        phase->closure = CLOSURE_PENDING;
    }
    phase->live_uptime = live->uptime_sec;
    phase->live_seq = live->seq;
    phase->live_ms = now_ms;
}

void rf_faults_live(const rf_scp_live_t *live, uint32_t now_ms)
{
    if ((NULL == live) || (1U > (live->source & 3U)) ||
        (RF_PHASES < (live->source & 3U)))
    {
        return;
    }
    const uint8_t feeder = (uint8_t)((live->source >> 2U) & 7U);
    if ((1U > feeder) || (RF_FEEDERS < feeder))
    {
        return;
    }
    evidence_version++;
    const size_t source_phase = (size_t)(live->source & 3U) - 1U;
    if (0U != live->log_pending)
    {
        phase_has_pending[feeder - 1U][source_phase] = true;
    }
    else if (phase_has_pending[feeder - 1U][source_phase])
    {
        phase_has_pending[feeder - 1U][source_phase] = false;
        phase_cleared_version[feeder - 1U][source_phase] = evidence_version;
    }
    else
    {
        /* An unchanged zero does not create another drain barrier. */
    }
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        fault_group_t *group = &groups[index];
        const size_t phase = (size_t)(live->source & 3U) - 1U;
        if (!group->used || group->finalized || (feeder != group->feeder))
        {
            continue;
        }
        fault_participant_t *participant = &group->phases[phase];
        if (!participant->present &&
            ((1U != live->state) || (0U != live->fault_count)))
        {
            rf_inventory_entry_t binding;
            if (rf_inventory_get_binding(live->source, &binding) &&
                (binding.zone == group->zone))
            {
                *participant = (fault_participant_t){.present = true,
                    .boot = live->boot_counter,
                    .event_uptime = live->uptime_sec,
                    .opening_uptime = live->uptime_sec};
                (void)memcpy(participant->eui64, binding.eui64, 8U);
            }
        }
        if (participant->present)
        {
            observe_participant(group, phase, live, now_ms);
        }
    }
}

uint64_t rf_faults_head_started(void)
{
    return evidence_version;
}

static bool closure_ready(const fault_group_t *group, uint64_t version,
                           uint32_t now_ms)
{
    for (size_t phase = 0U; RF_PHASES > phase; phase++)
    {
        const fault_participant_t *participant = &group->phases[phase];
        if (phase_has_pending[group->feeder - 1U][phase] ||
            (phase_cleared_version[group->feeder - 1U][phase] > version))
        {
            return false;
        }
        const fault_sequence_t *sequence =
            &sequences[group->feeder - 1U][phase];
        if (sequence->active && (sequence->zone == group->zone) &&
            (NO_GROUP == sequence->group_index))
        {
            return false;
        }
        if (participant->present &&
            ((CLOSURE_READY != participant->closure) ||
             (participant->ready_version > version) ||
             (RF_LIVE_FRESH_MS <= (uint32_t)(now_ms - participant->live_ms))))
        {
            return false;
        }
    }
    return true;
}

bool rf_faults_needs_head(uint32_t now_ms)
{
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].used && !groups[index].finalized &&
            closure_ready(&groups[index], evidence_version, now_ms))
        {
            return true;
        }
    }
    return false;
}

void rf_faults_ring_empty(uint64_t head_version, uint32_t now_ms)
{
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].used && !groups[index].finalized &&
            closure_ready(&groups[index], head_version, now_ms))
        {
            groups[index].finalized = true;
            for (size_t phase = 0U; RF_PHASES > phase; phase++)
            {
                if (groups[index].phases[phase].present &&
                    !groups[index].phases[phase].has_record)
                {
                    groups[index].uncertain = true;
                }
            }
            for (size_t feeder = 0U; RF_FEEDERS > feeder; feeder++)
            {
                for (size_t phase = 0U; RF_PHASES > phase; phase++)
                {
                    fault_sequence_t *sequence = &sequences[feeder][phase];
                    if (index == sequence->group_index)
                    {
                        sequence->active = false;
                        sequence->group_index = NO_GROUP;
                    }
                }
            }
        }
    }
}

void rf_faults_hub_restarted(void)
{
    evidence_version++;
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].used && !groups[index].finalized)
        {
            for (size_t phase = 0U; RF_PHASES > phase; phase++)
            {
                groups[index].phases[phase].closure = CLOSURE_IDLE;
            }
        }
    }
}

bool rf_faults_get_stats(uint8_t zone, uint8_t feeder, rf_fault_stats_t *out)
{
    if ((NULL == out) || (RF_ZONES <= zone) || (RF_FEEDERS < feeder))
    {
        return false;
    }
    *out = archived[zone][feeder];
    for (size_t index = 0U; RF_GROUPS > index; index++)
    {
        if (groups[index].used && (zone == groups[index].zone) &&
            (feeder == groups[index].feeder))
        {
            add_group_count(&groups[index], out);
        }
    }
    if (0U != feeder)
    {
        for (size_t phase = 0U; RF_PHASES > phase; phase++)
        {
            const fault_sequence_t *sequence = &sequences[feeder - 1U][phase];
            if (sequence->active && (zone == sequence->zone) &&
                (NO_GROUP == sequence->group_index))
            {
                increment(&out->pending);
            }
        }
    }
    out->is_final = (0U == out->pending) && (0U == out->uncertain);
    return true;
}

/*** end of file ***/
