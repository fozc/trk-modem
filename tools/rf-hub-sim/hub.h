/*
 * hub.h
 *
 *  Created on: Aug 22, 2026
 *      Author: fatih
 *
 * Modem_RF_Hub simulatoru (host). R1 arayuz paketine gore tum komutlara
 * yanit verir, proaktif bildirimleri gonderir, SEQ/idempotency, cfg grup
 * durum makinesi ve olay halkasini simule eder.
 *
 * Referanslar: doc/mailden/Fatih_Paketi_20260821_R1.md,
 * doc/mailden/scp_komut_kullanim_tablosu_R0.md,
 * doc/mailden/0x40_0x44_yanit_duzeni_R1.md.
 *
 * Cekirdek saf C'dir: gonderim geri cagrisi ve zaman disaridan enjekte
 * edilir (kendi-kendini-test modu ayni kodu kullanir).
 */

#ifndef HUB_SIM_HUB_H_
#define HUB_SIM_HUB_H_

#include <stdint.h>
#include <stdbool.h>
#include "scp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUB_DEVICES_MAX      16
#define HUB_LOG_SLOTS        100
#define HUB_EVENT_LEN        60
#define HUB_BLOCK_LEN        96
#define HUB_FW_LABEL         "SIM-R1"

/* CFG grup durumlari (R1 3.3.5) */
typedef enum
{
    HUB_G_IDLE = 0,
    HUB_G_STAGED,
    HUB_G_DELIVERED,
    HUB_G_APPLIED,
    HUB_G_FAILED
} hub_group_state_t;

typedef struct
{
    uint8_t valid;
    uint8_t zone;
    uint8_t fider;
    uint8_t phase;
    uint8_t channel;
    uint8_t eui[8];
    uint8_t fresh;                       /* config en az bir kez toplandi  */
    uint8_t block[HUB_BLOCK_LEN];        /* gecerli config (Ek-A duzeni)   */
    uint8_t staged;                      /* 0x22 ile hazirlanan kopya      */
    uint8_t staged_block[HUB_BLOCK_LEN];
} hub_device_t;

typedef void (*hub_send_fn_t)(const scp_packet_t *pkt, void *user);

typedef struct
{
    /* bagimliliklar */
    hub_send_fn_t send;
    void         *user;
    uint32_t      now_ms;

    /* envanter + config */
    hub_device_t  dev[HUB_DEVICES_MAX];
    uint8_t       inventory_loaded;

    /* cfg yazim grubu */
    uint8_t       group_id;
    uint8_t       group_state;           /* hub_group_state_t              */
    uint8_t       group_members;         /* bit masoesi (dev index)        */
    uint8_t       group_reason;
    uint16_t      group_crc;
    uint32_t      apply_at_ms;           /* DELIVERED -> APPLIED/FAILED    */

    /* olay halkasi (R1 5.1b) */
    uint8_t       events[HUB_LOG_SLOTS][HUB_EVENT_LEN];
    uint16_t      head;                  /* bir SONRAKI yazilacak yuva     */
    uint16_t      wrap;
    uint16_t      tail;                  /* ilk OKUNMAMIS yuva             */
    uint32_t      total_events;
    uint16_t      pending;

    /* zaman */
    uint32_t      start_ms;
    uint8_t       time_synced;
    uint32_t      synced_epoch_sec;      /* senkron duvar saati (unix)     */
    uint32_t      synced_uptime_ms;
    uint32_t      sched_cycles;

    /* SEQ / idempotency (R1 2.3d) */
    uint8_t       last_request_seq;      /* islenen son istegin SEQ'i      */
    uint8_t       last_set_seq;
    uint8_t       have_last_set;
    scp_packet_t  last_reply;            /* son SET yaniti (tekrar oynatma)*/
    uint8_t       capture_reply;         /* sonraki gonderim yakalanir      */
    uint8_t       pending_set_seq;

    /* proaktif sayac */
    uint8_t       proactive_seq;

    /* secenekler / hata enjeksiyonlari */
    uint8_t       r1_mode;               /* 0x40: 10 B (tail) / 8 B        */
    uint8_t       drop_next;             /* sonraki istege yanit YOK       */
    uint8_t       stale_0x20_next;       /* bir kez ERROR 0x05             */
    uint8_t       commit_fail;
    uint8_t       commit_fail_reason;
    uint32_t      epoch_busy_until;

    uint8_t       scp_major;
} hub_t;

/** @brief Simulatoru ilklendir (envanter bos, halka bos, R0 modu). */
void hub_init(hub_t *hub, hub_send_fn_t send, void *user, uint8_t r1_mode);

/** @brief RTU'dan gelen (libscp ile cozulmus) paketi isle. */
void hub_on_packet(hub_t *hub, const scp_packet_t *pkt);

/** @brief Zaman ilerlet: RF dagitim suresi, apply gecikmesi, vs. */
void hub_tick(hub_t *hub, uint32_t now_ms);

/* --- proaktif bildirimler (R1 3.2 B bolumu) --- */
void hub_send_boot_notify(hub_t *hub);
void hub_send_discovery(hub_t *hub);
void hub_send_trip(hub_t *hub);
void hub_send_live(hub_t *hub);
void hub_send_anomaly(hub_t *hub);
void hub_send_log_bell(hub_t *hub);

/** @brief Halkaya count olay ekle (trigger koduyla) ve zil gonder. */
void hub_add_events(hub_t *hub, uint8_t count, uint8_t trigger);

/* --- yardimcilar (test/analiz) --- */
hub_device_t *hub_find_device(hub_t *hub, const uint8_t eui[8]);
uint16_t hub_cfg_crc(const uint8_t block[HUB_BLOCK_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* HUB_SIM_HUB_H_ */
