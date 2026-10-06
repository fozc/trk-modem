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
#include "fault_log.h"
/* HAL types must precede the legacy IEC104 Init macro. */
#include "stm32u3xx_hal.h"
#include "iec104.h"
#include "iec104_event_log.h"
#include "scp_endian.h"
#include "utils.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define EVENT_POLL_MS 60000U
#define MH_EVENT_SLOTS 100U

_Static_assert(MAX_POWER_LINE_COUNT <= 8U,
               "Fault list feeder field has only three bits");

typedef enum
{
    EVENTS_IDLE,
    EVENTS_HEAD,
    EVENTS_READ,
    EVENTS_STORE,
    EVENTS_VERIFY,
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
static uint16_t pending_hint;
static uint16_t hint_head;
static uint16_t cursor;
static uint16_t read_count;
static uint16_t read_tail;
static uint16_t read_pending;
static uint32_t read_total;
static bool needs_verification;
static uint8_t batch[RF_SCP_EVENT_SIZE * RF_SCP_BATCH_MAX];
static uint8_t batch_count;
static uint8_t saved_count;
static bool raw_saved;
static bool fault_added;
static bool replay_added;
static fault_log_t fault_record;
static bool stop_after_consume;

static uint16_t next_slot(uint16_t slot, uint16_t count)
{
    return (uint16_t)((slot + count) % MH_EVENT_SLOTS);
}

void rf_events_init(void)
{
    state = EVENTS_IDLE;
    requested = true;
    store_waiting = false;
    pending_hint = 0U;
    batch_count = 0U;
    saved_count = 0U;
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    stop_after_consume = false;
    needs_verification = false;
}

void rf_events_notify(const rf_scp_message_t *message)
{
    if ((NULL != message) && (RF_SCP_CMD_LOG_AVAILABLE == message->cmd) &&
        (SCP_TYPE_SET == message->type) &&
        (MH_EVENT_SLOTS >= message->body.log_available.pending) &&
        (MH_EVENT_SLOTS > message->body.log_available.head))
    {
        pending_hint = message->body.log_available.pending;
        hint_head = message->body.log_available.head;
        requested = true;
    }
}

static void stop_cycle(void)
{
    state = EVENTS_IDLE;
    poll_ms = HAL_GetTick();
    requested = false;
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
    uint16_t pending = (uint16_t)((head + MH_EVENT_SLOTS - tail) %
                                  MH_EVENT_SLOTS);

    if ((0U == pending) && (MH_EVENT_SLOTS == pending_hint) &&
        (head == hint_head))
    {
        pending = MH_EVENT_SLOTS;
    }
    pending_hint = 0U;
    if (0U == pending)
    {
        stop_cycle();
        return;
    }
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
    (void)memcpy(batch, message->body.log_batch.records,
                 (size_t)batch_count * RF_SCP_EVENT_SIZE);
    saved_count = 0U;
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    store_waiting = false;
    stop_after_consume = false;
    needs_verification = false;
    state = EVENTS_STORE;
}

static void command_done(scp_cmd_result_t result,
                          const scp_packet_t *packet)
{
    rf_scp_message_t message;

    if ((SCP_CMD_OK != result) || (NULL == packet) ||
        (RF_CMD_OK != rf_scp_decode_message(packet, &message)))
    {
        if ((SCP_CMD_ERR == result) && (NULL != packet) &&
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
            CSLOG_WARN("[RF] unreadable event slot %u skipped\r\n",
                       (unsigned)cursor);
            cursor = next_slot(cursor, 1U);
            state = EVENTS_CONSUME;
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
        case EVENTS_VERIFY:
            if ((MH_EVENT_SLOTS > message.body.log_head.head) &&
                (read_tail == message.body.log_head.tail) &&
                ((uint32_t)(message.body.log_head.total - read_total) <=
                 (uint32_t)(MH_EVENT_SLOTS - read_pending)))
            {
                state = EVENTS_CONSUME;
            }
            else
            {
                CSLOG_WARN("[RF] event slots changed while saving\r\n");
                head_received(&message);
            }
            break;
        case EVENTS_CONSUME:
            if ((cursor != message.body.log_consume.tail) ||
                (MH_EVENT_SLOTS < message.body.log_consume.left) ||
                stop_after_consume || (0U == message.body.log_consume.left))
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
                CSLOG_ERR("[RF] degraded MH event storage; stopped\r\n");
            }
            break;
        default:
            stop_cycle();
            break;
    }
}

static bool build_fault(const rf_scp_event_t *event, fault_log_t *record)
{
    if (((1U != event->event) && (7U != event->event) &&
         (3U != event->event)) || (1U > event->feeder) ||
        (4U < event->feeder) || (1U > event->phase) ||
        (3U < event->phase) || !isfinite(event->current_amps) ||
        (0.0F > event->current_amps) ||
        ((FLT_MAX / 10.0F) < event->current_amps) ||
        (1U < event->nominal_current_status) || (1U < event->energy_status))
    {
        return false;
    }
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; index++)
    {
        const rf_feeder_t *feeder = rf_store_get((feeder_id_t)index);

        if ((NULL != feeder) && feeder->in_use &&
            (event->zone == feeder->config.zone_id) &&
            (event->feeder == feeder->config.fider_id))
        {
            (void)memset(record, 0, sizeof(*record));
            (void)memcpy(&record->tm, event->timestamp, 7U);
            record->tm.iv_bit = (1U != event->clock_quality);
            fault_log_set_current_amps(record, event->current_amps);
            record->fault_duration_ms = event->duration_ms;
            record->info.feeder = (uint8_t)(index & 0x07U);
            record->info.phase = (uint8_t)((event->phase - 1U) & 0x03U);
            /* R1 event byte 13 and LIVE bit 1 both mean load present. */
            record->info.nominal_current_status =
                (0U != event->nominal_current_status);
            record->info.power_status = (0U != event->energy_status);
            record->info.type = (3U != event->event);
            return true;
        }
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
        if (iec104_is_link_active() &&
            iec104_emit_evtlog_record(&fault_record))
        {
            iec104_event_log_mark_sent(seq);
        }
    }
    return 0 == iec104_event_log_sync();
}

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
            state = needs_verification ? EVENTS_VERIFY : EVENTS_CONSUME;
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
            store_waiting = true;
            store_retry_ms = now_ms;
            needs_verification = true;
            return;
        }
    }
    if (fault_added || build_fault(&event, &fault_record))
    {
        if (!save_fault())
        {
            store_waiting = true;
            store_retry_ms = now_ms;
            needs_verification = true;
            return;
        }
    }
    saved_count++;
    raw_saved = false;
    fault_added = false;
    replay_added = false;
    store_waiting = false;
    if (saved_count == batch_count)
    {
        cursor = next_slot(cursor, saved_count);
        state = needs_verification ? EVENTS_VERIFY : EVENTS_CONSUME;
    }
}

static void send_command(void)
{
    scp_packet_t request = {.type = SCP_TYPE_GET};

    switch (state)
    {
        case EVENTS_HEAD:
        case EVENTS_VERIFY:
            request.cmd = RF_SCP_CMD_LOG_READ_HEAD;
            break;
        case EVENTS_READ:
            request.cmd = RF_SCP_CMD_LOG_READ_RANGE;
            request.data_len = 4U;
            scp_pack_u16(request.data, cursor);
            scp_pack_u16(&request.data[2], read_count);
            break;
        case EVENTS_CONSUME:
            request.type = SCP_TYPE_SET;
            request.cmd = RF_SCP_CMD_LOG_CONSUME_TO;
            request.data_len = 2U;
            scp_pack_u16(request.data, cursor);
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
        ((RF_INVENTORY_READY != inventory) &&
         (RF_INVENTORY_PARTIAL != inventory) &&
         (RF_INVENTORY_EMPTY != inventory)) || !scp_is_free() ||
        (EVENTS_DEGRADED == state))
    {
        return;
    }
    if (EVENTS_IDLE == state)
    {
        if (requested ||
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
