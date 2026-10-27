/*
 * rf_shell.c
 *
 *  Created on: Aug 27, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * RF hub shell komutlari.
 *
 * rf disc    - Kesif kuyruguna bir sanal cihaz EUI-64'su ekler.
 *              Gercek hub'da test icin: kesif bildirimi simule eder.
 * rf status  - Hub durumu ve link bilgisi.
 * rf inv     - Envanter push'u yeniden tetikler.
 * rf time    - Request a time refresh without reloading inventory.
 * rf epoch N - Queue epoch refresh for feeder N after hub replacement.
 * rf cfg-status N - Read the hub configuration group status.
 */

#define CSLOG_MODULE LOG_MOD_RF
#include "rf_shell.h"
#include "shell.h"
#include "xprintf.h"
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "rf_discovery.h"
#include "rf_config.h"
#include "rf_comm.h"
#include "rf_inventory.h"
#include "rf_log.h"
#include "rf.h"
#include "rf_events.h"
#include "rf_group.h"
#include "rf_apply.h"
#include "rf_hil_transport.h"
#include "boot.h"
#include "stm32u3xx_hal.h"

#ifndef DISABLE_SHELL_LOG
static const char *inventory_status_name(rf_inventory_status_t status)
{
    switch (status)
    {
        case RF_INVENTORY_IDLE: return "BEKLIYOR";
        case RF_INVENTORY_LOADING: return "YUKLENIYOR";
        case RF_INVENTORY_EMPTY: return "BOS";
        case RF_INVENTORY_PARTIAL: return "KISMI";
        case RF_INVENTORY_READY: return "YUKLU";
        case RF_INVENTORY_ERROR: return "HATA";
        case RF_INVENTORY_DRAINING: return "KAYITLAR_CEKILIYOR";
        default: return "BILINMIYOR";
    }
}

static const char *config_status_name(uint8_t state)
{
    switch (state)
    {
        case 0U: return "IDLE";
        case 1U: return "STAGED";
        case 2U: return "DELIVERED";
        case 3U: return "APPLIED";
        case 4U: return "FAILED";
        default: return "UNKNOWN";
    }
}

static const char *group_state_name(rf_group_state_t state)
{
    switch (state)
    {
        case RF_GROUP_IDLE: return "IDLE";
        case RF_GROUP_CHECKING: return "CHECKING_ID";
        case RF_GROUP_WRITING: return "WRITING";
        case RF_GROUP_COMMITTING: return "COMMITTING";
        case RF_GROUP_WAITING: return "WAITING";
        case RF_GROUP_APPLIED: return "APPLIED";
        case RF_GROUP_FAILED: return "FAILED";
        case RF_GROUP_UNCERTAIN: return "UNCERTAIN";
        case RF_GROUP_ID_IN_USE: return "ID_IN_USE";
        case RF_GROUP_MISMATCH: return "REPORT_MISMATCH";
        case RF_GROUP_ABORTING: return "ABORTING";
        case RF_GROUP_CANCELLED: return "CANCELLED";
        case RF_GROUP_RESTARTED: return "MH_RESTARTED";
        default: return "UNKNOWN";
    }
}

#endif

/* ======================================================================
 * Sanal cihaz uretici
 * ====================================================================== */

/**
 * @brief Kesif kuyruguna eklenecek sanal EUI-64 uret.
 *
 * Format: AA:BB:CC:DD:EE:FF:hi:lo  (hi:lo = artan sayac)
 * AA oneki sanal cihazi gerceklerden ayirir.
 */
static uint16_t virtual_counter = 0;

static void generate_virtual_eui(uint8_t eui[8])
{
    virtual_counter++;

    eui[0] = 0xAAU;
    eui[1] = 0xBBU;
    eui[2] = 0xCCU;
    eui[3] = 0xDDU;
    eui[4] = 0xEEU;
    eui[5] = 0xFFU;
    eui[6] = (uint8_t)((virtual_counter >> 8) & 0xFFU);
    eui[7] = (uint8_t)(virtual_counter & 0xFFU);
}

