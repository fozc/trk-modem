/*
 * main.c
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * RF HUB simulatoru - canli UART modu.
 *
 * RTU'nun USART3 hattiyla (230400 8N1) konusur; tum gelen/giden
 * cerceveleri indeks + hex + cozumlenmis icerikle loglar.
 *
 * Kullanim:
 *   rf_hub_sim COM7                  (230400, R0 modu, acilista BOOT)
 *   rf_hub_sim COM7 --r1             (0x40 yaniti 10 B / tail'li)
 *   rf_hub_sim COM7 --baud 115200    (RTU henuz 230400'e gecmediyse)
 *   rf_hub_sim COM7 --noboot         (acilista BOOT_NOTIFY gonderme)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <conio.h>
#include <windows.h>
#include "scp.h"
#include "hub.h"
#include "serial_win.h"

#define RX_CHUNK          64
#define RAW_IDLE_NOTE_MS  150

static scp_t scp_ctx;
static hub_t hub;

/* Ham bayt akisi: cerceve tamamlanana kadar birikir */
static uint8_t  rx_raw[SCP_FRAME_MAX_SIZE * 2];
static size_t   rx_raw_len;
static uint32_t rx_last_ms;

/* Log indeks sayaci: her cerceve (RX ve TX) numaralanir */
static unsigned int log_index = 0;

/* Komut kodlarina insan-okur isim (hub.c ile ayni) */
static const char *cmd_name(uint8_t cmd)
{
    switch (cmd)
    {
        case 0x01: return "GET_STATUS";
        case 0x02: return "GET_FRAM_STATS";
        case 0x03: return "SET_CONFIG(stub)";
        case 0x04: return "INVENTORY_SET";
        case 0x05: return "INVENTORY_END";
        case 0x06: return "INVENTORY_UPDATE";
        case 0x07: return "TIME_SYNC";
        case 0x10: return "TRIP_NOTIFY";
        case 0x11: return "LIVE_DATA";
        case 0x12: return "ANOMALY_REPORT";
        case 0x13: return "BOOT_NOTIFY";
        case 0x14: return "DISCOVERY_REPORT";
        case 0x20: return "CFG_READ_ALL";
        case 0x21: return "CFG_STATUS_NOTIFY";
        case 0x22: return "CFG_WRITE";
        case 0x24: return "CFG_COMMIT";
        case 0x26: return "CFG_ABORT";
        case 0x28: return "CFG_STATUS_GET";
        case 0x2A: return "EPOCH_REFRESH";
        case 0x40: return "LOG_READ_HEAD";
        case 0x42: return "LOG_READ_RECORD";
        case 0x44: return "LOG_READ_RANGE";
        case 0x46: return "LOG_CONSUME_TO";
        case 0x47: return "LOG_AVAILABLE";
        default:   return "?";
    }
}

static const char *type_name(uint8_t type)
{
    switch (type)
    {
        case SCP_TYPE_GET:   return "GET";
        case SCP_TYPE_SET:   return "SET";
        case SCP_TYPE_ACK:   return "ACK";
        case SCP_TYPE_ERROR: return "ERROR";
        case SCP_TYPE_PING:  return "PING";
        default:             return "?";
    }
}

static const char *addr_name(uint8_t addr)
{
    switch (addr)
    {
        case 0x00: return "BCAST";
        case 0x01: return "HUB";
        case 0x02: return "RTU";
        default:   return "?";
    }
}

/* HEX satiri: 16 bayt/satir, iki bosluk aralikli */
static void print_hex(uint8_t indent, const uint8_t *data, size_t len)
{
    size_t i;
    for (i = 0U; i < len; i++)
    {
        if ((i % 16U) == 0U)
        {
            if (i > 0U)
            {
                printf("\n");
            }
            printf("%*s", (int)indent, "");
        }
        printf("%02X ", data[i]);
    }
    printf("\n");
}

