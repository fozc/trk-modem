/*
 * rf_events.c
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Read one MH batch, persist its records, then advance the MH tail.
 */

#define CSLOG_MODULE LOG_MOD_RF
#include "rf_events.h"
#include "rf_comm.h"
#include "rf_inventory.h"
#include "rf_config.h"
#include "rf_event_log.h"
#include "rf.h"
#include "rf_faults.h"
#include "rf_alarm.h"
#include "elog.h"
#include "fault_log.h"
/* HAL types must precede the legacy IEC104 Init macro. */
#include "stm32u3xx_hal.h"
#include "iec104.h"
#include "iec104_replay.h"
#include "iec104_event_log.h"
#include "scp_endian.h"
#include "utils.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define EVENT_POLL_MS 60000U
#define MH_EVENT_SLOTS 100U
#define MH_EVENT_WRAP_MODULUS 6553600U

_Static_assert(MAX_POWER_LINE_COUNT <= 8U,
               "Fault list feeder field has only three bits");

typedef enum
{
    EVENTS_IDLE,
    EVENTS_HEAD,
    EVENTS_READ,
    EVENTS_STORE,
    EVENTS_VERIFY,
    EVENTS_BAD_SLOT,
    EVENTS_CONSUME,
    EVENTS_FRAM,
    EVENTS_DEGRADED
} event_state_t;

/* Cooperative process ownership, including flash writes; no ISR access. */
static event_state_t state;
static bool requested;
static uint32_t poll_ms;
static uint32_t store_retry_ms;
static bool store_waiting;
static uint16_t cursor;
static uint16_t read_count;
static uint16_t read_tail;
static uint16_t read_pending;
static uint32_t read_total;
static uint32_t consume_seq;
static uint8_t batch[RF_SCP_EVENT_SIZE * RF_SCP_BATCH_MAX];
static uint8_t batch_count;
static uint8_t saved_count;
static bool raw_saved;
static bool fault_added;
static bool replay_added;
static fault_log_t fault_record;
static bool fault_classified;
static bool has_fault_record;
static bool stop_after_consume;
/* BOLATeX BQ-03: bring-up protection mode - read the head, consume the
 * current tail, then hand over to the inventory upload. */
static bool protect_mode;
static bool boot_protected;
/* BOLATeX BQ-05 store-continuity baseline from the previous 0x40 reply. */
static bool have_head_baseline;
static uint32_t prev_total;
static uint32_t prev_position;   /* Difference modulo 6553600. */
static bool drain_requested;
static bool drain_ready;
static uint32_t drain_total;
static uint64_t head_version;
static bool legacy_consume;

static uint16_t next_slot(uint16_t slot, uint16_t count)
{
    return (uint16_t)((slot + count) % MH_EVENT_SLOTS);
}

/* BOLATeX BQ-18: a smaller total or a changed modular position difference
 * means the MH event store was cleared or reset. Pulling already
 * continues from the current tail; only report the transition once. */
static bool detect_store_reset(const rf_scp_message_t *message,
                               uint16_t head)
{
    const uint32_t position =
        ((message->body.log_head.total % MH_EVENT_WRAP_MODULUS) +
         MH_EVENT_WRAP_MODULUS -
         (((uint32_t)message->body.log_head.wrap * MH_EVENT_SLOTS) + head))
        % MH_EVENT_WRAP_MODULUS;

    const bool reset = have_head_baseline &&
        ((message->body.log_head.total < prev_total) ||
         (position != prev_position));

    if (reset)
    {
        rf_faults_reset_sequences();
        CSLOG_WARN("[RF] MH event store reset detected "
                   "(total %lu -> %u); resuming from current tail\r\n",
                   (unsigned long)prev_total,
                   (unsigned)message->body.log_head.total);
    }
    prev_total = message->body.log_head.total;
    prev_position = position;
    have_head_baseline = true;
    return reset;
}

void rf_events_init(void)
{
    state = EVENTS_IDLE;
    requested = true;
    store_waiting = false;
    batch_count = 0U;
    saved_count = 0U;
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    fault_classified = false;
    has_fault_record = false;
    stop_after_consume = false;
    protect_mode = false;
    boot_protected = false;
    have_head_baseline = false;
    prev_total = 0U;
    prev_position = 0U;
    drain_requested = false;
    drain_ready = false;
    head_version = 0U;
    legacy_consume = false;
}

void rf_events_request_drain(void)
{
    drain_requested = true;
    drain_ready = false;
    requested = true;
}

