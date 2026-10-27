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
#include "rf_apply.h"
#include "nvram.h"
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
static size_t delete_index;
static size_t saved_delete_index;
static bool deleting_saved;
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
static bool epoch_auth_pending[4];
static bool epoch_for_replacement;
static uint32_t epoch_started_ms[4];
static uint8_t epoch_feeder;
static scp_cmd_done_fn_t epoch_done;
typedef struct
{
    rf_epoch_status_t status;
    uint32_t report_ms;
    uint8_t empty_phases;
    uint8_t head_phases;
    uint16_t boot_counter[3];
} epoch_monitor_t;

static epoch_monitor_t epoch_monitors[4];
static bool waiting_live;
static uint32_t drain_started_ms;
static uint32_t reason_started_ms;
static const char *drain_reason = "none";

static bool update_pending;
static bool update_sent;
static rf_inventory_status_t before_drain;
static bool has_boundary;
static uint32_t boundary_total;
static uint32_t uploaded_bindings;

/* Cooperative service transaction. Only the interruption marker is persistent;
 * evidence and the one-extra-EPOCH campaign remain in the existing monitor. */
static rf_move_status_t move_status;
static uint32_t move_epoch_ms;
static void continue_move(void);
static bool persist_move_target(void);
static void on_move_update_done(scp_cmd_result_t result, const scp_packet_t *response);

static void begin_upload(void);
static bool send_update(void);
static bool complete_inventory_save(void);

static void interrupt_epoch(size_t index)
{
    if ((index < 4U) && epoch_monitors[index].status.active)
    {
        epoch_monitors[index].status.active = false;
        epoch_monitors[index].status.uncertain = true;
        epoch_retry_pending[index] = false;
        epoch_first_config[index] = false;
    }
}

_Static_assert(MAX_POWER_LINE_COUNT >= 7,
               "MH inventory supports seven feeder IDs");
_Static_assert(MAX_POWER_LINE_COUNT * 3U <= 32U,
               "Accepted inventory bitmap must fit a word");

static void set_drain_reason(const char *reason)
{
    if (0 != strcmp(drain_reason, reason))
    {
        drain_reason = reason;
        reason_started_ms = HAL_GetTick();
        CSLOG_WARN("[RF-INV] kayit bekleme nedeni: %s\r\n", reason);
    }
}

static void start_drain_timer(void)
{
    drain_started_ms = HAL_GetTick();
    reason_started_ms = drain_started_ms;
    drain_reason = "checking";
}

void rf_inventory_get_wait(rf_inventory_wait_t *out)
{
    if (NULL != out)
    {
        *out = (rf_inventory_wait_t){.reason = "none"};
        if (RF_INVENTORY_DRAINING == rf_inventory_get_status())
        {
            const uint32_t now_ms = HAL_GetTick();
            out->reason = drain_reason;
            out->total_ms = now_ms - drain_started_ms;
            out->reason_ms = now_ms - reason_started_ms;
        }
    }
}

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
        CSLOG_WARN("[RF-INV] satir=%u faz=%u atlandi (hata)\r\n",
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
        if ((0U == skipped_count) && !complete_inventory_save())
        {
            inventory_status = RF_INVENTORY_ERROR;
            return;
        }
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
        CSLOG_WARN("[RF-INV] envanter yuklendi: %u cihaz gonderildi, "
                   "%u atlandi\r\n",
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
static bool assignment_retained(const rf_inventory_entry_t *entry,
                                bool staged)
{
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        const rf_feeder_t *feeder = staged ?
            rf_store_get_mutable((feeder_id_t)line) :
            rf_store_get((feeder_id_t)line);
        if ((NULL != feeder) && feeder->in_use &&
            (feeder->config.zone_id == entry->zone) &&
            (feeder->config.fider_id == entry->feeder))
        {
            const uint8_t *members[3] =
                {feeder->r_eui64, feeder->s_eui64, feeder->t_eui64};
            return 0 == memcmp(members[entry->phase - 1U], entry->eui64, 8U);
        }
    }
    return false;
}

static bool entry_relocated(const rf_inventory_entry_t *entry, bool staged)
{
    if (rf_eui64_is_zero(entry->eui64))
    {
        return false;
    }
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        const rf_feeder_t *target = staged ? rf_store_get_mutable((feeder_id_t)line) :
                                            rf_store_get((feeder_id_t)line);
        if ((target == NULL) || !target->in_use)
        {
            continue;
        }
        const uint8_t *members[3] = {target->r_eui64, target->s_eui64, target->t_eui64};
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (memcmp(entry->eui64, members[phase], 8U) == 0)
            {
                return (entry->zone != target->config.zone_id) ||
                       (entry->feeder != target->config.fider_id) || (entry->phase != phase + 1U);
            }
        }
    }
    return false;
}

