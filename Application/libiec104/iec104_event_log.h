/*
 * iec104_event_log.h
 *
 *  Created on: Mar 16, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef LIBIEC104_IEC104_EVENT_LOG_H_
#define LIBIEC104_IEC104_EVENT_LOG_H_

#include <stdint.h>
#include <stdbool.h>

#include "fault_log.h"

typedef enum
{
    IEC104_EVENT_FAULT = 0,
    IEC104_EVENT_TRIP_FAILURE = 1
} iec104_event_kind_t;

typedef struct
{
    cp56time2a_t time;
    uint8_t feeder;
    uint8_t phase;
    uint8_t active;
} __attribute__((packed)) iec104_alarm_record_t;

typedef struct
{
    uint8_t kind;
    union
    {
        fault_log_t fault;
        iec104_alarm_record_t alarm;
    } payload;
    uint32_t crc;
} __attribute__((packed)) iec104_event_record_t;

_Static_assert(sizeof(iec104_event_record_t) == 25U,
               "IEC event payload must be 25 bytes");

/* ---------------------------------------------------------------------------
 * Kapasite (hesap iec104_event_log.c icinde, spi_flash_log sabitlerinden):
 *   entry = LOG_ENTRY_OVERHEAD(4) + sizeof(iec104_event_record_t)(25) = 29 bytes
 *   sector = 4096 / 29 = 141 records
 *   8 sectors: 1128 records before erase, 987 kept during rotation
 *
 * NOT: bu baslik spi_flash_log.h'i include etmez; kapasite makrolari .c
 * icinde kalir ve flash log kutuphanesi tuketicilere tasinmaz.
 * --------------------------------------------------------------------------- */

/**
 * Olay gunlugunu acar: flash taramasiyla head'i bulur, replay durumunu
 * NVRAM'den yukler. Acilista bir kez cagrilir.
 */
bool iec104_event_log_init(void);

/**
 * Yeni ariza kaydi ekler. Kayit tanimi geregi "gonderilmedi" sayilir;
 * Kuyruga kabul kaydi tuketmez. Gecerli IEC104 N(R) butun ASDU'lari
 * onayladiktan sonra iec104_event_log_mark_sent() ile tuketilir.
 *
 * @param[out] seq_out Kayda atanan seq (NULL olabilir).
 */
bool iec104_event_log_add(const fault_log_t *entry, uint16_t *seq_out);
/* Alarm records use the same durable queue and unsent sequence range. */
bool iec104_event_log_add_alarm(const iec104_alarm_record_t *entry,
                               uint16_t *seq_out);

/**
 * Gonderilmemis kayit sayisi.
 */
uint16_t iec104_event_log_get_unsent_count(void);

/**
 * Reads the typed fault/alarm payload of the newest unsent record.
 * Gonderilmemis kayitlarin EN YENISINI okur (sartname 2.2.4.2: yeniden
 * eskiye gonderim). Her mark_sent sonrasi bir sonraki (daha eski) kaydi verir.
 * CRC'si bozuk slotlari spi_flash_log atlar; tek bozuk kayit yayimi kilitlemez.
 */
bool iec104_event_log_read_newest_unsent(iec104_event_record_t *out,
                                        uint16_t *seq_out);

/**
 * Mark the newest record acknowledged (RAM only); persist with sync.
 * Production callers must establish validated cumulative IEC104 ACK first.
 */
void iec104_event_log_mark_sent(uint16_t seq);

/**
 * Replay durumunu NVRAM'e yazar. Cagrilmasi gereken yerler: hat kapaliyken
 * kayit eklendiginde ve replay bittiginde/kesildiginde. Kayit basina
 * cagrilmaz - nvram_sync() 8 KB'lik bolgeyi yeniden yazar.
 */
int iec104_event_log_sync(void);

/**
 * Tum kayitlari siler ve replay durumunu sifirlar (servis/test).
 */
void iec104_event_log_clear(void);

/**
 * Deterministik sentetik kayit ekler (shell testi).
 */
void iec104_event_log_test(uint16_t count);

/**
 * Kayitlari konsola doker (yeniden eskiye).
 */
void iec104_event_log_dump(void);

/**
 * "iec104evtlog" shell komutunu kaydeder:
 * [status] | dump [ilk_seq son_seq] | test <N> | clear
 * Dump sinirlari dahil seq degerleridir: 0 <= ilk_seq <= son_seq <= 65534.
 */
void iec104_event_log_shell_init(void);


#endif /* LIBIEC104_IEC104_EVENT_LOG_H_ */

/*** end of file ***/