bool rf_events_drain_complete(uint32_t *total)
{
    if ((NULL == total) || !drain_requested || !drain_ready)
    {
        return false;
    }
    *total = drain_total;
    return true;
}

const char *rf_events_drain_reason(void)
{
    if (EVENTS_STORE == state)
    {
        return "storage";
    }
    if (EVENTS_DEGRADED == state)
    {
        return "mh_degraded";
    }
    return "mh_records";
}

void rf_events_finish_drain(void)
{
    drain_requested = false;
    drain_ready = false;
}

void rf_events_notify(const rf_scp_message_t *message)
{
    if ((NULL != message) && (RF_SCP_CMD_LOG_AVAILABLE == message->cmd) &&
        (SCP_TYPE_SET == message->type) &&
        (MH_EVENT_SLOTS > message->body.log_available.pending) &&
        (MH_EVENT_SLOTS > message->body.log_available.head))
    {
        requested = true;
        drain_ready = false;
    }
}

static void stop_cycle(void)
{
    state = EVENTS_IDLE;
    poll_ms = HAL_GetTick();
    requested = false;
    /* BOLATeX BQ-03: protection failure retries via the normal poll;
     * inventory starts only from a completed protect (command_done). */
}

bool rf_events_boot_protect(void)
{
    if (EVENTS_IDLE != state)
    {
        return false;
    }
    protect_mode = true;
    requested = true;
    state = EVENTS_HEAD;
    CSLOG_WARN("[RF-LOG] BOOT kayit korumasi basliyor\r\n");
    return true;
}

bool rf_events_inventory_allowed(void)
{
    /* SCP 1.1 explicitly permits inventory when FRAM protection is unavailable. */
    return boot_protected || (state == EVENTS_DEGRADED);
}

bool rf_events_storage_degraded(void)
{
    return state == EVENTS_DEGRADED;
}

static void head_received(const rf_scp_message_t *message)
{
    const uint16_t head = message->body.log_head.head;
    const uint16_t tail = message->body.log_head.tail;

    if ((MH_EVENT_SLOTS <= head) || (MH_EVENT_SLOTS <= tail))
    {
        stop_cycle();
        return;
    }
    /* BOLATeX BQ-02: the ring keeps at most MH_EVENT_SLOTS - 1 unconsumed
     * records, so head == tail is always the empty ring. The former
     * pending == 100 recovery branch is removed per the answer. */
    uint16_t pending = (uint16_t)((head + MH_EVENT_SLOTS - tail) %
                                  MH_EVENT_SLOTS);

    (void)detect_store_reset(message, head);
    CSLOG_WARN("[RF-LOG] HEAD: head=%u tail=%u pending=%u total=%lu\r\n",
               (unsigned)head, (unsigned)tail, (unsigned)pending,
               (unsigned long)message->body.log_head.total);
    if (protect_mode)
    {
        cursor = tail;
        consume_seq = message->body.log_head.total - pending;
        state = EVENTS_CONSUME;
        return;
    }
    if (0U == pending)
    {
        rf_faults_ring_empty(head_version, HAL_GetTick());
        rf_inventory_epoch_ring_empty();
        if (drain_requested)
        {
            drain_total = message->body.log_head.total;
            drain_ready = true;
        }
        stop_cycle();
        CSLOG_WARN("[RF-LOG] kayit cekimi tamamlandi; MH kuyrugu bos\r\n");
        return;
    }
    drain_ready = false;
    cursor = tail;
    read_tail = tail;
    read_pending = pending;
    read_total = message->body.log_head.total;
    read_count = (RF_SCP_BATCH_MAX < pending) ? RF_SCP_BATCH_MAX : pending;
    state = EVENTS_READ;
}

static void range_received(const rf_scp_message_t *message)
{
    if ((0U == message->body.log_batch.count) ||
        (read_count < message->body.log_batch.count))
    {
        stop_cycle();
        return;
    }
    batch_count = message->body.log_batch.count;
    CSLOG_WARN("[RF-LOG] READ: %u kayit alindi, kalici saklama basliyor\r\n",
               (unsigned)batch_count);
    (void)memcpy(batch, message->body.log_batch.records,
                 (size_t)batch_count * RF_SCP_EVENT_SIZE);
    saved_count = 0U;
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    store_waiting = false;
    stop_after_consume = false;
    state = EVENTS_STORE;
    fault_classified = false;
    has_fault_record = false;
}