/* ======================================================================
 * TX logging: hub'dan RTU'ya giden cerceve
 * ====================================================================== */
static void tx_to_serial(const uint8_t *frame, size_t frame_len)
{
    (void)serial_write(frame, frame_len);
    log_index++;
    printf("#%04u [TX] %s -> %s  %s  %s  seq=%u  (%zu bayt)\n",
           log_index,
           "HUB", addr_name(0x02),
           " ", " ",
           0, frame_len);
    /* Not: detay hub.c icindeki hub_log tarafindan basilir;
     * burada yalniz ham cerceve yazilir. */
    print_hex(6, frame, frame_len);
}

/* TX icin paket seviyesinde ozet (hub.c'nin send callback'inden) */
static void hub_send_packet(const scp_packet_t *pkt, void *user)
{
    (void)user;
    log_index++;
    printf("#%04u [TX] HUB -> %s  %s  %s  seq=%u  len=%u\n",
           log_index,
           addr_name(pkt->dst),
           type_name(pkt->type),
           cmd_name(pkt->cmd),
           pkt->seq,
           (unsigned)pkt->data_len);
    (void)scp_send(&scp_ctx, pkt);
}

/* ======================================================================
 * RX logging: RTU'dan hub'a gelen cerceve
 * ====================================================================== */
static void log_rx_frame(const scp_packet_t *pkt, const uint8_t *raw,
                         size_t raw_len)
{
    const char *direction;

    log_index++;

    /* Yon belirle: kim kime gonderdi? */
    if (pkt->src == 0x02)
    {
        direction = "RTU -> HUB";
    }
    else
    {
        direction = "HUB -> RTU";
    }

    printf("#%04u [RX] %s  %s  %s  seq=%u  len=%u  (%zu bayt telde)\n",
           log_index,
           direction,
           type_name(pkt->type),
           cmd_name(pkt->cmd),
           pkt->seq,
           (unsigned)pkt->data_len,
           raw_len);
    print_hex(6, raw, raw_len);

    /* Govde ozeti: ilk 16 bayti decode et */
    if (pkt->data_len > 0U)
    {
        printf("      govde: ");
        switch (pkt->cmd)
        {
            case 0x01:  /* GET_STATUS istegi — govde yok */
                printf("(govdesiz)\n");
                break;
            case 0x07:  /* TIME_SYNC */
                if (pkt->data_len >= 7U)
                {
                    printf("CP56: %02X%02X %02X %02X %02X %02X %02X "
                           "(ms|min|sa|gn|ay|yl)\n",
                           pkt->data[0], pkt->data[1], pkt->data[2],
                           pkt->data[3], pkt->data[4], pkt->data[5],
                           pkt->data[6]);
                }
                break;
            case 0x04:  /* INVENTORY_SET */
                if (pkt->data_len >= 12U)
                {
                    printf("zone=%u fider=%u phase=%u "
                           "EUI=%02X%02X%02X%02X%02X%02X%02X%02X ch=%u\n",
                           pkt->data[0], pkt->data[1], pkt->data[2],
                           pkt->data[3], pkt->data[4], pkt->data[5],
                           pkt->data[6], pkt->data[7], pkt->data[8],
                           pkt->data[9], pkt->data[10], pkt->data[11]);
                }
                break;
            case 0x46:  /* LOG_CONSUME_TO */
                if (pkt->data_len >= 2U)
                {
                    printf("index=%u (son_okunan+1)\n",
                           (unsigned)(pkt->data[0] |
                                      ((uint16_t)pkt->data[1] << 8)));
                }
                break;
            default:
                printf("%u bayt\n", (unsigned)pkt->data_len);
                break;
        }
    }
    printf("\n");
}

/* ======================================================================
 * Ham bayt / bozuk cerceve notu
 * ====================================================================== */
