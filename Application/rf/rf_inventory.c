/*
 * rf_inventory.c
 *
 *  Created on: Aug 26, 2026
 *      Author: fatih
 *
 * Envanter push siralayici (iterator paterni).
 *
 * Akis:
 *   rf_inventory_start()
 *     -> index'i (0,0)'a koy, send_next()
 *     -> gecerli cihaz bul -> scp_send_command(0x04, 12B)
 *     -> ACK -> index ilerlet -> send_next()  (dongu)
 *     -> cihaz kalmadi -> scp_send_command(0x05, END)
 *     -> ACK -> loaded = true
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

#include "rf_inventory.h"
#include "rf_comm.h"
#include "rf_config.h"
#include "rf_scp.h"
#include "console_logger.h"
#include <string.h>

/* ======================================================================
 * Configuration
 * ====================================================================== */

/** INVENTORY_SET/END: yazma sinifi, uzun timeout (R1 zamanlama tablosu). */
#define INV_TIMEOUT_MS     1000U
#define INV_RETRIES        2U

/* ======================================================================
 * Module state (iterator index)
 * ====================================================================== */

static uint8_t  feeder_index;      /* 0..MAX_POWER_LINE_COUNT-1 */
static uint8_t  phase_index;       /* 0=R, 1=S, 2=T             */
static bool     inv_active;
static bool     inv_loaded;
static uint8_t  inv_ok_count;
static uint8_t  inv_skip_count;

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
    body[11] = 0U;   /* TODO: channel kaynagi BOLATeX'e sorulacak */

    return true;
}

/* ======================================================================
 * Command callbacks
 * ====================================================================== */

/** 0x04 INVENTORY_SET bittiginde cagrilir. */
static void on_set_done(scp_cmd_result_t result, const scp_packet_t *rsp)
{
    (void)rsp;

    if (SCP_CMD_OK == result)
    {
        inv_ok_count++;
    }
    else
    {
        inv_skip_count++;
        CSLOG_WARN("[INV] fider=%u faz=%u atlandi (hata)\r\n",
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

    if (SCP_CMD_OK == result)
    {
        inv_loaded = true;
        CSLOG("[INV] envanter yuklendi: %u cihaz gonderildi, %u atlandi\r\n",
              (unsigned)inv_ok_count, (unsigned)inv_skip_count);
    }
    else
    {
        CSLOG_WARN("[INV] INVENTORY_END basarisiz - hub envanteri yuklu "
                   "sayilmaz\r\n");
    }

    inv_active = false;
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

    /* Bos slotlari atla, ilk gecerli cihazi bul */
    while (!build_body_at_index(body))
    {
        index_advance();

        if (feeder_index >= MAX_POWER_LINE_COUNT)
        {
            /* Tum slotlar tarandi -> END gonder */
            if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_END,
                                 NULL, 0,
                                 INV_TIMEOUT_MS, INV_RETRIES,
                                 on_end_done))
            {
                CSLOG("[INV] %u cihaz tarandi, END gonderiliyor\r\n",
                      (unsigned)(inv_ok_count + inv_skip_count));
            }
            else
            {
                CSLOG_WARN("[INV] END gonderilemedi (mesgul)\r\n");
                inv_active = false;
            }
            return;
        }
    }

    /* Gecerli cihaz bulundu -> 0x04 gonder */
    if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_SET,
                         body, RF_SCP_INV_SET_BODY_LEN,
                         INV_TIMEOUT_MS, INV_RETRIES,
                         on_set_done))
    {
        CSLOG("[INV] fider=%u faz=%u gonderiliyor (EUI=%02X..%02X)\r\n",
              (unsigned)(feeder_index + 1U),
              (unsigned)(phase_index + 1U),
              body[3], body[10]);
    }
    else
    {
        /* Komut mekanizmasi mesgul - index bu cihazda kalir,
         * rf_inventory_continue() tekrar deneyecek. */
        CSLOG("[INV] komut mesgul, bekleniyor\r\n");
    }
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void rf_inventory_start(void)
{
    if (inv_active)
    {
        return;   /* zaten calisiyor */
    }

    inv_active    = true;
    inv_loaded    = false;
    feeder_index = 0U;
    phase_index  = 0U;
    inv_ok_count  = 0U;
    inv_skip_count = 0U;

    CSLOG("[INV] envanter push basliyor\r\n");
    send_next();
}

bool rf_inventory_is_loaded(void)
{
    return inv_loaded;
}

bool rf_inventory_is_active(void)
{
    return inv_active;
}

void rf_inventory_continue(void)
{
    if (inv_active && scp_is_free())
    {
        send_next();
    }
}

/*** end of file ***/
