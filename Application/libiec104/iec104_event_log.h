/*
 * iec104_event_log.h
 *
 *  Created on: Mar 16, 2026
 *      Author: fatih
 */

#ifndef LIBIEC104_IEC104_EVENT_LOG_H_
#define LIBIEC104_IEC104_EVENT_LOG_H_

#include <stdint.h>
#include <stdbool.h>

#include "fault_log.h"

/* ---------------------------------------------------------------------------
 * Kapasite (hesap iec104_event_log.c icinde, spi_flash_log sabitlerinden):
 *   entry  = LOG_ENTRY_OVERHEAD(4) + sizeof(fault_log_t)(18) = 22 bayt
 *   sektor = 4096 / 22 = 186 kayit
 *   halka head icin bir sektoru bos birakir -> (N-1) * 186
 *   8 sektor -> 1302 kayit (3 kayit/gun'de ~434 gun; sartname 90 gun ister)
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
 * cagiran hemen gonderebilirse iec104_event_log_mark_sent() ile geri alir.
 *
 * @param[out] seq_out Kayda atanan seq (NULL olabilir).
 */
bool iec104_event_log_add(const fault_log_t *entry, uint16_t *seq_out);

/**
 * Gonderilmemis kayit sayisi.
 */
uint16_t iec104_event_log_get_unsent_count(void);

/**
 * Gonderilmemis kayitlarin EN YENISINI okur (sartname 2.2.4.2: yeniden
 * eskiye gonderim). Her mark_sent sonrasi bir sonraki (daha eski) kaydi verir.
 * CRC'si bozuk slotlari spi_flash_log atlar; tek bozuk kayit yayimi kilitlemez.
 */
bool iec104_event_log_read_newest_unsent(fault_log_t *out, uint16_t *seq_out);

/**
 * Kaydi gonderildi olarak isaretler (yalnizca RAM).
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
 * [status] | dump | test <N> | clear
 */
void iec104_event_log_shell_init(void);


#endif /* LIBIEC104_IEC104_EVENT_LOG_H_ */

/*** end of file ***/