static void flush_raw_note(uint32_t now)
{
    if ((rx_raw_len > 0U) &&
        ((int32_t)(now - rx_last_ms) >= (int32_t)RAW_IDLE_NOTE_MS))
    {
        log_index++;
        printf("#%04u [??] %zu bayt geldi ama gecerli cerceve cozulemedi\n"
               "      (bozuk CRC/format — gercek cihaz sessizce atardi)\n",
               log_index, rx_raw_len);
        print_hex(6, rx_raw, rx_raw_len);
        printf("\n");
        rx_raw_len = 0U;
    }
}

/* ======================================================================
 * RX byte akisi -> cerceve
 * ====================================================================== */
static void feed_rx_byte(uint8_t b, uint32_t now)
{
    if (rx_raw_len < sizeof(rx_raw))
    {
        rx_raw[rx_raw_len] = b;
        rx_raw_len++;
    }
    rx_last_ms = now;

    scp_process_byte(&scp_ctx, b);

    if (scp_packet_ready(&scp_ctx))
    {
        const scp_packet_t *pkt = scp_get_packet(&scp_ctx);
        if (pkt != NULL)
        {
            log_rx_frame(pkt, rx_raw, rx_raw_len);
            hub_on_packet(&hub, pkt);
        }
        scp_packet_done(&scp_ctx);
        rx_raw_len = 0U;
    }
}

/* ======================================================================
 * Yardim / durum
 * ====================================================================== */
static void print_help(void)
{
    printf(
        "Klavye komutlari:\n"
        "  b  BOOT_NOTIFY gonder (RTU TIME_SYNC tetiklemeli)\n"
        "  p  PING gonder (RTU bostan ACK donmeli)\n"
        "  d  DISCOVERY_REPORT gonder (atanmamis EUI)\n"
        "  t  TRIP_NOTIFY gonder (envanterdeki 1. cihaz)\n"
        "  v  LIVE_DATA gonder\n"
        "  n  ANOMALY_REPORT gonder\n"
        "  l  3 olay kaydi ekle + LOG_AVAILABLE zili\n"
        "  e  Bir sonraki 0x20 istegini ERROR 0x05 ile yanitla (taze yok)\n"
        "  x  Bir sonraki istege hic yanit verme (RTU retry provasi)\n"
        "  f  Bir sonraki COMMIT'i FAILED yap (sebep RANGE)\n"
        "  1  0x40 modunu R0<->R1 degistir (8 B / 10 B tail)\n"
        "  s  Durum ozeti (envanter/grup/halka)\n"
        "  h  Bu yardim\n"
        "  q  Cikis\n");
}

static void print_state(void)
{
    size_t i;
    printf("---- DURUM (log #%u) ----\n", log_index);
    printf("envanter_loaded=%u  grup: id=%u state=%u members=0x%02X "
           "crc=0x%04X\n",
           hub.inventory_loaded, hub.group_id, hub.group_state,
           hub.group_members, hub.group_crc);
    for (i = 0U; i < HUB_DEVICES_MAX; i++)
    {
        const hub_device_t *d = &hub.dev[i];
        if (d->valid != 0U)
        {
            char eui_hex[17];
            size_t k;
            static const char hex[] = "0123456789ABCDEF";
            for (k = 0U; k < 8U; k++)
            {
                eui_hex[(k * 2U)] = hex[(d->eui[k] >> 4) & 0x0FU];
                eui_hex[(k * 2U) + 1U] = hex[d->eui[k] & 0x0FU];
            }
            eui_hex[16] = '\0';
            printf("  [%zu] %s zone=%u fider=%u phase=%u ch=%u fresh=%u "
                   "staged=%u\n",
                   i, eui_hex, d->zone, d->fider, d->phase, d->channel,
                   d->fresh, d->staged);
        }
    }
    printf("halka: head=%u wrap=%u tail=%u pending=%u total=%u "
           "(0x40 modu: %s)\n",
           hub.head, hub.wrap, hub.tail, hub.pending, hub.total_events,
           (hub.r1_mode != 0U) ? "R1 (10B)" : "R0 (8B)");
    printf("-------------------------\n");
}