static void verify_head(const rf_scp_message_t *message)
{
    const uint16_t head = message->body.log_head.head;
    const uint16_t tail = message->body.log_head.tail;
    if ((MH_EVENT_SLOTS <= head) || (MH_EVENT_SLOTS <= tail))
    {
        stop_cycle();
        return;
    }
    if (detect_store_reset(message, head))
    {
        head_received(message);
        return;
    }
    const uint16_t pending = (uint16_t)((head + MH_EVENT_SLOTS - tail) %
                                       MH_EVENT_SLOTS);
    const uint32_t target = read_total - read_pending + saved_count;
    consume_seq = target;
    const uint32_t current_tail = message->body.log_head.total - pending;
    const uint32_t distance = target - current_tail;

    if ((0U < distance) && (pending >= distance) &&
        (cursor == next_slot(tail, (uint16_t)distance)))
    {
        CSLOG_WARN("[RF-LOG] VERIFY: %u kayit icin CONSUME hazir\r\n",
                   (unsigned)saved_count);
        state = EVENTS_CONSUME;
    }
    else
    {
        if ((0U != distance) && (pending < distance))
        {
            rf_faults_reset_sequences();
        }
        head_received(message);
    }
}

static void command_done(scp_cmd_result_t result,
                          const scp_packet_t *packet)
{
    rf_scp_message_t message;

    if ((SCP_CMD_OK != result) || (NULL == packet) ||
        (RF_CMD_OK != rf_scp_decode_message(packet, &message)))
    {
        CSLOG_WARN("[RF-LOG] komut tamamlanmadi: state=%u result=%u\r\n",
                   (unsigned)state, (unsigned)result);
        if ((EVENTS_CONSUME == state) && !legacy_consume &&
            (SCP_CMD_ERR == result) && (NULL != packet) &&
            (1U <= packet->data_len) && (RF_SCP_ERR_UNKNOWN_CMD == packet->data[0]))
        {
            legacy_consume = true;
            state = protect_mode ? EVENTS_HEAD : EVENTS_VERIFY;
        }
        else if ((SCP_CMD_ERR == result) && (NULL != packet) &&
            (1U <= packet->data_len) &&
            (RF_SCP_ERR_BUSY == packet->data[0]) &&
            (EVENTS_FRAM != state))
        {
            state = EVENTS_FRAM;
        }
        else if ((EVENTS_READ == state) && (SCP_CMD_ERR == result) &&
                 (NULL != packet) && (1U <= packet->data_len) &&
                 (RF_SCP_ERR_RECORD_INVALID == packet->data[0]))
        {
            state = EVENTS_BAD_SLOT;
        }
        else if ((EVENTS_CONSUME == state) &&
                 (SCP_CMD_ERR == result) && (NULL != packet) &&
                 (1U <= packet->data_len) &&
                 (RF_SCP_ERR_INVALID_PARAM == packet->data[0]))
        {
            state = EVENTS_HEAD;
        }
        else
        {
            stop_cycle();
        }
        return;
    }
    switch (state)
    {
        case EVENTS_HEAD:
            head_received(&message);
            break;
        case EVENTS_READ:
            range_received(&message);
            break;
        case EVENTS_BAD_SLOT:
            rf_faults_reset_sequences();
            if (cursor == message.body.log_head.head)
            {
                stop_cycle();
            }
            else
            {
                saved_count = 1U;
                cursor = next_slot(read_tail, 1U);
                verify_head(&message);
            }
            break;
        case EVENTS_VERIFY:
            verify_head(&message);
            break;
        case EVENTS_CONSUME:
            CSLOG_WARN("[RF-LOG] CONSUME ACK: tail=%u left=%u seq=%lu\r\n",
                       (unsigned)message.body.log_consume.tail,
                       (unsigned)message.body.log_consume.left,
                       (unsigned long)message.body.log_consume.tail_seq);
            if (!legacy_consume && (consume_seq != message.body.log_consume.tail_seq))
            {
                state = EVENTS_HEAD;
                break;
            }
            if (protect_mode && (cursor == message.body.log_consume.tail) &&
                (MH_EVENT_SLOTS > message.body.log_consume.left))
            {
                protect_mode = false;
                boot_protected = true;
                CSLOG_WARN("[RF-LOG] BOOT korumasi tamamlandi\r\n");
                stop_cycle();
                rf_inventory_start();
            }
            else if ((cursor != message.body.log_consume.tail) ||
                (MH_EVENT_SLOTS <= message.body.log_consume.left) ||
                stop_after_consume ||
                ((0U == message.body.log_consume.left) && !drain_requested))
            {
                stop_cycle();
            }
            else
            {
                state = EVENTS_HEAD;
            }
            break;
        case EVENTS_FRAM:
            stop_cycle();
            if (message.body.fram.degraded)
            {
                state = EVENTS_DEGRADED;
                elog_log_rf_maintenance(ELOG_RF_MAINTENANCE_FRAM, 0U, 0U, 1U, 0U);
                CSLOG_ERR("[RF] degraded MH event storage; stopped\r\n");
                if (protect_mode)
                {
                    protect_mode = false;
                    (void)rf_inventory_start();
                }
            }
            break;
        default:
            stop_cycle();
            break;
    }
}