/* ======================================================================
 * Alt komut isleyicileri
 * ====================================================================== */

static int rf_shell_disc(int argc, char **argv)
{
    uint8_t eui[8];
    char hex[RF_EUI64_HEX_LEN];

    (void)argc;
    (void)argv;

    generate_virtual_eui(eui);

    if (rf_discovery_add(eui))
    {
        rf_eui64_to_hex(eui, hex);
        SHELL_LOG( "Kesif kuyruguna eklendi: %s (#%u)\r\n",
                 hex, (unsigned)virtual_counter);
    }
    else
    {
        SHELL_LOG(
                 "Eklenemedi (kuyruk dolu veya bu EUI zaten var)\r\n");
    }

    return 0;
}

static int rf_shell_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#ifndef DISABLE_SHELL_LOG
    const fw_info_t *image = boot_get_installed_fw_info();
    SHELL_LOG("RTU image: crc=0x%08lX size=%lu git=%s profile=%u\r\n",
              (unsigned long)image->fw_crc, (unsigned long)image->size,
              (const char *)image->short_commit_hash,
              (unsigned)RF_SCP_OVER_MODBUS_PORT);
#endif
    SHELL_LOG("RF Link: %s\r\n",
             scp_is_free() ? "BOSTA" : "MESGUL");
    SHELL_LOG("MH SCP major: %u\r\n",
              (unsigned)rf_comm_get_hub_major());
    SHELL_LOG("Envanter: %s\r\n",
              inventory_status_name(rf_inventory_get_status()));
    if (rf_events_storage_degraded())
    {
        SHELL_LOG("MH event storage degraded: inventory allowed, event retrieval unavailable; contact Bolatex.\r\n");
    }
    rf_inventory_wait_t wait;
    rf_inventory_get_wait(&wait);
    SHELL_LOG("Inventory wait: %s total_ms=%u reason_ms=%u\r\n",
              wait.reason, (unsigned)wait.total_ms, (unsigned)wait.reason_ms);
    SHELL_LOG("Kesif kuyrugu: %u cihaz\r\n",
             (unsigned)rf_discovery_get_count());
    for (uint8_t feeder = 1U; feeder <= 4U; feeder++)
    {
        rf_epoch_status_t epoch;
        if (rf_inventory_get_epoch_status(feeder, &epoch) && (epoch.expected_phases != 0U))
        {
            SHELL_LOG("EPOCH fider=%u active=%u 201=%u mask=0x%02X 120=0x%02X "
                      "checked=0x%02X missing=0x%02X extra=%u uncertain=%u (diagnostic only)\r\n",
                      (unsigned)feeder, (unsigned)epoch.active, (unsigned)epoch.has_report,
                      (unsigned)epoch.report_phases, (unsigned)epoch.received_phases,
                      (unsigned)epoch.checked_phases, (unsigned)epoch.missing_phases,
                      (unsigned)epoch.retry_used, (unsigned)epoch.uncertain);
        }
    }

    return 0;
}

static int rf_shell_inv(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (rf_inventory_is_active())
    {
        SHELL_LOG("Envanter push zaten calisiyor\r\n");
    }
    else
    {
        if (!rf_inventory_start())
        {
            SHELL_LOG("Envanter baslamadi: BOOT korumasi veya aktif "
                      "ayar islemi tamamlanmali\r\n");
            return -1;
        }
        SHELL_LOG("Envanter: %s\r\n",
                  inventory_status_name(rf_inventory_get_status()));
    }

    return 0;
}

static int rf_shell_time(int argc, char **argv)
{
    (void)argv;
    if ((1 != argc) || (RF_SCP_MAJOR_EXPECTED != rf_comm_get_hub_major()))
    {
        SHELL_LOG("Kullanim: rf time (uyumlu BOOT gerekli)\r\n");
        return -1;
    }
    rf_comm_sync_time();
    SHELL_LOG("Saat esitleme bekliyor; gecerli RTC ile gonderilecek\r\n");
    return 0;
}

