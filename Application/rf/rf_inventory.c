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
#define TIMEOUT_MS     1000U
#define RETRIES        2U

/* ======================================================================
 * Module state (iterator index)
 * ====================================================================== */

static uint8_t  feeder_index;      /* 0..MAX_POWER_LINE_COUNT-1 */
static uint8_t  phase_index;       /* 0=R, 1=S, 2=T             */
static bool     active;
static bool     loaded;
static uint8_t  sent_count;
static uint8_t  skipped_count;

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

    if (SCP_CMD_OK == result)
    {
        loaded = true;
        CSLOG("[RF-INV] envanter yuklendi: %u cihaz gonderildi, %u atlandi\r\n",
              (unsigned)sent_count, (unsigned)skipped_count);
    }
    else
    {
        CSLOG_WARN("[RF-INV] INVENTORY_END basarisiz - hub envanteri yuklu "
                   "sayilmaz\r\n");
    }

    active = false;
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
                                 TIMEOUT_MS, RETRIES,
                                 on_end_done))
            {
                CSLOG("[RF-INV] %u cihaz tarandi, END gonderiliyor\r\n",
                      (unsigned)(sent_count + skipped_count));
            }
            else
            {
                CSLOG_WARN("[RF-INV] END gonderilemedi (mesgul)\r\n");
                active = false;
            }
            return;
        }
    }

    /* Gecerli cihaz bulundu -> 0x04 gonder */
    if (scp_send_command(SCP_TYPE_SET, RF_SCP_CMD_INVENTORY_SET,
                         body, RF_SCP_INV_SET_BODY_LEN,
                         TIMEOUT_MS, RETRIES,
                         on_set_done))
    {
        CSLOG("[RF-INV] fider=%u faz=%u gonderiliyor (EUI=%02X..%02X)\r\n",
              (unsigned)(feeder_index + 1U),
              (unsigned)(phase_index + 1U),
              body[3], body[10]);
    }
    else
    {
        /* Komut mekanizmasi mesgul - index bu cihazda kalir,
         * rf_inventory_continue() tekrar deneyecek. */
        CSLOG("[RF-INV] komut mesgul, bekleniyor\r\n");
    }
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void rf_inventory_start(void)
{
    if (active)
    {
        return;   /* zaten calisiyor */
    }

    active    = true;
    loaded    = false;
    feeder_index = 0U;
    phase_index  = 0U;
    sent_count  = 0U;
    skipped_count = 0U;

    CSLOG("[RF-INV] envanter push basliyor\r\n");
    send_next();
}

bool rf_inventory_is_loaded(void)
{
    return loaded;
}

bool rf_inventory_is_active(void)
{
    return active;
}

void rf_inventory_continue(void)
{
    if (active && scp_is_free())
    {
        send_next();
    }
}

/*** end of file ***/
