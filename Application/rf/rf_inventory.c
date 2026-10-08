/*
 * rf_inventory.c
 *
 *  Created on: Aug 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Envanter push siralayici (iterator paterni).
 *
 * Akis:
 *   rf_inventory_start()
 *     -> index'i (0,0)'a koy, send_next()
 *     -> gecerli cihaz bul -> scp_send_command(0x04, 12B)
 *     -> ACK -> index ilerlet -> send_next()  (dongu)
 *     -> cihaz kalmadi -> scp_send_command(0x05, END)
 *     -> ACK -> EMPTY / PARTIAL / READY, according to accepted entries
 *
 * Hata politikasi:
 *   Tek cihazin 0x04'u basarisiz -> uyari logla, ATLA, sonrakine gec
 *   0x05 END basarisisiz -> komut mekanizmasinin retry'i tukendiyse vazgec
 *
 * Bagimliliklar:
 *   rf_comm.h   -> scp_send_command, scp_is_free (komut mekanizmasi)
 *   rf_config.h -> rf_store_get (cihaz listesi)
 *   rf_scp.h    -> komut kodlari
 */

#define CSLOG_MODULE LOG_MOD_RF
#include "rf_inventory.h"
#include "rf_comm.h"
#include "rf_config.h"
#include "rf_scp.h"
#include "rf_log.h"
#include "rf_discovery.h"
#include "rf_events.h"
#include "rf_alarm.h"
#include "rf_faults.h"
#include "rf.h"
#include "rf_group.h"
#include "contiki.h"
#include "timer.h"
#include "stm32u3xx_hal.h"
#include <string.h>

/* ======================================================================
 * Configuration
 * ====================================================================== */

/** Documented inventory defaults: 500 ms, three extra retries. */
#define TIMEOUT_MS     500U
#define RETRIES        3U

/* ======================================================================
 * Module state (iterator index)
 * ====================================================================== */

static uint8_t  feeder_index;      /* 0..MAX_POWER_LINE_COUNT-1 */
static uint8_t  phase_index;       /* 0=R, 1=S, 2=T             */
static rf_inventory_status_t inventory_status = RF_INVENTORY_IDLE;
static uint8_t  sent_count;
static uint8_t  skipped_count;
static bool inventory_zone_set;
static uint8_t inventory_zone;
static rf_inventory_entry_t pending_update;
static scp_cmd_done_fn_t update_done;
static struct timer upload_timer;
static bool upload_timer_started;
static rf_inventory_entry_t bindings[MAX_POWER_LINE_COUNT][3];
static bool epoch_waiting[4];
static bool epoch_first_config[4];
static bool epoch_retry_pending[4];
static bool epoch_for_replacement;
static uint32_t epoch_started_ms[4];
static uint8_t epoch_feeder;
static scp_cmd_done_fn_t epoch_done;
static bool waiting_live;
static bool update_pending;
static bool update_sent;
static rf_inventory_status_t before_drain;
static bool has_boundary;
static uint32_t boundary_total;
static uint32_t uploaded_bindings;

static void begin_upload(void);
static bool send_update(void);

_Static_assert(MAX_POWER_LINE_COUNT >= 7,
               "MH inventory supports seven feeder IDs");
_Static_assert(MAX_POWER_LINE_COUNT * 3U <= 32U,
               "Accepted inventory bitmap must fit a word");

/* ======================================================================
 * Index helpers
 * ====================================================================== */

/** Index i bir faz ilerlet; faz biterse sonraki fidera gec. */
static void index_advance(void)
{
    phase_index++;
    if (phase_index >= 3U)
    {
        phase_index = 0U;
        feeder_index++;
    }
}

/**
 * @brief Index in isaret ettigi slot'tan 12 bayt govde kur.
 *
 * @return true gecerli cihaz var (in_use + EUI dolu);
 *         false bu slot bossa (index ilerletilmez, cagiran atmalar).
 */