bool rf_inventory_has_relocation(void)
{
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        const rf_feeder_t *current = rf_store_get((feeder_id_t)line);
        if (current == NULL)
        {
            continue;
        }
        const uint8_t *members[3] = {current->r_eui64, current->s_eui64, current->t_eui64};
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            rf_inventory_entry_t entry = {.zone = current->config.zone_id,
                .feeder = current->config.fider_id, .phase = (uint8_t)(phase + 1U)};
            (void)memcpy(entry.eui64, members[phase], 8U);
            if (entry_relocated(&entry, true))
            {
                return true;
            }
        }
    }
    return false;
}

static bool bindings_relocated(void)
{
    for (size_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (entry_relocated(&bindings[line][phase], false))
            {
                return true;
            }
        }
    }
    return false;
}

static void on_delete_done(scp_cmd_result_t result, const scp_packet_t *rsp)
{
    (void)rsp;
    if (RF_INVENTORY_LOADING != inventory_status)
    {
        return;
    }
    if (SCP_CMD_OK != result)
    {
        /* Do not upload or prune local bindings after an unconfirmed delete. */
        inventory_status = RF_INVENTORY_ERROR;
        return;
    }
    if (deleting_saved)
    {
        saved_delete_index++;
    }
    else
    {
        delete_index++;
    }
}

static bool send_delete(uint8_t zone, uint8_t phase,
                         const uint8_t eui64[8], uint8_t channel)
{
    uint8_t body[RF_SCP_INV_SET_BODY_LEN] = {0};
    body[0] = zone;
    body[2] = phase;
    (void)memcpy(&body[3], eui64, 8U);
    body[11] = channel;
    if (!scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_UPDATE,
                          body, sizeof(body), TIMEOUT_MS, RETRIES,
                          on_delete_done) && scp_is_free())
    {
        inventory_status = RF_INVENTORY_ERROR;
    }
    return true;
}

static bool delete_next(void)
{
    const rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    if (journal->pending && (RF_INVENTORY_JOURNAL_CAPACITY < journal->count))
    {
        inventory_status = RF_INVENTORY_ERROR;
        return true;
    }
    if (journal->pending && (journal->count > saved_delete_index))
    {
        const rf_inventory_delete_t *entry = &journal->entries[saved_delete_index];
        if ((7U < entry->zone) || (1U > entry->phase) ||
            (3U < entry->phase) || rf_eui64_is_zero(entry->eui64))
        {
            inventory_status = RF_INVENTORY_ERROR;
            return true;
        }
        deleting_saved = true;
        return send_delete(entry->zone, entry->phase, entry->eui64,
                           entry->channel);
    }
    deleting_saved = false;
    for (; MAX_POWER_LINE_COUNT * 3U > delete_index; delete_index++)
    {
        const rf_inventory_entry_t *entry =
            &bindings[delete_index / 3U][delete_index % 3U];
        if (rf_eui64_is_zero(entry->eui64) || assignment_retained(entry, false))
        {
            continue;
        }
        return send_delete(entry->zone, entry->phase, entry->eui64,
                           entry->channel);
    }
    return false;
}