/* ======================================================================
 * Main
 * ====================================================================== */
int main(int argc, char *argv[])
{
    const char *port = NULL;
    uint32_t baud = 230400U;
    uint8_t r1_mode = 0U;
    uint8_t send_boot = 1U;
    int i;
    uint8_t rx_buf[RX_CHUNK];

    for (i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "--r1") == 0))
        {
            r1_mode = 1U;
        }
        else if ((strcmp(argv[i], "--noboot") == 0))
        {
            send_boot = 0U;
        }
        else if ((strcmp(argv[i], "--baud") == 0) && ((i + 1) < argc))
        {
            baud = (uint32_t)strtoul(argv[++i], NULL, 10);
        }
        else if (argv[i][0] != '-')
        {
            port = argv[i];
        }
    }

    if (port == NULL)
    {
        (void)fprintf(stderr,
                      "Kullanim: rf_hub_sim COM7 [--baud 230400] "
                      "[--r1] [--noboot]\n");
        return 1;
    }

    if (serial_open(port, baud) != 0)
    {
        return 1;
    }

    if (scp_init(&scp_ctx, 0x01U, tx_to_serial, NULL, 100U) != SCP_STATUS_OK)
    {
        (void)fprintf(stderr, "scp_init hatasi\n");
        serial_close();
        return 1;
    }

    hub_init(&hub, hub_send_packet, NULL, r1_mode);
    hub_tick(&hub, GetTickCount());

    printf("=== RF HUB SIMULATOR ===\n");
    printf("Port=%s baud=%u 0x40 modu=%s\n", port, baud,
           (r1_mode != 0U) ? "R1 (10B tail)" : "R0 (8B)");
    print_help();
    printf("========================\n\n");

    if (send_boot != 0U)
    {
        Sleep(300);
        hub_tick(&hub, GetTickCount());
        hub_send_boot_notify(&hub);
    }

    for (;;)
    {
        uint32_t now = GetTickCount();
        int got = serial_read(rx_buf, sizeof(rx_buf));
        int k;

        if (got > 0)
        {
            for (k = 0; k < got; k++)
            {
                feed_rx_byte(rx_buf[k], now);
            }
        }

        flush_raw_note(now);
        hub_tick(&hub, now);

        while (_kbhit() != 0)
        {
            int c = _getch();
            switch (c)
            {
                case 'b': hub_send_boot_notify(&hub);   break;
                case 'p': hub_send_ping(&hub);          break;
                case 'd': hub_send_discovery(&hub);     break;
                case 't': hub_send_trip(&hub);          break;
                case 'v': hub_send_live(&hub);          break;
                case 'n': hub_send_anomaly(&hub);       break;
                case 'l': hub_add_events(&hub, 3U, 4U);
                          hub_send_log_bell(&hub);      break;
                case 'e': hub.stale_0x20_next = 1U;
                          printf("[SIM] sonraki 0x20 -> ERROR 0x05\n"); break;
                case 'x': hub.drop_next = 1U;
                          printf("[SIM] sonraki istek yutulacak\n");    break;
                case 'f': hub.commit_fail = 1U;
                          hub.commit_fail_reason = 3U;
                          printf("[SIM] sonraki COMMIT FAILED olacak\n");
                          break;
                case '1': hub.r1_mode = (uint8_t)(hub.r1_mode ^ 1U);
                          printf("[SIM] 0x40 modu: %s\n",
                                 (hub.r1_mode != 0U) ? "R1" : "R0"); break;
                case 's': print_state();                break;
                case 'h': print_help();                 break;
                case 'q': printf("Cikis (toplam log: %u)\n", log_index);
                          serial_close();
                          return 0;
                default: break;
            }
        }

        Sleep(5);
    }
}

/*** end of file ***/
