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
#include "rf_discovery.h"
#include "rf_config.h"
#include "rf_comm.h"
#include "rf_inventory.h"
#include "rf_log.h"
#include "rf.h"
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
    SHELL_LOG("Kesif kuyrugu: %u cihaz\r\n",
             (unsigned)rf_discovery_get_count());

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
    if ((1 != argc) || (1U != rf_comm_get_hub_major()))
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

    if (!rf_inventory_refresh_epoch(feeder, on_epoch_done))
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
        SHELL_LOG("RF log: NORMAL (hata/uyari)\r\n");
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

static bool parse_source_args(int argc, char **argv, uint8_t *source)
{
    if ((3 != argc) || (NULL == argv) || (NULL == argv[1]) ||
        (NULL == argv[2]) || ('1' > argv[1][0]) || ('4' < argv[1][0]) ||
        ('\0' != argv[1][1]) || ('1' > argv[2][0]) || ('3' < argv[2][0]) ||
        ('\0' != argv[2][1]))
    {
        return false;
    }
    uint8_t feeder = (uint8_t)(argv[1][0] - '0');
    uint8_t phase = (uint8_t)(argv[2][0] - '0');

    *source = (uint8_t)(((uint32_t)feeder << 2U) | (uint32_t)phase);
    return true;
}

static int rf_shell_live(int argc, char **argv)
{
    uint8_t source;
    rf_phase_data_t data;

    if (!parse_source_args(argc, argv, &source))
    {
        SHELL_LOG("Kullanim: rf live <fider 1..4> <faz 1..3>\r\n");
        return -1;
    }
    if (!rf_get_source_data(source, HAL_GetTick(), &data))
    {
        SHELL_LOG("ACK verilmis envanter/canli veri yok\r\n");
        return -1;
    }
    SHELL_LOG("RF %s, has_live=%u, trip_failed=%u, latched=%u\r\n",
              data.is_online ? "VAR" : "YOK", (unsigned)data.has_live,
              (unsigned)data.trip_failed, (unsigned)data.trip_failure_latched);
    if (data.has_live)
    {
        SHELL_LOG("seq=%lu uptime=%lu RSSI=%ld dBm temp=%ld C\r\n",
                  (unsigned long)data.live.seq,
                  (unsigned long)data.live.uptime_sec,
                  (long)data.live.rssi, (long)data.live.temperature);
        if (data.current_valid)
        {
            SHELL_LOG("Irms=%.3f A\r\n", (double)data.live.current_amps);
        }
        else
        {
            SHELL_LOG("Irms gecersiz\r\n");
        }
    }
    return 0;
}

static uint8_t requested_group;

static void on_config_status_done(scp_cmd_result_t result,
                                  const scp_packet_t *packet)
{
    rf_scp_message_t message;

    if ((SCP_CMD_OK == result) && (NULL != packet) &&
        (RF_CMD_OK == rf_scp_decode_message(packet, &message)))
    {
        if (requested_group != message.body.config.group_id)
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
                  "wait and current operation\r\n");
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
                 "Usage: rf <disc|status|inv|time|epoch|live|"
                 "cfg-status|cfg-apply|cfg-state|cfg-abort|log>\r\n"
                 "  disc   : kesif kuyruguna sanal cihaz ekle\r\n"
                 "  status : hub ve link durumu\r\n"
                 "  inv    : envanteri yeniden push et\r\n"
                 "  time   : saati esitle (envanteri yeniden yuklemez)\r\n"
                 "  epoch N: MH degisimi sonrasi fider 1..4 epoch yenile\r\n"
                 "  live F P: fider/faz canli veri ve alarm\r\n"
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
                "\trf inv                  - envanter push baslat\r\n"
                "\trf time                 - saat esitleme iste\r\n"
                "\trf epoch <1..4>         - epoch yenile (MH degisimi)\r\n"
                "\trf live <fider> <faz>   - canli veri ve alarm\r\n"
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