static bool build_body_at_index(uint8_t body[RF_SCP_INV_SET_BODY_LEN])
{
    const rf_feeder_t *feeder;
    const uint8_t     *eui;

    if (feeder_index >= MAX_POWER_LINE_COUNT)
    {
        return false;
    }

    feeder = rf_store_get((feeder_id_t)feeder_index);
    if ((feeder == NULL) || (!feeder->in_use))
    {
        return false;
    }

    switch (phase_index)
    {
        case 0U:  eui = feeder->r_eui64; break;
        case 1U:  eui = feeder->s_eui64; break;
        case 2U:  eui = feeder->t_eui64; break;
        default:  return false;
    }

    if (rf_eui64_is_zero(eui))
    {
        return false;
    }

    body[0]  = feeder->config.zone_id;
    body[1]  = feeder->config.fider_id;
    body[2]  = (uint8_t)(phase_index + 1U);   /* 1=L1, 2=L2, 3=L3 */
    (void)memcpy(&body[3], eui, 8U);
    body[11] = feeder->config.rf_channel; /* Informational in R1. */

    return true;
}

/* ======================================================================
 * Command callbacks
 * ====================================================================== */

/** 0x04 INVENTORY_SET bittiginde cagrilir. */
static void on_set_done(scp_cmd_result_t result, const scp_packet_t *rsp)
{
    (void)rsp;

    if (!rf_inventory_is_active())
    {
        return;
    }

    if (SCP_CMD_OK == result)
    {
        sent_count++;
    }
    else
    {
        skipped_count++;
        CSLOG_WARN("[RF-INV] fider=%u faz=%u atlandi (hata)\r\n",
                   (unsigned)(feeder_index + 1U),
                   (unsigned)(phase_index + 1U));
    }

    /* Bu cihazi tuket, sonrakine gec */
    index_advance();
}

/** 0x05 INVENTORY_END bittiginde cagrilir. */
static void on_end_done(scp_cmd_result_t result, const scp_packet_t *rsp)
{
    (void)rsp;

    if (!rf_inventory_is_active())
    {
        return;
    }

    if (SCP_CMD_OK == result)
    {
        for (size_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
        {
            for (size_t phase = 0U; phase < 3U; phase++)
            {
                const size_t bit = feeder * 3U + phase;
                if (0U == (uploaded_bindings & (UINT32_C(1) << bit)))
                {
                    rf_faults_reset_phase((uint8_t)(feeder + 1U),
                                           (uint8_t)(phase + 1U));
                    (void)memset(&bindings[feeder][phase], 0,
                                  sizeof(bindings[feeder][phase]));
                }
            }
        }
        if (0U == sent_count)
        {
            inventory_status = (0U == skipped_count)
                             ? RF_INVENTORY_EMPTY : RF_INVENTORY_ERROR;
        }
        else if (0U < skipped_count)
        {
            inventory_status = RF_INVENTORY_PARTIAL;
        }
        else
        {
            inventory_status = RF_INVENTORY_READY;
        }
        CSLOG("[RF-INV] envanter yuklendi: %u cihaz gonderildi, %u atlandi\r\n",
              (unsigned)sent_count, (unsigned)skipped_count);
    }
    else
    {
        inventory_status = RF_INVENTORY_ERROR;
        CSLOG_WARN("[RF-INV] INVENTORY_END basarisiz - hub envanteri yuklu "
                   "sayilmaz\r\n");
    }

}

static bool is_entry_valid(const uint8_t *body)
{
    return (7U >= body[0]) && (0U < body[1]) && (7U >= body[1]) &&
           (0U < body[2]) && (3U >= body[2]) &&
           (!inventory_zone_set || (inventory_zone == body[0]));
}

/* ======================================================================
 * Sequencer
 * ====================================================================== */

/**
 * @brief Index dan itibaren sonraki gecerli cihazi bul ve 0x04 gonder.
 *        Cihaz kalmamissa 0x05 END gonder.
 *
 * Bu fonksiyon yalnizca komut mekanizmasi BOSKEN cagirilmalidir
 * (callback'ten veya rf_inventory_continue'dan).
 */
static void send_next(void)
{
    uint8_t body[RF_SCP_INV_SET_BODY_LEN];

    /* Skip empty and invalid assignments without stalling on a bad field. */
    for (;;)
    {
        if (feeder_index >= MAX_POWER_LINE_COUNT)
        {
            /* Tum slotlar tarandi -> END gonder */
            if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_END,
                                 NULL, 0,
                                 TIMEOUT_MS, RETRIES,
                                 on_end_done))
            {
                CSLOG("[RF-INV] %u cihaz tarandi, END gonderiliyor\r\n",
                      (unsigned)(sent_count + skipped_count));
            }
            else
            {
                CSLOG_WARN("[RF-INV] END gonderilemedi (mesgul)\r\n");
                if (scp_is_free())
                {
                    inventory_status = RF_INVENTORY_ERROR;
                }
            }
            return;
        }
        if (build_body_at_index(body))
        {
            if (is_entry_valid(body))
            {
                break;
            }
            CSLOG_WARN("[RF-INV] invalid assignment: "
                       "slot=%u phase=%u zone=%u fider=%u\r\n",
                       (unsigned)feeder_index, (unsigned)body[2],
                       (unsigned)body[0], (unsigned)body[1]);
            skipped_count++;
        }
        index_advance();
    }

    /* Gecerli cihaz bulundu -> 0x04 gonder */
    if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_SET,
                         body, RF_SCP_INV_SET_BODY_LEN,
                         TIMEOUT_MS, RETRIES,
                         on_set_done))
    {
        inventory_zone = body[0];
        inventory_zone_set = true;
        CSLOG("[RF-INV] fider=%u faz=%u gonderiliyor (EUI=%02X..%02X)\r\n",
              (unsigned)(feeder_index + 1U),
              (unsigned)(phase_index + 1U),
              body[3], body[10]);
    }
    else
    {
        if (scp_is_free())
        {
            inventory_status = RF_INVENTORY_ERROR;
        }
        /* Komut mekanizmasi mesgul - index bu cihazda kalir,
         * rf_inventory_continue() tekrar deneyecek. */
        CSLOG("[RF-INV] komut mesgul, bekleniyor\r\n");
    }
}