static void send_next(void)
{
    uint8_t body[RF_SCP_INV_SET_BODY_LEN];

    if (delete_next())
    {
        return;
    }

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
                CSLOG_WARN("[RF-INV] %u cihaz tarandi, END gonderiliyor\r\n",
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
        CSLOG_WARN("[RF-INV] satir=%u fider=%u faz=%u "
              "gonderiliyor (EUI=%02X..%02X)\r\n",
              (unsigned)(feeder_index + 1U), (unsigned)body[1],
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
        !rf_events_inventory_allowed() || rf_group_is_active() || bindings_relocated())
    {
        return false;
    }

    if (rf_inventory_is_loaded() ||
        (RF_INVENTORY_ERROR == inventory_status))
    {
        before_drain = inventory_status;
        inventory_status = RF_INVENTORY_DRAINING;
        start_drain_timer();
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
    delete_index = 0U;
    saved_delete_index = 0U;
    inventory_status = RF_INVENTORY_LOADING;
    feeder_index = 0U;
    phase_index  = 0U;
    sent_count  = 0U;
    skipped_count = 0U;
    inventory_zone_set = false;
    upload_timer_started = false;

    CSLOG_WARN("[RF-INV] envanter push basliyor\r\n");
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

static bool matches_store(bool staged)
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
        const rf_feeder_t *feeder = staged ?
            rf_store_get_mutable((feeder_id_t)line) :
            rf_store_get((feeder_id_t)line);

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

bool rf_inventory_has_pending(void)
{
    return 0U != nvram_get_rf_inventory_journal()->pending;
}

bool rf_inventory_matches_config(void)
{
    return !rf_inventory_has_pending() && matches_store(false);
}

static bool feeder_has_assignments(const rf_feeder_t *feeder)
{
    return !rf_eui64_is_zero(feeder->r_eui64) ||
           !rf_eui64_is_zero(feeder->s_eui64) ||
           !rf_eui64_is_zero(feeder->t_eui64);
}

static bool is_known_legacy_device(const uint8_t eui64[8])
{
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        const rf_feeder_t *current = rf_store_get((feeder_id_t)line);
        if ((0U == current->config.zone_id) &&
            ((0 == memcmp(current->r_eui64, eui64, 8U)) ||
             (0 == memcmp(current->s_eui64, eui64, 8U)) ||
             (0 == memcmp(current->t_eui64, eui64, 8U))))
        {
            return true;
        }
    }
    return false;
}

static bool target_zone_matches(void)
{
    bool has_zone = inventory_zone_set;
    uint8_t zone = inventory_zone;
    /* Saved device assignments preserve the region even before MH upload. */
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        const rf_feeder_t *current = rf_store_get((feeder_id_t)line);
        if (feeder_has_assignments(current))
        {
            if (has_zone && (zone != current->config.zone_id))
            {
                return false;
            }
            zone = current->config.zone_id;
            has_zone = true;
        }
    }
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        const rf_feeder_t *next = rf_store_get_mutable((feeder_id_t)line);
        if (next->in_use && feeder_has_assignments(next))
        {
            if (has_zone && (zone != next->config.zone_id))
            {
                return false;
            }
            zone = next->config.zone_id;
            has_zone = true;
        }
    }
    return true;
}

bool rf_inventory_prepare_save(void)
{
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    if (rf_inventory_service_pending() || nvram_is_busy() || !target_zone_matches())
    {
        return false;
    }
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        const rf_feeder_t *current = rf_store_get((feeder_id_t)line);
        const rf_feeder_t *next = rf_store_get_mutable((feeder_id_t)line);
        const bool assigned = feeder_has_assignments(current);
        if (0U == next->config.zone_id)
        {
            const uint8_t *members[3] =
                {next->r_eui64, next->s_eui64, next->t_eui64};
            for (size_t phase = 0U; 3U > phase; phase++)
            {
                if (!rf_eui64_is_zero(members[phase]) &&
                    !is_known_legacy_device(members[phase]))
                {
                    return false;
                }
            }
        }
        if ((assigned && (current->config.zone_id != next->config.zone_id)) ||
            (next->in_use && (0U == next->config.zone_id) &&
             (!assigned || (0U != current->config.zone_id))))
        {
            return false;
        }
    }
    if (journal->pending)
    {
        /* Retry the saved target; never replace an unfinished transaction. */
        for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
        {
            if (0 != memcmp(rf_store_get((feeder_id_t)line),
                            rf_store_get_mutable((feeder_id_t)line),
                            sizeof(rf_feeder_t)))
            {
                return false;
            }
        }
        return RF_INVENTORY_JOURNAL_CAPACITY >= journal->count;
    }
    if (matches_store(true))
    {
        return true;
    }
    rf_inventory_journal_t candidate = {.pending = 1U};
    for (size_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        for (size_t phase = 0U; 3U > phase; phase++)
        {
            const rf_inventory_entry_t *entry = &bindings[line][phase];
            if (rf_eui64_is_zero(entry->eui64) ||
                assignment_retained(entry, true))
            {
                continue;
            }
            if (RF_INVENTORY_JOURNAL_CAPACITY <= candidate.count)
            {
                return false;
            }
            rf_inventory_delete_t *saved = &candidate.entries[candidate.count++];
            (void)memcpy(saved->eui64, entry->eui64, 8U);
            saved->zone = entry->zone;
            saved->phase = entry->phase;
            saved->channel = entry->channel;
        }
    }
    *journal = candidate;
    return true;
}