static bool find_line(const rf_scp_event_t *event, size_t *line)
{
    const uint8_t source = (uint8_t)((event->feeder << 2U) | event->phase);
    rf_inventory_entry_t binding;
    if (rf_inventory_get_binding(source, &binding) &&
        (binding.zone == event->zone) &&
        (MAX_POWER_LINE_COUNT > binding.line_index))
    {
        *line = binding.line_index;
        return true;
    }
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; index++)
    {
        const rf_feeder_t *feeder = rf_store_get((feeder_id_t)index);
        if ((NULL != feeder) && feeder->in_use &&
            (event->zone == feeder->config.zone_id) &&
            (event->feeder == feeder->config.fider_id))
        {
            *line = index;
            return true;
        }
    }
    return false;
}

static bool build_fault(const rf_scp_event_t *event, rf_fault_class_t type,
                         fault_log_t *record)
{
    if ((RF_FAULT_NONE == type) || (1U > event->feeder) ||
        (4U < event->feeder) || (1U > event->phase) ||
        (3U < event->phase) || !isfinite(event->current_amps) ||
        (0.0F > event->current_amps) ||
        ((FLT_MAX / 10.0F) < event->current_amps) ||
        (1U < event->nominal_current_status) || (1U < event->energy_status))
    {
        return false;
    }
    size_t index;
    if (find_line(event, &index))
    {
        (void)memset(record, 0, sizeof(*record));
        (void)memcpy(&record->tm, event->timestamp, 7U);
        if (0U == event->clock_quality)
        {
            record->tm = cp56time2a_now();
        }
        record->tm.iv_bit = (1U != event->clock_quality) ||
                            !cp56time2a_is_valid(&record->tm);
        fault_log_set_current_amps(record, event->current_amps);
        record->fault_duration_ms = event->duration_ms;
        record->info.feeder = (uint8_t)(index & 0x07U);
        record->info.phase = (uint8_t)((event->phase - 1U) & 0x03U);
        /* R1 event byte 13 and LIVE bit 1 both mean load present. */
        record->info.nominal_current_status =
            (0U != event->nominal_current_status);
        record->info.power_status = (0U != event->energy_status);
        record->info.type = (RF_FAULT_PERMANENT == type);
        return true;
    }
    return false;
}

static bool save_fault(void)
{
    if (!fault_added)
    {
        fault_added = fault_log_append(&fault_record);
    }
    if (!fault_added || (0 != fault_log_sync()))
    {
        return false;
    }
    if (!replay_added)
    {
        uint16_t seq;

        replay_added = iec104_event_log_add(&fault_record, &seq);
        if (!replay_added)
        {
            return false;
        }
        if (iec104_is_link_active())
        {
            const iec104_event_record_t event =
                {.kind = IEC104_EVENT_FAULT, .payload.fault = fault_record};
            (void)iec104_replay_send_record(&event, seq);
        }
    }
    return 0 == iec104_event_log_sync();
}