static void on_epoch_done(scp_cmd_result_t result, const scp_packet_t *response)
{
    if (SCP_CMD_OK == result)
    {
        SHELL_LOG("Epoch ACK: broadcast kuyruga alindi. "
                  "Yapilandirma ve sonraki epoch icin en az 90 s bekleyin\r\n");
    }
    else if ((SCP_CMD_ERR == result) && (NULL != response))
    {
        SHELL_LOG("Epoch ERROR: 0x%02X\r\n", response->data[0]);
    }
    else
    {
        SHELL_LOG("Epoch tamamlanmadi (timeout veya MH yeniden basladi)\r\n");
    }
}

static int rf_shell_epoch(int argc, char **argv)
{
    if ((2 != argc) || (NULL == argv) || (NULL == argv[1]) ||
        ('1' > argv[1][0]) || ('4' < argv[1][0]) || ('\0' != argv[1][1]))
    {
        SHELL_LOG("Kullanim: rf epoch <1..4>\r\n");
        return -1;
    }
    uint8_t feeder = (uint8_t)(argv[1][0] - '0');

    if (!rf_inventory_hub_replaced(feeder, on_epoch_done))
    {
        SHELL_LOG("Epoch gonderilmedi: "
                  "envanter yuklu olmali, hat bos olmali\r\n");
        return -1;
    }
    SHELL_LOG("Epoch istegi gonderildi; MH ACK bekleniyor\r\n");
    return 0;
}

/* ======================================================================
 * Log seviyesi alt komutu (gsm log ile ayni model)
 * ====================================================================== */

#ifndef DISABLE_SHELL_LOG
static const char *rf_log_level_name(rf_log_level_t lvl)
{
    switch (lvl)
    {
        case RF_LOG_OFF:     return "OFF";
        case RF_LOG_NORMAL:  return "NORMAL";
        case RF_LOG_VERBOSE: return "VERBOSE";
        default:             return "?";
    }
}

#endif