static bool complete_inventory_save(void)
{
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    if (!journal->pending)
    {
        return true;
    }
    const rf_inventory_journal_t previous = *journal;
    *journal = (rf_inventory_journal_t){.reserved = previous.reserved};
    if (NVRAM_SAVE_COMPLETE != nvram_save(false))
    {
        *journal = previous;
        return false;
    }
    return true;
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
        set_drain_reason("ay_logs");
        return;
    }
    if (waiting_live)
    {
        waiting_live = false;
        rf_events_request_drain();
        set_drain_reason("mh_records");
        return;
    }
    uint32_t total;
    if (!rf_alarm_is_idle())
    {
        set_drain_reason("alarm");
        return;
    }
    if (!rf_events_drain_complete(&total))
    {
        set_drain_reason(rf_events_drain_reason());
        return;
    }
    if (!scp_is_free())
    {
        set_drain_reason("scp");
        return;
    }
    if (!rf_comm_can_load_inventory())
    {
        set_drain_reason("hub_ready");
        return;
    }
    if (update_pending)
    {
        if ((move_status.state == RF_MOVE_DRAINING) && !persist_move_target())
        {
            return;
        }
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
        if (epoch_auth_pending[feeder - 1U])
        {
            (void)rf_inventory_hub_replaced(feeder, NULL);
            return;
        }
        if (epoch_retry_pending[feeder - 1U])
        {
            if (rf_inventory_refresh_epoch(feeder, NULL))
            {
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
    continue_move();
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
    if (nvram_get_rf_inventory_journal()->reserved == 0U)
    {
        move_status.state = RF_MOVE_IDLE;
    }
    else if (move_status.state != RF_MOVE_WAIT_LIVE)
    {
        move_status.state = rf_inventory_service_pending() ? RF_MOVE_INTERRUPTED : RF_MOVE_IDLE;
    }
    const scp_cmd_done_fn_t pending_done =
        (update_pending && !update_sent) ? update_done : NULL;
    if (NULL != pending_done)
    {
        update_done = NULL;
    }
    (void)memset(epoch_waiting, 0, sizeof(epoch_waiting));
    (void)memset(epoch_first_config, 0, sizeof(epoch_first_config));
    (void)memset(epoch_retry_pending, 0, sizeof(epoch_retry_pending));
    (void)memset(epoch_auth_pending, 0, sizeof(epoch_auth_pending));
    (void)memset(epoch_monitors, 0, sizeof(epoch_monitors));
    epoch_for_replacement = false;
    (void)memset(bindings, 0, sizeof(bindings));
    inventory_status = RF_INVENTORY_IDLE;
    delete_index = 0U;
    saved_delete_index = 0U;
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
         (RF_SCP_CMD_INVENTORY_END == cmd) ||
         (RF_SCP_CMD_INVENTORY_UPDATE == cmd)))
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
        if ((current->zone != body[0]) || (memcmp(current->eui64, &body[3], 8U) != 0))
        {
            interrupt_epoch((size_t)body[1] - 1U);
        }
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
                    interrupt_epoch(line);
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

        (void)rf_discovery_remove(&body[3]);
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
    else if (!rf_eui64_is_zero(&body[3]))
    {
        /* A confirmed removal is assignable without a new RF report. */
        (void)rf_discovery_add(&body[3]);
    }
    else
    {
        /* An empty identity cannot be offered for assignment. */
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
    if (NULL != done)
    {
        done(result, response);
    }
}

bool rf_inventory_update(const rf_inventory_entry_t *entry,
                          scp_cmd_done_fn_t done)
{
    if ((NULL == entry) || rf_inventory_service_pending() || !rf_inventory_is_loaded() ||
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
    if ((0U != entry->feeder) && (0U == entry->zone))
    {
        return false; /* Region zero remains usable only for legacy deletion. */
    }
    if (inventory_zone_set && (inventory_zone != entry->zone))
    {
        return false;
    }
    for (size_t line = 0U; (entry->feeder != 0U) && (line < MAX_POWER_LINE_COUNT); line++)
    {
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            const rf_inventory_entry_t *current = &bindings[line][phase];
            if ((memcmp(current->eui64, entry->eui64, 8U) == 0) &&
                ((current->feeder != entry->feeder) || (current->phase != entry->phase)))
            {
                return false;
            }
        }
    }
    pending_update = *entry;
    update_done = done;
    before_drain = inventory_status;
    inventory_status = RF_INVENTORY_DRAINING;
    start_drain_timer();
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
    return !rf_inventory_service_pending() && !epoch_waiting[index] &&
           !epoch_auth_pending[index] && !epoch_retry_pending[index];
}

bool rf_inventory_get_epoch_status(uint8_t feeder, rf_epoch_status_t *out)
{
    if ((feeder < 1U) || (feeder > 4U) || (out == NULL))
    {
        return false;
    }
    *out = epoch_monitors[feeder - 1U].status;
    return true;
}

void rf_inventory_epoch_event(const rf_scp_event_t *event, bool source_matches, uint32_t now_ms)
{
    if (event == NULL)
    {
        return;
    }
    if ((event->event == 138U) && (event->src_eui_hash == 0U))
    {
        for (size_t index = 0U; index < 4U; index++)
        {
            interrupt_epoch(index);
        }
        return;
    }
    if ((event->feeder < 1U) || (event->feeder > 4U))
    {
        return;
    }
    epoch_monitor_t *monitor = &epoch_monitors[event->feeder - 1U];
    if (!monitor->status.active)
    {
        return;
    }
    if ((event->event == 201U) && (event->src_eui_hash == 0U) && !monitor->status.has_report)
    {
        monitor->status.has_report = true;
        monitor->status.report_phases = (uint8_t)(event->permanent_faults & 7U);
        monitor->report_ms = now_ms;
        monitor->empty_phases = 0U;
        monitor->head_phases = 0U;
        monitor->status.checked_phases = 0U;
        monitor->status.missing_phases = 0U;
    }
    else if ((event->event == 120U) && source_matches && (event->phase >= 1U) && (event->phase <= 3U))
    {
        /* Receipt is diagnostic: delayed 120 records have no campaign ID. */
        const uint8_t bit = (uint8_t)(1U << (event->phase - 1U));
        monitor->status.received_phases |= bit;
        monitor->status.missing_phases &= (uint8_t)~bit;
    }
    else
    {
        /* Other records cannot prove discovery completion. */
    }
}

void rf_inventory_epoch_live(const rf_scp_live_t *live, uint32_t now_ms)
{
    if (live == NULL)
    {
        return;
    }
    const uint8_t feeder = live->source >> 2U;
    const uint8_t phase = live->source & 3U;
    rf_inventory_entry_t binding;
    if ((feeder < 1U) || (feeder > 4U) || (phase == 0U) ||
        (live->state > 6U) || (live->log_pending > 99U) ||
        !rf_inventory_get_binding(live->source, &binding))
    {
        return;
    }
    epoch_monitor_t *monitor = &epoch_monitors[feeder - 1U];
    if (!monitor->status.active)
    {
        return;
    }
    const uint8_t bit = (uint8_t)(1U << (phase - 1U));
    rf_phase_data_t previous;
    const bool restarted = rf_get_source_data(live->source, now_ms, &previous) && previous.has_live &&
        rf_scp_uptime_discontinuous(previous.live.uptime_sec, previous.last_live_ms, live->uptime_sec, now_ms);
    if (restarted || (live->log_pending >= 99U) ||
        ((monitor->boot_counter[phase - 1U] != 0U) && (live->boot_counter != 0U) &&
         (monitor->boot_counter[phase - 1U] != live->boot_counter)))
    {
        interrupt_epoch(feeder - 1U);
        return;
    }
    if (live->boot_counter != 0U)
    {
        monitor->boot_counter[phase - 1U] = live->boot_counter;
    }
    const uint32_t anchor = monitor->status.has_report ? monitor->report_ms : epoch_started_ms[feeder - 1U];
    const uint32_t delay_ms = monitor->status.has_report ? 90000U : 180000U;
    if ((live->log_pending == 0U) && ((uint32_t)(now_ms - anchor) >= delay_ms))
    {
        monitor->empty_phases |= bit;
    }
    else
    {
        monitor->empty_phases &= (uint8_t)~bit;
        monitor->head_phases &= (uint8_t)~bit;
    }
}

void rf_inventory_epoch_head_started(void)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        epoch_monitors[index].head_phases = epoch_monitors[index].empty_phases;
    }
}