#ifndef NO_CONSOLE_LOG
static const char *event_name(uint8_t event)
{
    static const struct
    {
        uint8_t code;
        const char *name;
    } names[] =
    {
        {0U, "Line did not return"}, {1U, "Permanent fault: opened"},
        {2U, "Load current returned"}, {3U, "Fault sequence step/end"},
        {4U, "Current threshold fault"}, {5U, "Current rate fault"},
        {6U, "Upstream breaker opened"}, {7U, "Permanent fault: RF opening"},
        {90U, "AY boot"}, {91U, "Legacy settings saved"}, {92U, "Line break"},
        {94U, "Manual service opening"}, {95U, "Measurement watchdog"},
        {97U, "Inrush detected"}, {100U, "Opening cancelled: line energized"},
        {101U, "Opening failed: capacitor voltage"},
        {102U, "Legacy startup flag cleared; not an active lock"},
        {103U, "Protection unexpected state; contact BOLATeX"},
        {104U, "Dead-line memory expired"}, {105U, "Opening failed: current returned"},
        {106U, "RF opening rejected: stale measurement"},
        {115U, "RF opening rejected: phase not ready"}, {116U, "MH binding cleared in service"},
        {117U, "Opening suppressed: feeder unassigned"}, {118U, "Three-phase mode without feeder"},
        {119U, "Assignment from a different MH rejected"}, {120U, "Discovery assignment"},
        {121U, "Staged configuration expired"}, {122U, "Configuration applied on AY"},
        {134U, "AY RF stalled: restart"}, {135U, "MH RF scheduler stalled: restart"},
        {136U, "Inventory phase silent"}, {137U, "TEST; not a protection event"},
        {138U, "MH event store reset/clear"}, {139U, "AY configuration/counters defaulted"},
        {140U, "Production lock rejected service command; not an attempt counter"},
        {141U, "Delayed RF opening discarded"}, {142U, "R2 temporary fault completed"},
        {143U, "RF security counter recovery/lock"}, {144U, "MH authentication rejected; EPOCH required"},
        {201U, "EPOCH campaign completed; not configuration APPLIED"}
    };
    for (size_t index = 0U; index < ARRAY_SIZE(names); index++)
    {
        if (names[index].code == event)
        {
            return names[index].name;
        }
    }
    if ((event >= 110U) && (event <= 114U))
    {
        return "RF common-trip diagnostic";
    }
    if ((event >= 123U) && (event <= 133U))
    {
        return "Reserved OTA event";
    }
    return "Unknown event";
}

static const char *event_detail(const rf_scp_event_t *event)
{
    static const char *const defaults[] = {"Unknown default reason", "Invalid configuration field",
        "Invalid nominal/threshold pair", "Configuration CRC failed; feeder unassigned",
        "Configuration read failed; feeder unassigned", "Counter CRC failed", "Counter data inconsistent",
        "Counter read failed", "Configuration empty; feeder unassigned", "Counter block empty"};
    if ((event->event == 139U) && (event->fault_count < ARRAY_SIZE(defaults)))
    {
        return defaults[event->fault_count];
    }
    if (event->event == 140U)
    {
        switch (event->permanent_faults)
        {
            case 3U: return "set";
            case 4U: return "default";
            case 7U: return "key set";
            case 13U: return "log clear";
            default: return "Other service command";
        }
    }
    return "See raw record and SCP event table";
}
#endif