static int rf_shell_log(int argc, char **argv)
{
    if (argc < 2)
    {
        SHELL_LOG("RF log level: %s (%u)\r\n",
                  rf_log_level_name(rf_log_get_level()),
                  (unsigned)rf_log_get_level());
        SHELL_LOG("Usage: rf log <off|on|verbose>\r\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "off"))
    {
        rf_log_set_level(RF_LOG_OFF);
        SHELL_LOG("RF log: OFF\r\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "on"))
    {
        rf_log_set_level(RF_LOG_NORMAL);
        SHELL_LOG("RF log: NORMAL (akis/hata/uyari)\r\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "verbose"))
    {
        rf_log_set_level(RF_LOG_VERBOSE);
        SHELL_LOG("RF log: VERBOSE (tum iz + ham paket dokumu)\r\n");
        return 0;
    }

    SHELL_LOG("Bilinmeyen seviye '%s'. Usage: off | on | verbose\r\n", argv[1]);
    return -1;
}

/* ======================================================================
 * Ana komut dispatch
 * ====================================================================== */

static bool parse_live_args(int argc, char **argv, uint8_t *source)
{
    if (((2 != argc) && (3 != argc)) || (NULL == argv) ||
        (NULL == argv[1]) || ('1' > argv[1][0]) || ('4' < argv[1][0]) ||
        ('\0' != argv[1][1]))
    {
        return false;
    }
    if ((3 == argc) && ((NULL == argv[2]) || ('1' > argv[2][0]) ||
        ('3' < argv[2][0]) || ('\0' != argv[2][1])))
    {
        return false;
    }
    uint8_t feeder = (uint8_t)(argv[1][0] - '0');
    uint8_t phase = (3 == argc) ? (uint8_t)(argv[2][0] - '0') : 1U;

    *source = (uint8_t)(((uint32_t)feeder << 2U) | (uint32_t)phase);
    return true;
}

#ifndef DISABLE_SHELL_LOG
static void print_live_measurement(const char *label, float value,
                                    const char *unit, bool is_valid)
{
    if (isfinite(value))
    {
        SHELL_LOG("  %-18s : %.3f %s [%s]\r\n", label, (double)value,
                  unit, is_valid ? "GECERLI" : "GECERSIZ");
    }
    else
    {
        SHELL_LOG("  %-18s : GECERSIZ (NaN/Inf)\r\n", label);
    }
}

static void print_live_payload(const rf_phase_data_t *data)
{
    const rf_scp_live_t *live = &data->live;

    SHELL_LOG("  -- LIVE_DATA: son alinan paket --\r\n");
    SHELL_LOG("  Source             : 0x%02X\r\n", (unsigned)live->source);
    SHELL_LOG("  Sequence           : %lu\r\n", (unsigned long)live->seq);
    SHELL_LOG("  Uptime             : %lu s\r\n",
              (unsigned long)live->uptime_sec);
    SHELL_LOG("  Boot counter       : %u\r\n", (unsigned)live->boot_counter);
    SHELL_LOG("  State              : %u\r\n", (unsigned)live->state);
    SHELL_LOG("  Fault count        : %u\r\n", (unsigned)live->fault_count);
    print_live_measurement("Irms", live->current_amps, "A",
                           data->current_valid);
    print_live_measurement("VTrip", live->trip_voltage, "V",
                           data->trip_voltage_valid);
    print_live_measurement("VHarvest", live->harvest_voltage, "V",
                           data->harvest_voltage_valid);
    SHELL_LOG("  MCU temperature    : %ld C\r\n", (long)live->temperature);
    SHELL_LOG("  RSSI               : %ld dBm\r\n", (long)live->rssi);
    SHELL_LOG("  Status flags       : 0x%02X\r\n", (unsigned)live->flags);
    SHELL_LOG("  FSM error          : %u\r\n", (unsigned)live->fsm_error);
    SHELL_LOG("  Trip failed (LIVE) : %u\r\n", (unsigned)live->trip_failed);
    SHELL_LOG("  Log pending        : %u\r\n", (unsigned)live->log_pending);
    SHELL_LOG("  Log wrap low       : %u\r\n", (unsigned)live->log_wrap_low);
}
#endif

static bool print_live_phase(uint8_t source, uint32_t now_ms)
{
    rf_phase_data_t data;

    SHELL_LOG("\r\n+--------------------------------------------------+\r\n"
              "  RF LIVE | Fider %u | Faz L%u\r\n"
              "+--------------------------------------------------+\r\n",
              (unsigned)(source >> 2U), (unsigned)(source & 3U));
    if (!rf_get_source_data(source, now_ms, &data))
    {
        SHELL_LOG("  ACK verilmis envanter/canli veri yok\r\n");
        return false;
    }
#ifndef DISABLE_SHELL_LOG
    char eui64[RF_EUI64_HEX_LEN];
    rf_eui64_to_hex(data.eui64, eui64);
    SHELL_LOG("  EUI-64             : %s\r\n", eui64);
#endif
    SHELL_LOG("  RF baglantisi      : %s\r\n",
              data.is_online ? "ONLINE" : "OFFLINE / TAZE LIVE YOK");
    SHELL_LOG("  Acma alarmi        : %u (latched=%u)\r\n",
              (unsigned)data.trip_failed, (unsigned)data.trip_failure_latched);
    if (!data.has_live)
    {
        SHELL_LOG("  LIVE_DATA          : Henuz alinmadi\r\n");
        return true;
    }
    SHELL_LOG("  Son LIVE yasi      : %lu ms\r\n",
              (unsigned long)(now_ms - data.last_live_ms));
    SHELL_LOG("  Uptime stalled     : %u\r\n", (unsigned)data.uptime_stalled);
#ifndef DISABLE_SHELL_LOG
    print_live_payload(&data);
#endif
    return true;
}

static int rf_shell_live(int argc, char **argv)
{
    uint8_t source;
    const bool all_phases = (2 == argc);

    if (!parse_live_args(argc, argv, &source))
    {
        SHELL_LOG("Kullanim: rf live <fider 1..4> [faz 1..3]\r\n");
        return -1;
    }
    const uint32_t now_ms = HAL_GetTick();
    bool has_data = false;
    const uint8_t phase_count = all_phases ? 3U : 1U;
    for (uint8_t index = 0U; index < phase_count; index++)
    {
        if (print_live_phase((uint8_t)(source + index), now_ms))
        {
            has_data = true;
        }
    }
    return has_data ? 0 : -1;
}

static uint8_t requested_group;

static void on_config_status_done(scp_cmd_result_t result,
                                  const scp_packet_t *packet)
{
    rf_scp_message_t message;

    if ((SCP_CMD_OK == result) && (NULL != packet) &&
        (RF_CMD_OK == rf_scp_decode_message(packet, &message)))
    {
        if ((requested_group != 0U) && (requested_group != message.body.config.group_id))
        {
            SHELL_LOG("CFG_STATUS: unexpected group ID\r\n");
            return;
        }
        SHELL_LOG("MH group=%u state=%s members=0x%02X "
                  "reason=%u cfg_crc=0x%04X attempts=%u\r\n",
                  (unsigned)message.body.config.group_id,
                  config_status_name(message.body.config.state),
                  (unsigned)message.body.config.member_bitmap,
                  (unsigned)message.body.config.reason,
                  (unsigned)message.body.config.config_crc,
                  (unsigned)message.body.config.attempts);
        (void)rf_group_handle_status(&message);
    }
    else if ((SCP_CMD_ERR == result) && (NULL != packet))
    {
        SHELL_LOG("CFG_STATUS ERROR: 0x%02X\r\n", packet->data[0]);
    }
    else
    {
        SHELL_LOG("CFG_STATUS unavailable (timeout or MH restart)\r\n");
    }
}

static bool parse_group_id(const char *text, uint8_t *group)
{
    uint16_t value = 0U;

    if ((NULL == text) || ('\0' == text[0]))
    {
        return false;
    }
    for (size_t index = 0U; '\0' != text[index]; index++)
    {
        if ((3U <= index) || ('0' > text[index]) || ('9' < text[index]))
        {
            return false;
        }
        value = (uint16_t)((value * 10U) + (uint8_t)(text[index] - '0'));
        if (UINT8_MAX < value)
        {
            return false;
        }
    }
    *group = (uint8_t)value;
    return true;
}

static int rf_shell_move(int argc, char **argv)
{
    if ((argc == 1) || ((argc == 2) && (strcmp(argv[1], "status") == 0)))
    {
#ifndef DISABLE_SHELL_LOG
        static const char *const names[] = {"idle", "draining", "assigning", "epoch", "waiting-201-120",
            "reset", "waiting-new-live", "complete", "interrupted", "storage-error", "final-save-failed"};
        rf_move_status_t status;
        rf_inventory_move_status(&status);
        const size_t state = (size_t)status.state;
        SHELL_LOG("Service move: %s line=%u L%u->L%u pending=%u\r\n",
                  (state < sizeof(names) / sizeof(names[0])) ? names[state] : "unknown",
                  (unsigned)(status.line + 1U), (unsigned)(status.from_phase + 1U),
                  (unsigned)(status.to_phase + 1U), (unsigned)rf_inventory_service_pending());
        if ((status.state == RF_MOVE_INTERRUPTED) || (status.state == RF_MOVE_STORAGE_ERROR))
        {
            SHELL_LOG("Maintenance required; config remains blocked while the durable marker is set.\r\n"
                      "No automatic EPOCH retry or unverified service-marker clear.\r\n");
        }
        if (status.state == RF_MOVE_SAVE_FAILED)
        {
            SHELL_LOG("Final save failed; admin may retry rf move finish while this RTU session retains proof.\r\n");
        }
#endif
        return 0;
    }
    if (shell_get_session_level() != SHELL_LVL_SUPER_USER)
    {
        SHELL_LOG("Admin role required for service relocation\r\n");
        return -1;
    }
    if ((argc == 2) && (strcmp(argv[1], "finish") == 0))
    {
        const bool finished = rf_inventory_move_finish();
        SHELL_LOG("Service move finish: %s; inspect rf move status\r\n", finished ? "verified" : "not completed");
        return finished ? 0 : -1;
    }
    uint8_t line;
    uint8_t from;
    uint8_t to;
    if ((argc != 5) || !parse_group_id(argv[1], &line) || (line < 1U) ||
        !parse_group_id(argv[2], &from) || (from < 1U) || (from > 3U) ||
        !parse_group_id(argv[3], &to) || (to < 1U) || (to > 3U) ||
        (strcmp(argv[4], "confirm-unused") != 0))
    {
        SHELL_LOG("Usage: rf move <line 1..7> <from 1..3> <to 1..3> confirm-unused\r\n"
                  "Attest that the empty target has never been used; same feeder only.\r\n");
        return -1;
    }
    if (!rf_inventory_move_begin((uint8_t)(line - 1U), (uint8_t)(from - 1U), (uint8_t)(to - 1U), true))
    {
        SHELL_LOG("Move not started; check accepted inventory, empty target and active operations\r\n");
        return -1;
    }
    SHELL_LOG("Service move queued; inspect rf move status. Configuration is blocked until verified completion.\r\n");
    return 0;
}

static int rf_shell_config_status(int argc, char **argv)
{
    uint8_t group;
    scp_packet_t request =
    {
        .type = SCP_TYPE_GET, .cmd = RF_SCP_CMD_CFG_STATUS_GET,
        .data_len = 1U
    };

    if ((2 != argc) || (NULL == argv) || !parse_group_id(argv[1], &group))
    {
        SHELL_LOG("Usage: rf cfg-status <group_id 0..255>\r\n");
        return -1;
    }
    request.data[0] = group;
    if (!scp_send_request(&request, on_config_status_done))
    {
        SHELL_LOG("CFG_STATUS request not sent; check hub/link state\r\n");
        return -1;
    }
    requested_group = group;
    return 0;
}

static int rf_shell_config_apply(int argc, char **argv)
{
    uint8_t line;
    uint8_t group_id;

    if ((3 != argc) || (NULL == argv) ||
        !parse_group_id(argv[1], &line) ||
        !parse_group_id(argv[2], &group_id) ||
        (0U == line) || (MAX_POWER_LINE_COUNT < line))
    {
        SHELL_LOG("Usage: rf cfg-apply <line 1..7> <fresh group_id>\r\n");
        return -1;
    }
    if (rf_apply_is_running() ||
        !rf_group_start((size_t)line - 1U, group_id))
    {
        SHELL_LOG("Config not started: check members, inventory, epoch "
                  "wait and current operation\r\n"
                  "Uncertain WRITE before COMMIT: retry unchanged line "
                  "with the same group ID\r\n");
        return -1;
    }
    SHELL_LOG("Config queued; use rf cfg-state for the verified result\r\n");
    return 0;
}

static int rf_shell_config_state(int argc, char **argv)
{
    rf_group_status_t status;

    (void)argv;
    if ((1 != argc) || !rf_group_get_status(&status))
    {
        return -1;
    }
    SHELL_LOG("RTU group=%u feeder=%u state=%s writes=%u "
              "expected_crc=0x%04X\r\n",
              (unsigned)status.group_id, (unsigned)status.feeder,
              group_state_name(status.state), (unsigned)status.writes_acked,
              (unsigned)status.expected_crc);
    if ((RF_GROUP_IDLE == status.state) || (status.line == 0U))
    {
        SHELL_LOG("Applied AY settings: unknown; no retained APPLIED evidence\r\n");
    }
    if (status.has_report)
    {
        SHELL_LOG("MH state=%s bitmap=0x%02X reason=%u cfg_crc=0x%04X\r\n",
                  config_status_name(status.report.state),
                  (unsigned)status.report.member_bitmap,
                  (unsigned)status.report.reason,
                  (unsigned)status.report.config_crc);
    }
    return 0;
}

static int rf_shell_config_abort(int argc, char **argv)
{
    (void)argv;
    if ((1 != argc) || !rf_apply_abort())
    {
        SHELL_LOG("Abort not sent: active COMMIT group must be known\r\n");
        return -1;
    }
    SHELL_LOG("Cancellation requested; inspect rf cfg-state for result\r\n");
    return 0;
}

static int rf_shell_command(int argc, char *argv[])
{
    if (argc < 2)
    {
        SHELL_LOG(
                 "Usage: rf <disc|status|reset|inv|move|time|epoch|live|"
                 "cfg-status|cfg-apply|cfg-state|cfg-abort|log>\r\n"
                 "  disc   : kesif kuyruguna sanal cihaz ekle\r\n"
                 "  status : hub ve link durumu\r\n"
                 "  reset  : MH reset (100 ms); idle transport required\r\n"
                 "  inv    : envanteri yeniden push et\r\n"
                 "  move   : privileged same-feeder service relocation; status is read-only\r\n"
                 "  time   : saati esitle (envanteri yeniden yuklemez)\r\n"
                 "  epoch N: MH degisimi bakimi; ilk TIMEOUT'ta tek "
                 "epoch tekrari\r\n"
                 "  live F [P]: fiderin 3 fazi veya tek faz LIVE paketi\r\n"
                 "  cfg-status N: MH group status (0..255)\r\n"
                 "  cfg-apply L N: apply stored line config with fresh ID\r\n"
                 "  cfg-state: local verified group result\r\n"
                 "  cfg-abort: cancel an active known COMMIT group\r\n"
                 "  log    : log seviyesi (off/on/verbose)\r\n");
        return -1;
    }

    if (0 == strcmp(argv[1], "disc"))
    {
        return rf_shell_disc(argc - 1, &argv[1]);
    }

    if (0 == strcmp(argv[1], "status"))
    {
        return rf_shell_status(argc - 1, &argv[1]);
    }

    if (0 == strcmp(argv[1], "inv"))
    {
        return rf_shell_inv(argc - 1, &argv[1]);
    }
    if (strcmp(argv[1], "move") == 0)
    {
        return rf_shell_move(argc - 1, &argv[1]);
    }

    if (strcmp(argv[1], "reset") == 0)
    {
        if ((argc != 2) || !rf_comm_reset_hub())
        {
            SHELL_LOG("RF reset not started; check arguments, bridge and active operations\r\n");
            return -1;
        }
        SHELL_LOG("MH reset started; waiting for BOOT_NOTIFY and startup recovery\r\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "log"))
    {
        return rf_shell_log(argc - 1, &argv[1]);
    }

    if (0 == strcmp(argv[1], "cfg-status"))
    {
        return rf_shell_config_status(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "cfg-apply"))
    {
        return rf_shell_config_apply(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "cfg-state"))
    {
        return rf_shell_config_state(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "cfg-abort"))
    {
        return rf_shell_config_abort(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "time"))
    {
        return rf_shell_time(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "epoch"))
    {
        return rf_shell_epoch(argc - 1, &argv[1]);
    }
    if (0 == strcmp(argv[1], "live"))
    {
        return rf_shell_live(argc - 1, &argv[1]);
    }


    SHELL_LOG( "Bilinmeyen alt komut: %s\r\n", argv[1]);
    return -1;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void rf_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "rf",
        .desc = "RF hub islemleri\r\n"
                "\trf disc                 - kesif kuyruguna test EUI ekle\r\n"
                "\trf status               - hub ve hat ozet durumu\r\n"
                "\trf reset                - MH reset pulse (100 ms, idle only)\r\n"
                "\trf inv                  - envanter push baslat\r\n"
                "\trf move [status]        - service relocation; admin start requires confirm-unused\r\n"
                "\trf time                 - saat esitleme iste\r\n"
                "\trf epoch <1..4>         - epoch yenile (MH degisimi)\r\n"
                "\trf live <fider> [faz]   - tam LIVE paketi (3 faz/tek faz)\r\n"
                "\trf cfg-status <group_id> - MH config group status\r\n"
                "\trf cfg-apply <line> <group_id> - apply stored config\r\n"
                "\trf cfg-state            - local verified group result\r\n"
                "\trf cfg-abort            - cancel active COMMIT group\r\n"
                "\trf log [off|on|verbose] - RF log seviyesini goster/ayarla",
        .level = SHELL_LVL_USER,
        .func = rf_shell_command
    });
}

/*** end of file ***/