void rf_inventory_epoch_ring_empty(void)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        epoch_monitor_t *monitor = &epoch_monitors[index];
        if (!monitor->status.active)
        {
            continue;
        }
        monitor->status.checked_phases |= monitor->head_phases;
        monitor->status.missing_phases = (uint8_t)(monitor->status.expected_phases &
            monitor->status.checked_phases & (uint8_t)~monitor->status.received_phases);
        monitor->head_phases = 0U;
        if ((monitor->status.missing_phases != 0U) && !monitor->status.retry_used)
        {
            if (!epoch_retry_pending[index])
            {
                CSLOG_WARN("[RF-INV] feeder=%u missing 120 mask=0x%02X; one extra epoch queued\r\n",
                           (unsigned)(index + 1U), (unsigned)monitor->status.missing_phases);
            }
            epoch_retry_pending[index] = true;
        }
    }
}

static void on_epoch_refresh_done(scp_cmd_result_t result, const scp_packet_t *packet)
{
    scp_cmd_done_fn_t done = epoch_done;

    epoch_done = NULL;
    if (SCP_CMD_OK == result)
    {
        const size_t index = (size_t)epoch_feeder - 1U;

        const bool retry = epoch_retry_pending[index];
        epoch_monitors[index] = (epoch_monitor_t){.status = {.active = true, .retry_used = retry}};
        for (size_t phase = 0U; phase < 3U; phase++)
        {
            if (!rf_eui64_is_zero(bindings[index][phase].eui64))
            {
                epoch_monitors[index].status.expected_phases |= (uint8_t)(1U << phase);
                rf_phase_data_t data;
                const uint8_t source = (uint8_t)((epoch_feeder << 2U) | (uint8_t)(phase + 1U));
                if (rf_get_source_data(source, HAL_GetTick(), &data) && data.has_live)
                {
                    epoch_monitors[index].boot_counter[phase] = data.live.boot_counter;
                }
            }
        }
        epoch_started_ms[index] = HAL_GetTick();
        epoch_waiting[index] = true;
        epoch_first_config[index] = epoch_for_replacement && !retry;
        epoch_auth_pending[index] = false;
        epoch_retry_pending[index] = false;
    }
    else if ((NULL == packet) || (SCP_CMD_ERR != result) ||
             (0U == packet->data_len) ||
             (RF_SCP_ERR_BUSY != packet->data[0]))
    {
        /* Only queue-busy has the documented repeat authorization. */
        epoch_auth_pending[epoch_feeder - 1U] = false;
        epoch_retry_pending[epoch_feeder - 1U] = false;
        epoch_monitors[epoch_feeder - 1U].status.active = false;
        epoch_monitors[epoch_feeder - 1U].status.uncertain = true;
    }
    epoch_for_replacement = false;
    if (NULL != done)
    {
        done(result, packet);
    }
}