/* ======================================================================
 * Public API
 * ====================================================================== */

bool rf_inventory_start(void)
{
    if (rf_inventory_is_active() || !rf_comm_can_load_inventory() ||
        !rf_events_inventory_allowed() || rf_group_is_active())
    {
        return false;
    }

    if (rf_inventory_is_loaded() ||
        (RF_INVENTORY_ERROR == inventory_status))
    {
        before_drain = inventory_status;
        inventory_status = RF_INVENTORY_DRAINING;
        waiting_live = true;
        update_pending = false;
        upload_timer_started = false;
        return true;
    }
    begin_upload();
    return true;
}

static void begin_upload(void)
{
    rf_hub_restarted();
    uploaded_bindings = 0U;
    inventory_status = RF_INVENTORY_LOADING;
    feeder_index = 0U;
    phase_index  = 0U;
    sent_count  = 0U;
    skipped_count = 0U;
    inventory_zone_set = false;
    upload_timer_started = false;

    CSLOG("[RF-INV] envanter push basliyor\r\n");
    rf_inventory_continue();
}

bool rf_inventory_is_loaded(void)
{
    return (RF_INVENTORY_READY == inventory_status) ||
           (RF_INVENTORY_PARTIAL == inventory_status);
}

bool rf_inventory_is_active(void)
{
    return (RF_INVENTORY_LOADING == inventory_status) ||
           (RF_INVENTORY_DRAINING == inventory_status);
}

bool rf_inventory_matches_config(void)
{
    size_t expected = 0U;
    size_t accepted = 0U;

    if ((RF_INVENTORY_READY != inventory_status) &&
        (RF_INVENTORY_EMPTY != inventory_status))
    {
        return false;
    }
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        const rf_feeder_t *feeder = rf_store_get((feeder_id_t)line);

        if ((NULL == feeder) || !feeder->in_use)
        {
            continue;
        }
        if ((1U > feeder->config.fider_id) ||
            (4U < feeder->config.fider_id))
        {
            return false;
        }
        const uint8_t *members[3] =
            {feeder->r_eui64, feeder->s_eui64, feeder->t_eui64};

        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (rf_eui64_is_zero(members[phase]))
            {
                continue;
            }
            const rf_inventory_entry_t *entry =
                &bindings[(size_t)feeder->config.fider_id - 1U][phase];

            if ((entry->zone != feeder->config.zone_id) ||
                (entry->line_index != line) ||
                (entry->channel != feeder->config.rf_channel) ||
                (0 != memcmp(entry->eui64, members[phase], 8U)))
            {
                return false;
            }
            expected++;
        }
    }
    for (size_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
    {
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (!rf_eui64_is_zero(bindings[feeder][phase].eui64))
            {
                accepted++;
            }
        }
    }
    return expected == accepted;
}

