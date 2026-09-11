/*
 * iec104_replay.c
 *
 *  Created on: 11 Eyl 2026
 *      Author: fatih
 *
 * Baglanti kurulup GI tamamlandiktan sonra, haberlesme kesikken
 * birikmis olay gunlugu kayitlarini en yeniden eskiye spontane
 * (COT=3) gonderen replay sureci + RF uretici giris noktasi.
 * Kaynak: doc/Sartname_Uyum_Analizi_TEDAS_MLZ_2020-072.md madde 3.15
 * (sertname 2.2.4.2).
 *
 * Depolama spi_flash_log tabanli; gonderilmemis kayitlar NVRAM'deki
 * tek parca [low..high] seq araligi olarak izlenir (bkz.
 * iec104_event_log.c baslik yorumu - kopya/kayip odunu orada).
 */
#define CSLOG_MODULE LOG_MOD_IEC104
#include "iec104_replay.h"
#include "iec104.h"
#include "iec104_application.h"
#include "iec104_event_log.h"
#include "fault_log.h"
#include "console_logger.h"
#include "contiki.h"
#include "contiki_process.h"
#include "sys/pt-sem.h"

/* Kayit basina gecikme: GI yayicilariyla ayni tempo (tick = 1 ms). */
#define REPLAY_RECORD_PERIOD_TICKS   100U
/* Tikanik TX kuyrugu emniyeti: bozuk CRC artik yayimi kilitlemez
 * (spi_flash_log bozuk slotlari atlar); bu sayac yalnizca kuyrugun
 * uzun sure bosalmadigi durumda sureci durdurur. 150 x 100 ms = 15 s. */
#define REPLAY_MAX_CONSEC_FAIL       150U

PROCESS(iec104_replay_process, "iec104_replay_process");

PROCESS_THREAD(iec104_replay_process, ev, data)
{
	static struct etimer timer;
	static fault_log_t record;
	static uint16_t seq;
	static uint32_t fail_count;

	(void)ev;
	(void)data;

	PROCESS_EXITHANDLER({
		/* Soket kapandi: replay durumu NVRAM'e yazilir, kalan kayitlar
		 * unsent kalir ve sonraki baglantinin GI'sinden sonra surer. */
		(void)iec104_event_log_sync();
		CSLOG("replay: kesinti, cikis\r\n");
	});

	PROCESS_BEGIN();

	/* GI yayicilariyla ayni semafor: replay sirasinda gelen bir grup 3/4
	 * sorgulamasi, replay bitene kadar kendi sirasini bekler. */
	PT_SEM_WAIT(&iec104_replay_process.pt, &iec104_tx_sem);

	CSLOG("replay: basliyor (unsent=%u)\r\n",
	      iec104_event_log_get_unsent_count());

	etimer_set(&timer, REPLAY_RECORD_PERIOD_TICKS);
	fail_count = 0U;

	/* Daima "en yeni gonderilmemis" okunur: mark_sent araligin ust ucunu
	 * cektikce ayni sorgu bir sonraki (daha eski) kaydi verir ->
	 * en yeni->eski sirasi kendiliginden kurulur (sertname 2.2.4.2). */
	while (iec104_event_log_get_unsent_count() > 0U)
	{
		bool sent_ok = false;

		if (!iec104_is_link_active())
		{
			break;
		}

		if (iec104_event_log_read_newest_unsent(&record, &seq))
		{
			if (iec104_emit_evtlog_record(&record))
			{
				iec104_event_log_mark_sent(seq);
				sent_ok = true;
			}
		}

		if (sent_ok)
		{
			fail_count = 0U;
		}
		else
		{
			fail_count++;
			if (fail_count >= REPLAY_MAX_CONSEC_FAIL)
			{
				CSLOG_ERR("replay: art arda %u basarisiz kayit, duruyorum\r\n",
				          (unsigned int)fail_count);
				break;
			}
		}

		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
		etimer_reset(&timer);
	}

	(void)iec104_event_log_sync();
	CSLOG("replay: bitti (kalan unsent=%u)\r\n",
	      iec104_event_log_get_unsent_count());

	PT_SEM_SIGNAL(&process_pt, &iec104_tx_sem);

	PROCESS_END();
}

void iec104_replay_start_if_pending(void)
{
	/* GI alt sureclerinden sonuncusu bitirdiginde cagirilir; kardes
	 * surec hala calisiyorken replay baslamaz (siralama: GI -> replay).
	 * Unsent sayacinin kendisi durumdur - ayri oturum bayragi yok. */
	if (process_is_running(&iec104_send_temporary_faults))
	{
		return;
	}
	if (process_is_running(&iec104_send_permanent_faults))
	{
		return;
	}
	if (process_is_running(&iec104_replay_process))
	{
		return;
	}
	if (!iec104_is_link_active())
	{
		return;
	}
	if (0U == iec104_event_log_get_unsent_count())
	{
		return;
	}

	process_start(&iec104_replay_process, NULL);
}

void iec104_report_fault_event(float fault_current, uint16_t fault_duration_ms,
                               uint8_t nominal_current_status, uint8_t power_status,
                               uint8_t type, uint8_t feeder_id, uint8_t phase_id)
{
	fault_log_t record;
	uint16_t seq;
	const fault_log_type_t log_type = (0U == type) ? FAULT_LOG_TYPE_TEMPORARY
	                                               : FAULT_LOG_TYPE_PERMANENT;

	/* 1) 15+15 listeleri (GI grup 3/4 bunlari okur). */
	if (!fault_log_add(fault_current, fault_duration_ms, nominal_current_status,
	                   power_status, type, feeder_id, phase_id))
	{
		CSLOG_ERR("report: fault_log_add basarisiz\r\n");
		return;
	}
	(void)fault_log_sync();

	/* 2) 3 aylik kalici depo. Az once yazilan kayit olceklendirme dahil
	 *    listeden geri okunur - akim olcegi (x10) tek yerde kalsin.
	 *    add() araligi genisletir; bu yuzden asagidaki sync her iki
	 *    durumda da gerekli: gonderim basarisiz olursa ve aralik
	 *    NVRAM'e yazilmazsa kayit flash'ta durur ama unsent listesine
	 *    girmez - enerji kesilirse kaybolur. */
	if (!fault_log_read_nth(feeder_id, phase_id, log_type, 0U, &record))
	{
		CSLOG_ERR("report: kayit listeden geri okunamadi\r\n");
		return;
	}

	if (!iec104_event_log_add(&record, &seq))
	{
		CSLOG_ERR("report: olay gunlugune yazilamadi\r\n");
		return;
	}

	/* 3) Hat aciksa olay aninda spontane gonder.
	 *
	 * Bilincl odun: hat acikken backlog varken gelen kayit sinirli
	 * kopyaya yol acar - mark_sent(seq) araligin ust ucunu ceker ve
	 * bir onceki (zaten gonderilmis) kayit yeniden unsent gorunur.
	 * Kopya, kayiptan iyidir; alternatif (araligi yalnizca gonderim
	 * basarisizsa genisletmek) log_write ile mark_sent arasinda enerji
	 * kesilirse gercek kayip uretir. */
	if (iec104_is_link_active() && iec104_emit_evtlog_record(&record))
	{
		iec104_event_log_mark_sent(seq);
	}

	/* Kayit basina bir nvram_sync() kabul edilebilir - olaylar seyrek. */
	(void)iec104_event_log_sync();
}