bool rf_inventory_refresh_epoch(uint8_t feeder, scp_cmd_done_fn_t done)
{
    if (rf_inventory_service_pending() &&
        (((move_status.state != RF_MOVE_EPOCH) && (move_status.state != RF_MOVE_WAIT_EVENTS)) ||
         (feeder != pending_update.feeder)))
    {
        return false;
    }
    if ((1U > feeder) || (4U < feeder) || !rf_inventory_is_loaded() ||
        !rf_comm_can_load_inventory() || rf_group_is_active())
    {
        return false;
    }
    /* BQ-07: the MH uses one RF network; pace different feeders too. */
    for (uint8_t current = 1U; 4U >= current; current++)
    {
        (void)rf_inventory_epoch_ready(current);
        if (epoch_waiting[current - 1U])
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
    if (rf_inventory_service_pending())
    {
        return false;
    }
    if (!rf_inventory_refresh_epoch(feeder, done))
    {
        return false;
    }
    epoch_first_config[feeder - 1U] = false;
    epoch_retry_pending[feeder - 1U] = false;
    epoch_for_replacement = true;
    return true;
}

void rf_inventory_note_auth_failure(uint8_t feeder)
{
    if ((1U <= feeder) && (4U >= feeder) &&
        !epoch_monitors[feeder - 1U].status.active &&
        !epoch_monitors[feeder - 1U].status.uncertain &&
        !epoch_waiting[feeder - 1U] && !epoch_first_config[feeder - 1U] &&
        !epoch_retry_pending[feeder - 1U])
    {
        epoch_auth_pending[feeder - 1U] = true;
    }
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
        epoch_retry_pending[index] = epoch_retry_pending[index] ||
            ((5U == reason) && !epoch_monitors[index].status.retry_used);
    }
    if (0U == reason)
    {
        epoch_monitors[index].status.active = false;
        epoch_monitors[index].status.missing_phases = 0U;
        epoch_retry_pending[index] = false;
    }
}