static bool live_logs_empty(uint32_t now_ms)
{
    for (uint8_t feeder = 1U; 4U >= feeder; feeder++)
    {
        for (uint8_t phase = 1U; 3U >= phase; phase++)
        {
            rf_phase_data_t data;
            const uint8_t source = (uint8_t)((feeder << 2U) | phase);
            if (rf_get_source_data(source, now_ms, &data) && data.is_online &&
                (0U != data.live.log_pending))
            {
                return false;
            }
        }
    }
    return true;
}

static void continue_drain(void)
{
    if (!live_logs_empty(HAL_GetTick()))
    {
        waiting_live = true;
        return;
    }
    if (waiting_live)
    {
        waiting_live = false;
        rf_events_request_drain();
        return;
    }
    uint32_t total;
    if (!rf_alarm_is_idle() || !rf_events_drain_complete(&total) ||
        !scp_is_free() || !rf_comm_can_load_inventory())
    {
        return;
    }
    if (update_pending)
    {
        if (!send_update())
        {
            return;
        }
    }
    else
    {
        begin_upload();
    }
    boundary_total = total;
    has_boundary = true;
    rf_events_finish_drain();
}

bool rf_inventory_get_boundary(uint32_t *total)
{
    if ((NULL == total) || !has_boundary)
    {
        return false;
    }
    *total = boundary_total;
    return true;
}

static void continue_epoch_retry(void)
{
    for (uint8_t feeder = 1U; 4U >= feeder; feeder++)
    {
        if (epoch_retry_pending[feeder - 1U])
        {
            if (rf_inventory_refresh_epoch(feeder, NULL))
            {
                epoch_retry_pending[feeder - 1U] = false;
                CSLOG_WARN("[RF-INV] MH replacement: one epoch retry "
                           "for feeder %u; config remains stopped\r\n",
                           (unsigned)feeder);
            }
            return;
        }
    }
}

void rf_inventory_continue(void)
{
    if (RF_INVENTORY_DRAINING == inventory_status)
    {
        continue_drain();
        return;
    }
    if (rf_inventory_is_active() && upload_timer_started &&
        timer_expired(&upload_timer))
    {
        inventory_status = RF_INVENTORY_ERROR;
        CSLOG_WARN("[RF-INV] upload gap exceeded 10 s; "
                   "wait for BOOT/reload\r\n");
        return;
    }
    if (rf_inventory_is_active() && rf_comm_can_load_inventory() &&
        scp_is_free())
    {
        send_next();
    }
    else if (!rf_inventory_is_active())
    {
        continue_epoch_retry();
    }
    else
    {
        /* The accepted inventory/command must finish first. */
    }
}

void rf_inventory_reset(void)
{
    const scp_cmd_done_fn_t pending_done =
        (update_pending && !update_sent) ? update_done : NULL;
    if (NULL != pending_done)
    {
        update_done = NULL;
    }
    (void)memset(epoch_waiting, 0, sizeof(epoch_waiting));
    (void)memset(epoch_first_config, 0, sizeof(epoch_first_config));
    (void)memset(epoch_retry_pending, 0, sizeof(epoch_retry_pending));
    epoch_for_replacement = false;
    (void)memset(bindings, 0, sizeof(bindings));
    inventory_status = RF_INVENTORY_IDLE;
    feeder_index = 0U;
    phase_index = 0U;
    sent_count = 0U;
    skipped_count = 0U;
    inventory_zone_set = false;
    upload_timer_started = false;
    update_pending = false;
    update_sent = false;
    waiting_live = false;
    has_boundary = false;
    if (NULL != pending_done)
    {
        pending_done(SCP_CMD_RESTARTED, NULL);
    }
}