static void store_record(uint32_t now_ms)
{
    rf_scp_event_t event;
    const uint8_t *raw = &batch[(size_t)saved_count * RF_SCP_EVENT_SIZE];

    if (RF_CMD_OK != rf_scp_decode_event(raw, RF_SCP_EVENT_SIZE, &event))
    {
        CSLOG_WARN("[RF] invalid event CRC/body; tail held\r\n");
        if (0U < saved_count)
        {
            cursor = next_slot(cursor, saved_count);
            stop_after_consume = true;
            state = EVENTS_VERIFY;
        }
        else
        {
            stop_cycle();
        }
        return;
    }
    if (!raw_saved)
    {
        raw_saved = rf_event_log_append(raw, RF_SCP_EVENT_SIZE);
        if (!raw_saved)
        {
            CSLOG_WARN("[RF-LOG] STORE bekliyor: ham kayit saklanamadi\r\n");
            store_waiting = true;
            store_retry_ms = now_ms;
            return;
        }
    }
    if (!fault_classified)
    {
        const bool matches = rf_event_source_matches(&event);
        const bool source_verified = matches && (event.src_eui_hash != 0U);
        rf_record_service_event(&event, matches);
        rf_inventory_epoch_event(&event, source_verified, now_ms);
        if ((138U == event.event) && (0U == event.src_eui_hash))
        {
            rf_faults_reset_sequences();
        }
        else if (source_verified && (144U == event.event))
        {
            rf_inventory_note_auth_failure(event.feeder);
        }
        else
        {
            /* Other service records do not invalidate fault evidence. */
        }
        const rf_fault_class_t type = matches ? rf_faults_classify(&event) :
                                                RF_FAULT_NONE;
        has_fault_record = build_fault(&event, type, &fault_record);
        if (!matches && (0U != event.feeder) && (0U != event.phase))
        {
            CSLOG_WARN("[RF] event source unverified: feeder=%u phase=%u "
                       "hash=0x%04X; retained raw only\r\n",
                       (unsigned)event.feeder, (unsigned)event.phase,
                       (unsigned)event.src_eui_hash);
        }
        fault_classified = true;
    }
    if (has_fault_record)
    {
        if (!save_fault())
        {
            CSLOG_WARN("[RF-LOG] STORE bekliyor: ariza/replay saklanamadi\r\n");
            store_waiting = true;
            store_retry_ms = now_ms;
            return;
        }
        rf_faults_record_stored(&event);
    }
    if (!rf_alarm_record(&event))
    {
        CSLOG_WARN("[RF-LOG] STORE bekliyor: alarm saklanamadi\r\n");
        store_waiting = true;
        store_retry_ms = now_ms;
        return;
    }
    saved_count++;
    CSLOG_WARN("[RF-LOG] STORE: %u/%u kayit saklandi "
               "event=%u feeder=%u phase=%u name=%s sub=%u aux=%u detail=%s\r\n",
               (unsigned)saved_count, (unsigned)batch_count,
               (unsigned)event.event, (unsigned)event.feeder,
               (unsigned)event.phase, event_name(event.event), (unsigned)event.fault_count,
               (unsigned)event.permanent_faults, event_detail(&event));
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    fault_classified = false;
    has_fault_record = false;
    store_waiting = false;
    if (saved_count == batch_count)
    {
        cursor = next_slot(cursor, saved_count);
        /* BOLATeX BQ-03: re-read the head before EVERY consume and apply
         * the sequence-number guard, not only after a store stall. */
        state = EVENTS_VERIFY;
    }
}

/* The packet local must not inflate the STORE/Flash branch of process. */
static __attribute__((noinline)) void send_command(void)
{
    scp_packet_t request = {.type = SCP_TYPE_GET};

    switch (state)
    {
        case EVENTS_HEAD:
        case EVENTS_VERIFY:
        case EVENTS_BAD_SLOT:
            request.cmd = RF_SCP_CMD_LOG_READ_HEAD;
            head_version = rf_faults_head_started();
            rf_inventory_epoch_head_started();
            break;
        case EVENTS_READ:
            request.cmd = RF_SCP_CMD_LOG_READ_RANGE;
            request.data_len = 4U;
            scp_pack_u16(request.data, cursor);
            scp_pack_u16(&request.data[2], read_count);
            break;
        case EVENTS_CONSUME:
            request.type = SCP_TYPE_SET;
            if (legacy_consume)
            {
                request.cmd = RF_SCP_CMD_LOG_CONSUME_TO;
                request.data_len = 2U;
                scp_pack_u16(request.data, cursor);
            }
            else
            {
                request.cmd = RF_SCP_CMD_LOG_CONSUME_IF;
                request.data_len = 4U;
                for (size_t index = 0U; index < 4U; index++)
                {
                    request.data[index] = (uint8_t)(consume_seq >> (8U * index));
                }
            }
            break;
        case EVENTS_FRAM:
            request.cmd = RF_SCP_CMD_GET_FRAM_STATS;
            break;
        default:
            return;
    }
    (void)scp_send_request(&request, command_done);
}

void rf_events_process(uint32_t now_ms)
{
    const rf_inventory_status_t inventory = rf_inventory_get_status();

    if (!rf_comm_can_load_inventory() ||
        (!protect_mode && (RF_INVENTORY_READY != inventory) &&
         (RF_INVENTORY_PARTIAL != inventory) &&
         (RF_INVENTORY_EMPTY != inventory) &&
         (RF_INVENTORY_DRAINING != inventory)) || !scp_is_free() ||
        (EVENTS_DEGRADED == state))
    {
        return;
    }
    if (EVENTS_IDLE == state)
    {
        if (requested || rf_faults_needs_head(now_ms) ||
            (EVENT_POLL_MS <= (uint32_t)(now_ms - poll_ms)))
        {
            requested = false;
            poll_ms = now_ms;
            state = EVENTS_HEAD;
        }
    }
    if (EVENTS_STORE == state)
    {
        if (!store_waiting ||
            (EVENT_POLL_MS <= (uint32_t)(now_ms - store_retry_ms)))
        {
            store_record(now_ms);
        }
        return;
    }
    send_command();
}

/*** end of file ***/