bool rf_inventory_service_pending(void)
{
    return (nvram_get_rf_inventory_journal()->reserved != 0U) ||
           (move_status.state == RF_MOVE_DRAINING);
}

void rf_inventory_move_status(rf_move_status_t *out)
{
    if (out == NULL)
    {
        return;
    }
    *out = move_status;
    const uint16_t marker = nvram_get_rf_inventory_journal()->reserved;
    if ((marker != 0U) && ((move_status.state == RF_MOVE_IDLE) ||
        (move_status.state == RF_MOVE_INTERRUPTED)))
    {
        out->state = RF_MOVE_INTERRUPTED;
        out->line = (uint8_t)(marker & 7U);
        out->from_phase = (uint8_t)((marker >> 3U) & 3U);
        out->to_phase = (uint8_t)((marker >> 5U) & 3U);
    }
}

static uint8_t *move_member(rf_feeder_t *feeder, uint8_t phase)
{
    switch (phase)
    {
        case 0U: return feeder->r_eui64;
        case 1U: return feeder->s_eui64;
        case 2U: return feeder->t_eui64;
        default: return NULL;
    }
}

static uint16_t move_marker(void)
{
    return (uint16_t)(0xA500U | move_status.line | ((uint16_t)move_status.from_phase << 3U) |
                      ((uint16_t)move_status.to_phase << 5U));
}

bool rf_inventory_move_begin(uint8_t line, uint8_t from_phase, uint8_t to_phase, bool confirm_unused)
{
    const rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    if (!confirm_unused || (line >= MAX_POWER_LINE_COUNT) || (from_phase >= 3U) || (to_phase >= 3U) ||
        (from_phase == to_phase) || rf_inventory_service_pending() || journal->pending || (journal->count != 0U) ||
        nvram_is_busy() || rf_events_storage_degraded() || rf_inventory_is_active() ||
        rf_apply_is_running() || rf_group_is_active() ||
        !rf_inventory_matches_config() || !rf_comm_can_load_inventory() || !scp_is_free())
    {
        return false;
    }
    const rf_feeder_t *current = rf_store_get((feeder_id_t)line);
    if ((current == NULL) || !current->in_use || (current->config.zone_id < 1U) ||
        (current->config.zone_id > 7U) || (current->config.fider_id < 1U) || (current->config.fider_id > 4U))
    {
        return false;
    }
    rf_feeder_t candidate = *current;
    const uint8_t *identity = move_member(&candidate, from_phase);
    rf_inventory_entry_t original;
    rf_inventory_entry_t occupied;
    const uint8_t old_source = (uint8_t)((current->config.fider_id << 2U) | (from_phase + 1U));
    const uint8_t new_source = (uint8_t)((current->config.fider_id << 2U) | (to_phase + 1U));
    if (rf_eui64_is_zero(identity) || !rf_eui64_is_zero(move_member(&candidate, to_phase)) ||
        !rf_inventory_get_binding(old_source, &original) ||
        (original.zone != current->config.zone_id) || (memcmp(original.eui64, identity, 8U) != 0) ||
        rf_inventory_get_binding(new_source, &occupied))
    {
        return false;
    }
    pending_update = original;
    pending_update.phase = (uint8_t)(to_phase + 1U);
    update_done = on_move_update_done;
    before_drain = inventory_status;
    inventory_status = RF_INVENTORY_DRAINING;
    start_drain_timer();
    waiting_live = true;
    update_pending = true;
    update_sent = false;
    upload_timer_started = false;
    move_status = (rf_move_status_t){.state = RF_MOVE_DRAINING, .line = line,
        .from_phase = from_phase, .to_phase = to_phase};
    return true;
}