void rf_inventory_request_sent(uint8_t cmd)
{
    if (rf_inventory_is_active() &&
        ((RF_SCP_CMD_INVENTORY_SET == cmd) ||
         (RF_SCP_CMD_INVENTORY_END == cmd)))
    {
        timer_set(&upload_timer, CLOCK_SECOND * 10U + 1U);
        upload_timer_started = true;
    }
}

rf_inventory_status_t rf_inventory_get_status(void)
{
    return inventory_status;
}

void rf_inventory_record_ack(const scp_packet_t *request)
{
    if ((NULL == request) || (SCP_TYPE_SET != request->type) ||
        (RF_SCP_ADDR_RTU != request->src) ||
        (RF_SCP_ADDR_HUB != request->dst) ||
        ((RF_SCP_CMD_INVENTORY_SET != request->cmd) &&
         (RF_SCP_CMD_INVENTORY_UPDATE != request->cmd)) ||
        (12U != request->data_len))
    {
        return;
    }
    const uint8_t *body = request->data;

    if ((7U < body[0]) || (7U < body[1]) || (1U > body[2]) ||
        (3U < body[2]) ||
        ((0U == body[1]) && (RF_SCP_CMD_INVENTORY_SET == request->cmd)))
    {
        return;
    }
    if ((RF_SCP_CMD_INVENTORY_SET == request->cmd) &&
        (RF_INVENTORY_LOADING == inventory_status))
    {
        const uint32_t bit = ((uint32_t)body[1] - 1U) * 3U + body[2] - 1U;
        uploaded_bindings |= UINT32_C(1) << bit;
    }
    if (0U < body[1])
    {
        const rf_inventory_entry_t *current =
            &bindings[(size_t)body[1] - 1U][(size_t)body[2] - 1U];
        if (!rf_eui64_is_zero(current->eui64) &&
            ((current->zone != body[0]) ||
             (0 != memcmp(current->eui64, &body[3], 8U))))
        {
            rf_faults_reset_phase(body[1], body[2]);
        }
    }
    /* Same EUI moves to its new assignment; deletion is idempotent. */
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (0 == memcmp(bindings[line][phase].eui64, &body[3], 8U))
            {
                if ((body[1] != line + 1U) || (body[2] != phase + 1U))
                {
                    rf_faults_reset_phase((uint8_t)(line + 1U),
                                           (uint8_t)(phase + 1U));
                }
                (void)memset(&bindings[line][phase], 0,
                              sizeof(bindings[line][phase]));
            }
        }
    }
    if (0U < body[1])
    {
        rf_inventory_entry_t *entry =
            &bindings[(size_t)body[1] - 1U][(size_t)body[2] - 1U];

        entry->zone = body[0];
        entry->feeder = body[1];
        entry->phase = body[2];
        (void)memcpy(entry->eui64, &body[3], 8U);
        entry->channel = body[11];
        entry->line_index = UINT8_MAX;
        for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
        {
            const rf_feeder_t *desired = rf_store_get((feeder_id_t)line);
            if ((NULL != desired) && desired->in_use &&
                (desired->config.zone_id == body[0]) &&
                (desired->config.fider_id == body[1]))
            {
                entry->line_index = (uint8_t)line;
                break;
            }
        }
    }
}

bool rf_inventory_get_binding(uint8_t source, rf_inventory_entry_t *out)
{
    uint8_t feeder = (source >> 2U) & 0x07U;
    uint8_t phase = source & 0x03U;

    if ((NULL == out) || (0U == feeder) || (4U < feeder) || (0U == phase))
    {
        return false;
    }
    const rf_inventory_entry_t *entry =
        &bindings[(size_t)feeder - 1U][(size_t)phase - 1U];

    if ((feeder != entry->feeder) || (phase != entry->phase))
    {
        return false;
    }
    *out = *entry;
    return true;
}

static void on_update_done(scp_cmd_result_t result,
                            const scp_packet_t *response)
{
    scp_cmd_done_fn_t done = update_done;

    update_done = NULL;
    update_pending = false;
    update_sent = false;
    if (SCP_CMD_RESTARTED != result)
    {
        inventory_status = before_drain;
    }
    if (SCP_CMD_OK != result)
    {
        has_boundary = false;
    }
    if (SCP_CMD_OK == result)
    {
        (void)rf_discovery_remove(pending_update.eui64);
    }
    if (NULL != done)
    {
        done(result, response);
    }
}

