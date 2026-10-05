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
        default: return "BILINMIYOR";
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
        rf_inventory_start();
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
                  "Yapilandirmadan once yaklasik 30 s bekleyin\r\n");
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

static int rf_shell_alarm_ack(int argc, char **argv)
{
    uint8_t source;

    if (!parse_source_args(argc, argv, &source))
    {
        SHELL_LOG("Kullanim: rf alarm-ack <fider 1..4> <faz 1..3>\r\n");
        return -1;
    }
    if (!rf_ack_trip_failure(source))
    {
        SHELL_LOG("Onaylanacak latched alarm yok veya Trip_Failed hala 1\r\n");
        return -1;
    }
    SHELL_LOG("Acma basarisizligi alarmi operator tarafindan onaylandi\r\n");
    return 0;
}

static int rf_shell_command(int argc, char *argv[])
{
    if (argc < 2)
    {
        SHELL_LOG(
                 "Usage: rf <disc|status|inv|time|epoch|live|alarm-ack|log>\r\n"
                 "  disc   : kesif kuyruguna sanal cihaz ekle\r\n"
                 "  status : hub ve link durumu\r\n"
                 "  inv    : envanteri yeniden push et\r\n"
                 "  time   : saati esitle (envanteri yeniden yuklemez)\r\n"
                 "  epoch N: MH degisimi sonrasi fider 1..4 epoch yenile\r\n"
                 "  live F P: fider/faz canli veri ve alarm\r\n"
                 "  alarm-ack F P: latched acma basarisizligi onayi\r\n"
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
    if (0 == strcmp(argv[1], "alarm-ack"))
    {
        return rf_shell_alarm_ack(argc - 1, &argv[1]);
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
                "\trf alarm-ack <fider> <faz> - latched alarmi onayla\r\n"
                "\trf log [off|on|verbose] - RF log seviyesini goster/ayarla",
        .level = SHELL_LVL_USER,
        .func = rf_shell_command
    });
}

/*** end of file ***/
