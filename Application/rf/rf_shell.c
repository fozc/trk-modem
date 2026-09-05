/*
 * rf_shell.c
 *
 *  Created on: Aug 27, 2026
 *      Author: fatih
 *
 * RF hub shell komutlari.
 *
 * rf disc    - Kesif kuyruguna bir sanal cihaz EUI-64'su ekler.
 *              Gercek hub'da test icin: keşif bildirimi simule eder.
 * rf status  - Hub durumu ve link bilgisi.
 * rf inv     - Envanter push'u yeniden tetikler.
 */

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
        xfprintf(shell_putchr, "Kesif kuyruguna eklendi: %s (#%u)\r\n",
                 hex, (unsigned)virtual_counter);
    }
    else
    {
        xfprintf(shell_putchr,
                 "Eklenemedi (kuyruk dolu veya bu EUI zaten var)\r\n");
    }

    return 0;
}

static int rf_shell_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    xfprintf(shell_putchr, "RF Link: %s\r\n",
             scp_is_free() ? "BOSTA" : "MESGUL");
    xfprintf(shell_putchr, "Envanter: %s / %s\r\n",
             rf_inventory_is_loaded() ? "YUKLU" : "YUKLENMEDI",
             rf_inventory_is_active() ? "CALISIYOR" : "BEKLIYOR");
    xfprintf(shell_putchr, "Kesif kuyrugu: %u cihaz\r\n",
             (unsigned)rf_discovery_get_count());

    return 0;
}

static int rf_shell_inv(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (rf_inventory_is_active())
    {
        xfprintf(shell_putchr, "Envanter push zaten calisiyor\r\n");
    }
    else
    {
        rf_inventory_start();
        xfprintf(shell_putchr, "Envanter push baslatildi\r\n");
    }

    return 0;
}

/* ======================================================================
 * Log seviyesi alt komutu (gsm log ile ayni model)
 * ====================================================================== */

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

static int rf_shell_command(int argc, char *argv[])
{
    if (argc < 2)
    {
        xfprintf(shell_putchr,
                 "Usage: rf <disc|status|inv|log>\r\n"
                 "  disc   : kesif kuyruguna sanal cihaz ekle\r\n"
                 "  status : hub ve link durumu\r\n"
                 "  inv    : envanteri yeniden push et\r\n"
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

    xfprintf(shell_putchr, "Bilinmeyen alt komut: %s\r\n", argv[1]);
    return -1;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void rf_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd = "rf",
        .desc = "RF hub islemleri (disc/status/inv/log)",
        .level = SHELL_LVL_USER,
        .func = rf_shell_command
    });
}

/*** end of file ***/