bool rf_inventory_update(const rf_inventory_entry_t *entry,
                          scp_cmd_done_fn_t done)
{
    if ((NULL == entry) || !rf_inventory_is_loaded() ||
        !rf_comm_can_load_inventory() || !scp_is_free() || rf_group_is_active())
    {
        return false;
    }
    if ((7U < entry->zone) || (4U < entry->feeder) ||
        (1U > entry->phase) || (3U < entry->phase) ||
        rf_eui64_is_zero(entry->eui64))
    {
        return false;
    }
    if (inventory_zone_set && (inventory_zone != entry->zone))
    {
        return false;
    }
    pending_update = *entry;
    update_done = done;
    before_drain = inventory_status;
    inventory_status = RF_INVENTORY_DRAINING;
    waiting_live = true;
    update_pending = true;
    update_sent = false;
    upload_timer_started = false;
    return true;
}

static bool send_update(void)
{
    const rf_inventory_entry_t *entry = &pending_update;
    uint8_t body[12];
    body[0] = entry->zone;
    body[1] = entry->feeder;
    body[2] = entry->phase;
    (void)memcpy(&body[3], entry->eui64, 8U);
    body[11] = entry->channel;
    if (!scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_UPDATE,
                          body, sizeof(body), TIMEOUT_MS, RETRIES,
                          on_update_done))
    {
        return false;
    }
    update_sent = true;
    return true;
}

bool rf_inventory_epoch_ready(uint8_t feeder)
{
    if ((1U > feeder) || (4U < feeder))
    {
        return false;
    }
    const size_t index = (size_t)feeder - 1U;

    if (epoch_waiting[index] &&
        (90000U <= (uint32_t)(HAL_GetTick() - epoch_started_ms[index])))
    {
        epoch_waiting[index] = false;
    }
    return !epoch_waiting[index];
}

static void on_epoch_refresh_done(scp_cmd_result_t result, const scp_packet_t *packet)
{
    scp_cmd_done_fn_t done = epoch_done;

    epoch_done = NULL;
    if (SCP_CMD_OK == result)
    {
        const size_t index = (size_t)epoch_feeder - 1U;

        epoch_started_ms[index] = HAL_GetTick();
        epoch_waiting[index] = true;
        epoch_first_config[index] = epoch_for_replacement;
    }
    epoch_for_replacement = false;
    if (NULL != done)
    {
        done(result, packet);
    }
}

bool rf_inventory_refresh_epoch(uint8_t feeder, scp_cmd_done_fn_t done)
{
    if ((1U > feeder) || (4U < feeder) || !rf_inventory_is_loaded() ||
        !rf_comm_can_load_inventory() || rf_group_is_active())
    {
        return false;
    }
    /* BQ-07: the MH uses one RF network; pace different feeders too. */
    for (uint8_t current = 1U; 4U >= current; current++)
    {
        if (!rf_inventory_epoch_ready(current))
        {
            return false;
        }
    }
    if (!scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_EPOCH_REFRESH,
                          &feeder, 1U, 1000U, 1U, on_epoch_refresh_done))
    {
        return false;
    }
    epoch_feeder = feeder;
    epoch_done = done;
    epoch_for_replacement = false;
    return true;
}

bool rf_inventory_hub_replaced(uint8_t feeder, scp_cmd_done_fn_t done)
{
    if (!rf_inventory_refresh_epoch(feeder, done))
    {
        return false;
    }
    epoch_first_config[feeder - 1U] = false;
    epoch_retry_pending[feeder - 1U] = false;
    epoch_for_replacement = true;
    return true;
}

void rf_inventory_config_finished(uint8_t feeder, uint8_t reason)
{
    if ((1U > feeder) || (4U < feeder))
    {
        return;
    }
    const size_t index = (size_t)feeder - 1U;

    if (epoch_first_config[index])
    {
        epoch_first_config[index] = false;
        epoch_retry_pending[index] = (5U == reason);
    }
}

/*** end of file ***/