static bool persist_move_target(void)
{
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    const uint16_t previous = journal->reserved;
    if (!rf_store_stage_begin())
    {
        return false;
    }
    rf_feeder_t *candidate = rf_store_get_mutable((feeder_id_t)move_status.line);
    if (candidate == NULL)
    {
        rf_store_stage_abort();
        return false;
    }
    (void)memcpy(move_member(candidate, move_status.to_phase), pending_update.eui64, 8U);
    (void)memset(move_member(candidate, move_status.from_phase), 0, 8U);
    journal->reserved = move_marker();
    const int result = rf_store_sync();
    if (result != 0)
    {
        if (result < 0)
        {
            journal->reserved = previous;
        }
        move_status.state = RF_MOVE_STORAGE_ERROR;
        update_pending = false;
        update_done = NULL;
        inventory_status = before_drain;
        rf_events_finish_drain();
        return false;
    }
    move_status.state = RF_MOVE_ASSIGNING;
    return true;
}

static void on_move_update_done(scp_cmd_result_t result, const scp_packet_t *response)
{
    (void)response;
    move_status.state = (result == SCP_CMD_OK) ? RF_MOVE_EPOCH : RF_MOVE_INTERRUPTED;
}

static void on_move_epoch_done(scp_cmd_result_t result, const scp_packet_t *response)
{
    (void)response;
    move_status.state = (result == SCP_CMD_OK) ? RF_MOVE_WAIT_EVENTS : RF_MOVE_INTERRUPTED;
}

static void complete_move(void)
{
    rf_phase_data_t data;
    const uint8_t source = (uint8_t)((pending_update.feeder << 2U) | pending_update.phase);
    if (!rf_inventory_is_loaded() || !rf_inventory_matches_config() ||
        ((uint32_t)(HAL_GetTick() - move_epoch_ms) < 90000U) ||
        !rf_get_source_data(source, HAL_GetTick(), &data) || !data.is_online ||
        (memcmp(data.eui64, pending_update.eui64, 8U) != 0) || nvram_is_busy())
    {
        return;
    }
    rf_inventory_journal_t *journal = nvram_get_rf_inventory_journal();
    const uint16_t marker = journal->reserved;
    if (marker != move_marker())
    {
        move_status.state = RF_MOVE_INTERRUPTED;
        return;
    }
    journal->reserved = 0U;
    if (nvram_save(false) != NVRAM_SAVE_COMPLETE)
    {
        journal->reserved = marker;
        move_status.state = RF_MOVE_SAVE_FAILED;
        return;
    }
    move_status.state = RF_MOVE_COMPLETE;
}

bool rf_inventory_move_finish(void)
{
    if ((move_status.state != RF_MOVE_WAIT_LIVE) && (move_status.state != RF_MOVE_SAVE_FAILED))
    {
        return false;
    }
    move_status.state = RF_MOVE_WAIT_LIVE;
    complete_move();
    return move_status.state == RF_MOVE_COMPLETE;
}

static void continue_move(void)
{
    switch (move_status.state)
    {
        case RF_MOVE_EPOCH:
            if (scp_is_free())
            {
                (void)rf_inventory_refresh_epoch(pending_update.feeder, on_move_epoch_done);
            }
            break;
        case RF_MOVE_WAIT_EVENTS:
        {
            const epoch_monitor_t *monitor = &epoch_monitors[pending_update.feeder - 1U];
            if (monitor->status.uncertain)
            {
                move_status.state = RF_MOVE_INTERRUPTED;
            }
            else if (monitor->status.has_report &&
                ((monitor->status.received_phases & (1U << move_status.to_phase)) != 0U))
            {
                move_epoch_ms = epoch_started_ms[pending_update.feeder - 1U];
                move_status.state = RF_MOVE_RESET;
            }
            else
            {
                /* The existing monitor owns the single extra EPOCH budget. */
            }
            break;
        }
        case RF_MOVE_RESET:
            move_status.state = RF_MOVE_WAIT_LIVE;
            if (!rf_comm_reset_hub())
            {
                move_status.state = RF_MOVE_RESET;
            }
            break;
        case RF_MOVE_WAIT_LIVE:
            complete_move();
            break;
        default:
            break;
    }
}

/*** end of file ***/
