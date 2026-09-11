/*
 * fault_log.c
 *
 *  Created on: Feb 21, 2026
 *      Author: fatih
 */
#include <spi_flash_organization.h>
#include <stddef.h>
#include <string.h>
#include "fault_log.h"
#include "w25qxx.h"
#include "bsp.h"
#include "crc32.h"
#include "datetime.h"
#include "shell.h"
#include "cp56time2a.h"
#include "utils.h"

#define FAULT_LOG_COUNT 15

#define FAULT_LOG_FEEDER_1_ADDRESS          (FAULT_LOG_ADDRESS)
#define FAULT_LOG_FEEDER_2_ADDRESS          (FAULT_LOG_ADDRESS + 4096U)
#define FAULT_LOG_FEEDER_3_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*2)
#define FAULT_LOG_FEEDER_4_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*3)
#define FAULT_LOG_FEEDER_5_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*4)
#define FAULT_LOG_FEEDER_6_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*5)
#define FAULT_LOG_FEEDER_7_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*6)
#define FAULT_LOG_FEEDER_8_ADDRESS          (FAULT_LOG_ADDRESS + 4096U*7)

#define INVALID_FAULT_LOG_ADDRESS 0xFFFFFFFF

const uint32_t fault_log_feeder_addresses[MAX_POWER_LINE_COUNT] = {
	FAULT_LOG_FEEDER_1_ADDRESS,
	FAULT_LOG_FEEDER_2_ADDRESS,
	FAULT_LOG_FEEDER_3_ADDRESS,
	FAULT_LOG_FEEDER_4_ADDRESS,
	FAULT_LOG_FEEDER_5_ADDRESS,
	FAULT_LOG_FEEDER_6_ADDRESS,
	FAULT_LOG_FEEDER_7_ADDRESS
};

/* =====================================================================
 * Iki slotlu (A ana / B yedek) depolama cekirdegi - nvram.c portudur
 * (doc/nvram.md sozlesmesi; feeder basina bir A/B slot catali):
 *   - A silinmeden once B, guncel goruntunun DOGRULANMIS kopyasini tasir.
 *   - Basarili save sonunda A ve B ayni goruntudedir.
 *   - Her hata islemi durdurur; tek saglam kopya, digeri dogrulanmadan
 *     asla silinmez.
 *   - Save/onarim sirasinda RAM goruntusu degismez (flash->flash
 *     kopyalar kucuk sayfa tamponuyla yapilir; ikinci tam boy RAM/stack
 *     goruntusu YOKTUR).
 *   - Sequence yalnizca yeni kayitta, flash'taki son gecerli surumden
 *     hazirlanir; salt onarimda artmaz. Ilk kurulum disinda sifirlanmaz.
 * NOT (senkron sozlesmesi): nvram.c'de bu cekirdege davranis degisikligi
 * yapilirsa ayni degisiklik buraya da uygulanmalidir - ortak cekirdek
 * modulu cikarilana kadar iki dosya elle es tutulur.
 * ===================================================================== */

#define FAULT_LOG_MAGIC          0x54524B46U   /* "TRKF" */
#define FAULT_LOG_SCHEMA_VERSION 1U

#define FAULT_LOG_SLOT_A 0U
#define FAULT_LOG_SLOT_B 1U
#define FAULT_LOG_SLOT_COUNT 2U

typedef struct
{
	uint32_t magic;           /* FAULT_LOG_MAGIC - CRC'den ONCE kontrol edilir */
	uint32_t schema_version;  /* FAULT_LOG_SCHEMA_VERSION */
	uint32_t length;          /* toplam goruntu boyutu (crc dahil) - okuma ve CRC kapsami buna gore */
	uint32_t sequence;        /* monoton yazim sayaci; cift kopyada taze (yuksek) olan kazanir */
	fault_log_t temporary_fault_log[PHASE_MAX][FAULT_LOG_COUNT];
	fault_log_t permanent_fault_log[PHASE_MAX][FAULT_LOG_COUNT];
	uint8_t temporary_fault_log_index[PHASE_MAX];
	uint8_t permanent_fault_log_index[PHASE_MAX];
	uint32_t total_temporary_faults[PHASE_MAX];
	uint32_t total_permanent_faults[PHASE_MAX];
	crc32_t crc;
}fault_log_feeder_history_t;

/* Layout kaymasini derleme zamanina cevir (nvram_t bekcilerinin esleri). */
_Static_assert(offsetof(fault_log_feeder_history_t, magic) == 0U, "fault_log: magic@0");
_Static_assert(offsetof(fault_log_feeder_history_t, schema_version) == 4U, "fault_log: schema_version@4");
_Static_assert(offsetof(fault_log_feeder_history_t, length) == 8U, "fault_log: length@8");
_Static_assert(offsetof(fault_log_feeder_history_t, sequence) == 12U, "fault_log: sequence@12");
_Static_assert(offsetof(fault_log_feeder_history_t, crc) == (sizeof(fault_log_feeder_history_t) - 4U), "fault_log: crc kuyrugun sonunda");
_Static_assert(sizeof(fault_log_feeder_history_t) <= 4096U, "feeder goruntusu kendi 4K slotuna sigmali");
_Static_assert(((FAULT_LOG_ADDRESS % 4096U) == 0U) && ((FAULT_LOG_BACKUP_ADDRESS % 4096U) == 0U), "fault_log slot adresleri 4K sektore hizali olmali");
_Static_assert(FAULT_LOG_ADDRESS != FAULT_LOG_BACKUP_ADDRESS, "fault_log slot adresleri cakismamali");

static fault_log_feeder_history_t g_feeder_log;
static int8_t g_current_feeder = -1;    /* RAM goruntusunun ait oldugu feeder, -1 = hicbiri */

/* Ortak kucuk calisma tamponu: akiskan dogrulama + flash->flash kopya.
 * Tam boy ikinci bir goruntu (RAM ya da stack) KULLANILMAZ. */
static uint8_t fault_log_page_buf[256];

/* Kalicilastirilan surumun veri CRC'si: kirli-durum algisinin tek kaynagi
 * (calc == persisted ise veri degismedi demektir). */
static crc32_t fault_log_persisted_crc;

/* Save/onarim kilidi: tek sahip, ic ice lock yok. Kilitliyken RAM
 * goruntusu donuktur - kayit ekleme mutasyonu reddeder. */
static bool fault_log_busy;

/* Forward declarations */
static crc32_t calculate_crc(const void *data, uint32_t len);
static bool fault_log_change_allowed(void);
static int fault_log_sync_internal(bool crc_no_check);

/* Guncel feeder'in kopya adresleri. */
static uint32_t fault_log_slot_addr(uint32_t slot)
{
	return (slot == FAULT_LOG_SLOT_B)
	    ? (FAULT_LOG_BACKUP_ADDRESS + (uint32_t)g_current_feeder * 4096U)
	    : fault_log_feeder_addresses[g_current_feeder];
}

/* Tasima guvenli tazelik karsilastirmasi (uint32 sarma). */
static bool fault_log_seq_newer(uint32_t a, uint32_t b)
{
	return ((int32_t)(a - b)) > 0;
}

/* Slot basligi - fault_log_feeder_history_t'nin ilk 16 bayti ile ayni sirada
 * (yukaridaki offsetof assert'leri bu sozlesme korur). */
typedef struct
{
	uint32_t magic;
	uint32_t schema_version;
	uint32_t length;
	uint32_t sequence;
} fault_log_hdr_t;

static void fault_log_read_hdr(uint32_t addr, fault_log_hdr_t *hdr)
{
	w25qxx_read_buff(addr, hdr, sizeof(fault_log_hdr_t));
}

/* Header akil sagligi kontrolu: magic + uzunluk sinirlari (ucuz on bakis;
 * tam dogrulama fault_log_validate_slot'in CRC akisindadir). */
static bool fault_log_hdr_sane(const fault_log_hdr_t *hdr)
{
	return (hdr->magic == FAULT_LOG_MAGIC) &&
	       (hdr->length >= (uint32_t)(offsetof(fault_log_feeder_history_t, crc) + 4U)) &&
	       (hdr->length <= (uint32_t)sizeof(fault_log_feeder_history_t));
}

/* Slot durumu - her save/onarim baslangicinda flash'tan kurulur. */
typedef struct
{
	bool     valid;      /* header + sema + CRC dogrulandi */
	uint32_t sequence;
	crc32_t  crc;        /* goruntunun saklanan CRC alani */
} fault_log_slot_state_t;

/* Slotu AKISKAN dogrula: header -> sema -> CRC. Goruntu RAM'e YUKLENMEZ;
 * 256 B sayfa tamponuyla okunur, CRC artimli hesaplanir - boylece ikinci
 * tam boy goruntuye gerek kalmaz ve dogrulama fiziksel readback olur. */
static bool fault_log_validate_slot(uint32_t slot, fault_log_slot_state_t *st)
{
	const uint32_t addr = fault_log_slot_addr(slot);
	fault_log_hdr_t hdr;
	crc32_t crc;
	uint32_t off;
	uint32_t remaining;
	uint32_t chunk;
	uint32_t stored_crc;

	*st = (fault_log_slot_state_t){0};   /* gecersiz durumda alanlar da tanimli olsun */

	fault_log_read_hdr(addr, &hdr);
	if (!fault_log_hdr_sane(&hdr))
	{
		return false;
	}
	if (hdr.schema_version != FAULT_LOG_SCHEMA_VERSION)
	{
		return false;
	}

	crc = crc32_init();
	off = 0U;
	remaining = hdr.length - 4U;

	while (remaining > 0U)
	{
		chunk = (remaining > (uint32_t)sizeof(fault_log_page_buf))
		            ? (uint32_t)sizeof(fault_log_page_buf) : remaining;
		w25qxx_read_buff(addr + off, fault_log_page_buf, chunk);
		crc = crc32_update(crc, fault_log_page_buf, chunk);
		off += chunk;
		remaining -= chunk;
	}

	w25qxx_read_buff(addr + off, &stored_crc, 4U);
	if (crc32_finalize(crc) != stored_crc)
	{
		return false;
	}

	st->valid = true;
	st->sequence = hdr.sequence;
	st->crc = stored_crc;
	return true;
}

/* Iki DOGRULANMIS slot ayni goruntu mu? Pratik esdegerlik: ayni sequence
 * + ayni saklanan CRC. CRC esitligi bayt-esitliginin KANITI degildir
 * (cati$ma teorik olarak mumkun); amac yalnizca "B tazeleme gerekli mi"
 * karari verdirmektir. */
static bool fault_log_slots_equal(const fault_log_slot_state_t *a,
                                  const fault_log_slot_state_t *b)
{
	return a->valid && b->valid
	       && (a->sequence == b->sequence)
	       && (a->crc == b->crc);
}

/* Global RAM goruntusunu slota yaz ve dogrula. Surucu erase-once yazar ve
 * fiziksel readback ile dogrular: tek cagri = tek sektor silme + dogrulama. */
static int fault_log_write_slot(uint32_t slot)
{
	if (w25qxx_write_buff(fault_log_slot_addr(slot), &g_feeder_log, sizeof(g_feeder_log))
	        != W25QXX_RES_OK)
	{
		return -1;
	}
	return 0;
}

/* Flash'tan flash'a slot kopyasi: hedefi BIR kez sil, sayfa sayfa programla,
 * ardindan akiskan dogrula (fiziksel readback). Global RAM'e DOKUNMAZ -
 * kaydedilmemis kayitlar korunur. Kopya, kaynak goruntuyu AYNI sequence ile
 * tasiyacak bicimde birebir kopyalar. */
static int fault_log_copy_slot(uint32_t dst, uint32_t src)
{
	const uint32_t dst_addr = fault_log_slot_addr(dst);
	const uint32_t src_addr = fault_log_slot_addr(src);
	fault_log_slot_state_t st;
	uint32_t off;
	uint32_t chunk;

	if (w25qxx_erase_sector(dst_addr) != W25QXX_RES_OK)
	{
		return -1;
	}

	for (off = 0U; off < (uint32_t)sizeof(g_feeder_log);
	     off += (uint32_t)sizeof(fault_log_page_buf))
	{
		chunk = (uint32_t)sizeof(g_feeder_log) - off;
		if (chunk > (uint32_t)sizeof(fault_log_page_buf))
		{
			chunk = (uint32_t)sizeof(fault_log_page_buf);
		}
		w25qxx_read_buff(src_addr + off, fault_log_page_buf, chunk);
		if (w25qxx_page_write(dst_addr + off, fault_log_page_buf, chunk)
		        != W25QXX_RES_OK)
		{
			return -1;
		}
	}

	if (!fault_log_validate_slot(dst, &st))
	{
		return -1;
	}
	return 0;
}

/* Faz 1 - yedek tamamlama: A silinmeden ONCE her iki slot elden gecirilir.
 * Tum kopyalar flash->flash yapilir; global RAM goruntusune dokunulmaz.
 * Donus -1: tek saglam kopya dogrulanmadan kayda gecilmez. */
static int fault_log_prepare_slots(fault_log_slot_state_t *a, fault_log_slot_state_t *b)
{
	/* Yalniz B gecerliyse (ya da B daha tazeyse): once A'yi B'den kur -
	 * boylece kayit A'yi silerken B eldeki guncel goruntuyu tasir. */
	if (b->valid && (!a->valid || fault_log_seq_newer(b->sequence, a->sequence)))
	{
		if (fault_log_copy_slot(FAULT_LOG_SLOT_A, FAULT_LOG_SLOT_B) != 0)
		{
			return -1;
		}
		(void)fault_log_validate_slot(FAULT_LOG_SLOT_A, a);
	}

	/* A gecerli ve B onun aynisi degilse: B'yi A'dan tazele. */
	if (a->valid && !fault_log_slots_equal(a, b))
	{
		if (fault_log_copy_slot(FAULT_LOG_SLOT_B, FAULT_LOG_SLOT_A) != 0)
		{
			return -1;
		}
		(void)fault_log_validate_slot(FAULT_LOG_SLOT_B, b);
	}
	return 0;
}

static crc32_t fault_log_image_crc(void)
{
	crc32_t crc = crc32_init();
	crc = crc32_update(crc, &g_feeder_log, (sizeof(g_feeder_log) - sizeof(g_feeder_log.crc)));
	return crc32_finalize(crc);
}

/* Kirli-durum sorgusu (tek kaynak: calc == persisted). */
static bool fault_log_image_changed(void)
{
	return fault_log_image_crc() != fault_log_persisted_crc;
}

static int fault_log_sync_locked(bool crc_no_check)
{
	fault_log_slot_state_t a;
	fault_log_slot_state_t b;
	crc32_t ram_crc = fault_log_image_crc();

	(void)fault_log_validate_slot(FAULT_LOG_SLOT_A, &a);
	(void)fault_log_validate_slot(FAULT_LOG_SLOT_B, &b);

	/* Faz 1 - yedek tamamlama (flash->flash; RAM goruntusu donuk). */
	if (fault_log_prepare_slots(&a, &b) != 0)
	{
		CCSLOG(XCOLOR_RED,
		       "Feeder %d: slot prepare FAILED - save rejected, good copy untouched.\r\n",
		       g_current_feeder + 1);
		return -1;
	}

	if (!a.valid && !b.valid)
	{
		/* Bakir kurulum akisi: defaults yukleme yolundan seq=0 ile gelir. */
	}
	else if (!crc_no_check && (ram_crc == fault_log_persisted_crc))
	{
		/* Veri degismedi; slotlar Faz 1'de esitlendi - gereksiz yazma yok. */
		return 0;
	}
	else if (a.valid && (a.crc == ram_crc))
	{
		/* Retry tamamlanmasi: A zaten RAM goruntusunun AYNISINI tasiyor ve
		 * B de Faz 1'de esitlendi - yeniden yazmaya gerek yok. */
		g_feeder_log.crc = ram_crc;
		g_feeder_log.sequence = a.sequence;
		fault_log_persisted_crc = ram_crc;
		return 0;
	}

	/* Yeni goruntu: sequence YALNIZ gecerli flash goruntusunden uretilir -
	 * RAM'deki (basarisiz denemeden kalmis) sequence'e bakilmaz. Boylece
	 * hazirlik deterministiktir: retry ayni degeri tekrar uretir ve
	 * sequence ikinci kez artirilmaz. Bakir kurulumda (iki slot gecersiz)
	 * taban 0'dir. */
	{
		uint32_t base = 0U;

		if (a.valid && b.valid)
		{
			base = fault_log_seq_newer(a.sequence, b.sequence) ? a.sequence : b.sequence;
		}
		else if (a.valid)
		{
			base = a.sequence;
		}
		else if (b.valid)
		{
			base = b.sequence;
		}
		g_feeder_log.sequence = base + 1U;
	}

	/* Layout isaretleri: memset gecmis goruntuler (ilk kurulum, clear)
	 * icin baslik yeniden kurulur; zaten dogruysa yazmak zararsiz. */
	g_feeder_log.magic          = FAULT_LOG_MAGIC;
	g_feeder_log.schema_version = FAULT_LOG_SCHEMA_VERSION;
	g_feeder_log.length         = (uint32_t)sizeof(g_feeder_log);
	g_feeder_log.crc            = fault_log_image_crc();

	/* A once: A silinmeden once B, mevcut goruntunun dogrulanmis kopyasini
	 * tasir (Faz 1). A yazilamazsa B saglam kalir. */
	if (fault_log_write_slot(FAULT_LOG_SLOT_A) != 0)
	{
		CCSLOG(XCOLOR_RED,
		       "Feeder %d: slot A write FAILED - B keeps the last verified image.\r\n",
		       g_current_feeder + 1);
		return -1;
	}

	/* B sonra: B silinirken A yeni goruntunun dogrulanmis kopyasini tasir. */
	if (fault_log_write_slot(FAULT_LOG_SLOT_B) != 0)
	{
		CCSLOG(XCOLOR_RED,
		       "Feeder %d: slot B write FAILED - A holds the new verified image.\r\n",
		       g_current_feeder + 1);
		return -1;
	}

	fault_log_persisted_crc = g_feeder_log.crc;
	return 0;
}

static int fault_log_sync_internal(bool crc_no_check)
{
	int res;

	if (fault_log_busy)
	{
		/* Ic ice lock yasak: tek sahip kurali. */
		CCSLOG(XCOLOR_RED, "FAULT LOG: sync re-entry rejected (save in progress).\r\n");
		return -1;
	}

	fault_log_busy = true;
	res = fault_log_sync_locked(crc_no_check);
	fault_log_busy = false;
	return res;
}

/* Save/onarim sirasinda RAM goruntusu donuktur: kayit ekleme mutasyonu
 * reddedilir (nvram setter-reddi kuralinin fault_log karsiligi). */
static bool fault_log_change_allowed(void)
{
	if (fault_log_busy)
	{
		CCSLOG(XCOLOR_RED, "FAULT LOG: change rejected - save/repair in progress.\r\n");
		return false;
	}
	return true;
}

/* ---- Feeder gecis katmani (nvram'da karsiligi yok) ---------------------
 * RAM'de tek aktif goruntu vardir; baska feeder'a gecmeden once degisen
 * goruntu flush edilir. Flush basarisizsa gecis REDDEDILIR - kaydedilmemis
 * RAM tamponu boylece hayatta kalir ve sonraki deneme yeniden dener.
 * Donus 0: gecis tamam; -1: bekleyen flush basarisiz (gecis yok). */
static int fault_log_load_feeder(uint8_t feeder_id)
{
	fault_log_slot_state_t a;
	fault_log_slot_state_t b;

	if (g_current_feeder == (int8_t)feeder_id){
		return 0;
	}

	if ((g_current_feeder >= 0) && fault_log_image_changed())
	{
		if (fault_log_sync_internal(false) != 0)
		{
			CCSLOG(XCOLOR_RED,
			       "Feeder %d: flush FAILED - switch to feeder %d rejected, RAM preserved.\r\n",
			       g_current_feeder + 1, feeder_id + 1);
			return -1;
		}
	}

	g_current_feeder = (int8_t)feeder_id;

	(void)fault_log_validate_slot(FAULT_LOG_SLOT_A, &a);
	(void)fault_log_validate_slot(FAULT_LOG_SLOT_B, &b);

	if (a.valid || b.valid)
	{
		/* En guncel GECERLI goruntuyu sec ve global yapiya yukle. */
		uint32_t load_slot = FAULT_LOG_SLOT_A;

		if (!a.valid)
		{
			load_slot = FAULT_LOG_SLOT_B;
		}
		else if (b.valid && fault_log_seq_newer(b.sequence, a.sequence))
		{
			load_slot = FAULT_LOG_SLOT_B;
		}

		w25qxx_read_buff(fault_log_slot_addr(load_slot), &g_feeder_log, sizeof(g_feeder_log));
		fault_log_persisted_crc = g_feeder_log.crc;

		if (load_slot == FAULT_LOG_SLOT_B)
		{
			CCSLOG(XCOLOR_CYAN, "Feeder %d: yedek slottan yuklendi (ana sequence=%u, yedek=%u).\r\n",
			       feeder_id + 1, (unsigned)a.sequence, (unsigned)b.sequence);
		}

		/* Acilis onarimi: iki kopya da ayni dogrulanmis goruntuyu tasin.
		 * Basarisizsa kayitlar okunabilir kalir; sonraki sync, onarimi
		 * Faz 1'de tamamlamadan A'yi silmez. */
		if (fault_log_prepare_slots(&a, &b) != 0)
		{
			CCSLOG(XCOLOR_RED,
			       "Feeder %d: slot repair FAILED - data usable, repair retried on next sync.\r\n",
			       feeder_id + 1);
		}
	}
	else
	{
		CCSLOG(XCOLOR_RED, "Feeder %d: kullanilabilir slot yok - log temizleniyor.\r\n", feeder_id + 1);
		memset(&g_feeder_log, 0, sizeof(g_feeder_log));
		g_feeder_log.magic          = FAULT_LOG_MAGIC;
		g_feeder_log.schema_version = FAULT_LOG_SCHEMA_VERSION;
		g_feeder_log.length         = (uint32_t)sizeof(g_feeder_log);
		g_feeder_log.sequence       = 0U;    /* ilk kurulum: bakir flash sifirdan baslar */
		if (fault_log_sync_internal(true) != 0)
		{
			CCSLOG(XCOLOR_RED,
			       "Feeder %d: default log write FAILED - retried on next sync.\r\n",
			       feeder_id + 1);
		}
	}

	return 0;
}

static void print_log(int entry_num, const fault_log_t *log)
{
	SHELL_CLOG(XCOLOR_CYAN, "  [%2d] %02u-%02u-%04u %02u:%02u:%02u  I=%.1fA  T=%ums  Nominal=%s  Power=%s\r\n",
			entry_num,
			log->tm.day, log->tm.month, (uint32_t)(log->tm.year + 2000),
			log->tm.hour, log->tm.minute, cp56time2a_get_second(&log->tm),
			(double)fault_log_current_amps(log),
			log->fault_duration_ms,
			log->info.nominal_current_status ? "Below" : "Normal",
			log->info.power_status ? "On" : "Off");
}

static void fault_log_dump_feeder(uint8_t feeder)
{
	if(fault_log_load_feeder(feeder) != 0){
		SHELL_LOG("\r\n=== Feeder %d: unavailable (pending flush, RAM preserved) ===\r\n",
				feeder + 1);
		return;
	}

	const fault_log_feeder_history_t *fh = &g_feeder_log;

	SHELL_LOG("\r\n=== Feeder %d ===\r\n", feeder + 1);

	for(int phase = 0; phase < PHASE_MAX; phase++)
	{
		uint32_t total_temp = fh->total_temporary_faults[phase];
		uint32_t temp_count = total_temp > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total_temp;

		SHELL_LOG("  Phase %d — TEMPORARY FAULTS (total: %u, showing: %u, newest first):\r\n",
				phase + 1, total_temp, temp_count);

		if(temp_count == 0)
		{
			SHELL_LOG("  (none)\r\n");
		}
		else
		{
			uint8_t write_idx = fh->temporary_fault_log_index[phase];
			for(uint32_t i = 0; i < temp_count; i++)
			{
				uint8_t idx = (uint8_t)((write_idx - 1u - i + FAULT_LOG_COUNT) % FAULT_LOG_COUNT); // Show newest first
				print_log((int)(i + 1), &fh->temporary_fault_log[phase][idx]);
			}
		}

		uint32_t total_perm_flt = fh->total_permanent_faults[phase];
		uint32_t perm_flt_count = total_perm_flt > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total_perm_flt;

		SHELL_LOG("  Phase %d — PERMANENT FAULTS (total: %u, showing: %u, newest first):\r\n",
				phase + 1, total_perm_flt, perm_flt_count);

		if(perm_flt_count == 0)
		{
			SHELL_LOG("  (none)\r\n");
		}
		else
		{
			uint8_t perm_write_idx = fh->permanent_fault_log_index[phase];
			for(uint32_t i = 0; i < perm_flt_count; i++)
			{
				uint8_t idx = (uint8_t)((perm_write_idx - 1u - i + FAULT_LOG_COUNT) % FAULT_LOG_COUNT);
				print_log((int)(i + 1), &fh->permanent_fault_log[phase][idx]);
			}
		}
	}
	SHELL_LOG("\r\n");
}

void fault_log_dump(void)
{
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		bsp_kick_wdt();
		fault_log_dump_feeder((uint8_t)feeder);
	}
}


static void test_fault_log_add_random(void)
{
	/* Values are encoded so they are self-identifying when read back:
	 *   fault_current (A)  = (feeder+1)*100 + (phase+1)*10 + (i+1)
	 *     e.g. feeder=0, phase=0, i=0  →  111.0 A
	 *          feeder=1, phase=2, i=3  →  234.0 A
	 *   fault_duration_ms  = (feeder+1)*1000 + (phase+1)*100 + (i+1)*10
	 *     e.g. feeder=0, phase=0, i=0  →  1110 ms
	 *          feeder=1, phase=2, i=3  →  2340 ms
	 *   nominal_current_status / power_status:
	 *     temporary  →  0 / 0
	 *     permanent  →  1 / 1
	 */
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		for(int phase = 0; phase < PHASE_MAX; phase++)
		{
			/* 5 temporary */
			for(int i = 0; i < 5; i++)
			{
				float    fault_current     = (feeder + 1) * 100.0f + (phase + 1) * 10.0f + (i + 1) * 1.0f;
				uint16_t fault_duration_ms = (uint16_t)((feeder + 1) * 1000U + (phase + 1) * 100U + (i + 1) * 10U);

				fault_log_add(fault_current, fault_duration_ms,
						0 /* nominal_current_status */, 0 /* power_status */, 0 /* temporary */,
						(uint8_t)feeder, (uint8_t)phase);
			}

			/* 5 permanent */
			for(int i = 0; i < 5; i++)
			{
				float    fault_current     = (feeder + 1) * 100.0f + (phase + 1) * 10.0f + (i + 1) * 1.0f;
				uint16_t fault_duration_ms = (uint16_t)((feeder + 1) * 1000U + (phase + 1) * 100U + (i + 1) * 10U);

				fault_log_add(fault_current, fault_duration_ms,
						1 /* nominal_current_status */, 1 /* power_status */, 1 /* permanent */,
						(uint8_t)feeder, (uint8_t)phase);
			}
		}
	}

	fault_log_sync();
	SHELL_LOG("Test logs added: %d feeders x %d phases x 5 temp + 5 perm.\r\n",
			MAX_POWER_LINE_COUNT, PHASE_MAX);
}

/* Icerik ve sayaclari sifirla. RAM'deki sequence bilincli olarak korunur;
 * yazilacak yeni sequence yine flash'taki gecerli surumden turetilir -
 * boylece kismi yazma hatasinda hayatta kalan eski kopya, temiz
 * goruntuden daha YENI sayilamaz (nvram fabrika reseti ile ayni garanti). */
static void fault_log_clear_contents(void)
{
	uint32_t keep_sequence = g_feeder_log.sequence;

	memset(&g_feeder_log, 0, sizeof(g_feeder_log));

	/* Layout gecerlilik isaretleri - bunlar olmadan yazilan bos goruntu
	 * bir sonraki acilista yine default-reset'e dusardi. */
	g_feeder_log.magic          = FAULT_LOG_MAGIC;
	g_feeder_log.schema_version = FAULT_LOG_SCHEMA_VERSION;
	g_feeder_log.length         = (uint32_t)sizeof(g_feeder_log);
	g_feeder_log.sequence       = keep_sequence;
}

void fault_log_clear(void)
{
	bool all_ok = true;

	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		/* Once guncel gecerli goruntuyu yukle: clear'in sequence tabani
		 * flash'taki en taze kopyadan gelsin. Bekleyen flush basarisizsa
		 * gecis reddedilir - o feeder sonra tekrar denenmelidir. */
		if(fault_log_load_feeder((uint8_t)feeder) != 0){
			all_ok = false;
			continue;
		}

		fault_log_clear_contents();

		if(fault_log_sync() != 0){
			/* Temizlik o feeder icin kaliclasmadi; dongu kalan
			 * feeder'larin flush denemelerinde yeniden denenecek. */
			all_ok = false;
		}
	}

	/* RAM artik hicbir feeder'a ait degil: bir sonraki erisim flash'tan
	 * yeniden yukler. */
	g_current_feeder = -1;

	SHELL_LOG(all_ok ? "Fault log cleared.\r\n"
	                 : "Fault log clear PARTIALLY FAILED (flash write error).\r\n");
}

static int shell_fltlog_dump(int argc, char *argv[])
{
	if(argc > 1 && strcmp(argv[1], "test") == 0)
	{
		test_fault_log_add_random();
	}
	else if(argc > 1 && strcmp(argv[1], "clear") == 0)
	{
		fault_log_clear();
	}
	else if(argc > 1 && strcmp(argv[1], "dump") == 0)
	{
		if(argc > 2)
		{
			int feeder = xstrtoi(argv[2]);

			if((feeder < 1) || (feeder > (int)MAX_POWER_LINE_COUNT))
			{
				SHELL_LOG("Usage: fltlog dump [feeder 1..%u]\r\n",
						(unsigned)MAX_POWER_LINE_COUNT);
				return -1;
			}

			fault_log_dump_feeder((uint8_t)(feeder - 1));
		}
		else
		{
			fault_log_dump();
		}
	}
	else
	{
		SHELL_LOG("Usage: fltlog <command>\r\n"
				"  dump [1..%u] - dump all feeders or feeder n\r\n"
				"  test         - add random test logs\r\n"
				"  clear        - clear all logs\r\n",
				(unsigned)MAX_POWER_LINE_COUNT);
		return -1;
	}

	return 0;
}

static crc32_t calculate_crc(const void *data, uint32_t len)
{
	if(len < sizeof(crc32_t)){
		CCSLOG(XCOLOR_RED, "Data length too small for CRC calculation: %u bytes\r\n", len);
		return 0;
	}

    crc32_t crc = crc32_init();
    crc = crc32_update(crc, data, (len - sizeof(crc32_t)));
    return crc32_finalize(crc);
}

int fault_log_sync(void)
{
	if (g_current_feeder < 0){
		return 0;
	}
	return fault_log_sync_internal(false);
}

void fault_log_init(void)
{
	CSLOG("sizeof(fault_log_t) = %u bytes\r\n", (unsigned)sizeof(fault_log_t));
	CSLOG("sizeof(fault_log_feeder_history_t) = %u bytes\r\n", (unsigned)sizeof(fault_log_feeder_history_t));

	/* Her feeder'i yukle: gecerli kopyayi sec, bozuk ikizi onar; bakir
	 * flash'ta default bos goruntu hemen yazilir (load_feeder icinde). */
	for(int feeder = 0; feeder < MAX_POWER_LINE_COUNT; feeder++)
	{
		g_current_feeder = -1; /* force reload */

		if(fault_log_load_feeder((uint8_t)feeder) != 0){
			CSLOG("Feeder %d: load rejected (pending flush) - skipped.\r\n", feeder + 1);
			continue;
		}

		CSLOG("Feeder %d: fault log loaded OK.\r\n", feeder + 1);
	}

	shell_register_command( &(shell_cmd_t){
		.cmd = "fltlog",
		.desc = "Fault log management\r\n"
			"fltlog dump [n]  - dump all feeders or a single feeder\r\n"
			"fltlog test      - add random test logs\r\n"
			"fltlog clear     - clear all logs",
		.func = shell_fltlog_dump}
	);
}

static bool fault_log_add_temporary(uint8_t feeder_id, uint8_t phase_id, const fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	}

	if(!fault_log_change_allowed()){
		return false;
	}

	if(fault_log_load_feeder(feeder_id) != 0){
		return false;    /* gecis reddedildi - gecmis kaybedilmeden kayit da reddedilir */
	}

	uint8_t idx = g_feeder_log.temporary_fault_log_index[phase_id];
	fault_log_t *slot = &g_feeder_log.temporary_fault_log[phase_id][idx];
	*slot = *log;
	slot->crc = calculate_crc(slot, sizeof(fault_log_t));
	g_feeder_log.temporary_fault_log_index[phase_id] = (idx + 1u) % FAULT_LOG_COUNT;
	g_feeder_log.total_temporary_faults[phase_id]++;

	return true;
}

static bool fault_log_add_permanent(uint8_t feeder_id, uint8_t phase_id, const fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	}

	if(!fault_log_change_allowed()){
		return false;
	}

	if(fault_log_load_feeder(feeder_id) != 0){
		return false;    /* gecis reddedildi - gecmis kaybedilmeden kayit da reddedilir */
	}

	uint8_t idx = g_feeder_log.permanent_fault_log_index[phase_id];
	fault_log_t *slot = &g_feeder_log.permanent_fault_log[phase_id][idx];
	*slot = *log;
	slot->crc = calculate_crc(slot, sizeof(fault_log_t));
	g_feeder_log.permanent_fault_log_index[phase_id] = (idx + 1u) % FAULT_LOG_COUNT;
	g_feeder_log.total_permanent_faults[phase_id]++;

	return true;
}

bool fault_log_add(float fault_current, uint16_t fault_duration_ms, uint8_t nominal_current_status,
		uint8_t power_status, uint8_t type, uint8_t feeder_id, uint8_t phase_id)
{
	cp56time2a_t timestamp = cp56time2a_now();

	fault_log_t new_log =
	{
		.tm = timestamp,
		.fault_duration_ms = fault_duration_ms,
		.info = {
			.feeder = feeder_id & 0x07,
			.phase = phase_id & 0x03,
			.nominal_current_status = nominal_current_status ? 1 : 0,
			.power_status = power_status ? 1 : 0,
			.type = type ? 1 : 0
		}
	};

	fault_log_set_current_amps(&new_log, fault_current);

	if(type){
		return fault_log_add_permanent(feeder_id, phase_id, &new_log);
	}
	else{
		return fault_log_add_temporary(feeder_id, phase_id, &new_log);
	}
}

bool fault_log_add_log(fault_log_t *log)
{
	if(log == NULL){
		return false;
	}

	log->tm = cp56time2a_now();
	if(log->info.type){
		return fault_log_add_permanent(log->info.feeder, log->info.phase, log);
	}
	else{
		return fault_log_add_temporary(log->info.feeder, log->info.phase, log);
	}
}


bool fault_log_read_permanent(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || index >= FAULT_LOG_COUNT || log == NULL){
		return false;
	}

	if(fault_log_load_feeder(feeder_id) != 0){
		return false;
	}
	const fault_log_t *entry = &g_feeder_log.permanent_fault_log[phase_id][index];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc)
	{
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u Perm[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u, index, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}

bool fault_log_read_temporary(uint8_t feeder_id, uint8_t phase_id, uint8_t index, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || index >= FAULT_LOG_COUNT || log == NULL){
		return false;
	}

	if(fault_log_load_feeder(feeder_id) != 0){
		return false;
	}
	const fault_log_t *entry = &g_feeder_log.temporary_fault_log[phase_id][index];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc)
	{
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u Temp[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u, index, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}

uint8_t fault_log_get_temp_count(uint8_t feeder_id, uint8_t phase_id)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX){
		return 0;
	}
	if(fault_log_load_feeder(feeder_id) != 0){
		return 0;
	}
	uint32_t total = g_feeder_log.total_temporary_faults[phase_id];
	return (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
}

uint8_t fault_log_get_perm_count(uint8_t feeder_id, uint8_t phase_id)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX){
		return 0;
	}
	if(fault_log_load_feeder(feeder_id) != 0){
		return 0;
	}
	uint32_t total = g_feeder_log.total_permanent_faults[phase_id];
	return (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
}

// Bu fonksiyon dongusel kayitlarin icinden index'e gore okuma yapar.
// 0. index son/guncel kayit, 14. kayit en eski kayit
bool fault_log_read_nth(uint8_t feeder_id, uint8_t phase_id, fault_log_type_t type, uint8_t n, fault_log_t *log)
{
	if(feeder_id >= MAX_POWER_LINE_COUNT || phase_id >= PHASE_MAX || log == NULL){
		return false;
	}

	if(fault_log_load_feeder(feeder_id) != 0){
		return false;
	}

	uint32_t total;
	uint8_t write_idx;
	const fault_log_t *array;

	if(type == FAULT_LOG_TYPE_PERMANENT)
	{
		total     = g_feeder_log.total_permanent_faults[phase_id];
		write_idx = g_feeder_log.permanent_fault_log_index[phase_id];
		array     = g_feeder_log.permanent_fault_log[phase_id];
	} else {
		total     = g_feeder_log.total_temporary_faults[phase_id];
		write_idx = g_feeder_log.temporary_fault_log_index[phase_id];
		array     = g_feeder_log.temporary_fault_log[phase_id];
	}

	uint8_t count = (uint8_t)(total > FAULT_LOG_COUNT ? FAULT_LOG_COUNT : total);
	if(n >= count){
		return false;
	}

	uint8_t slot = (uint8_t)((write_idx - 1u - n + FAULT_LOG_COUNT) % FAULT_LOG_COUNT);
	const fault_log_t *entry = &array[slot];
	crc32_t crc = calculate_crc(entry, sizeof(fault_log_t));
	if(crc != entry->crc){
		CCSLOG(XCOLOR_RED, "Feeder %u Phase %u %s[%u]: CRC mismatch (stored=0x%08X calc=0x%08X)\r\n",
				feeder_id + 1u, phase_id + 1u,
				type == FAULT_LOG_TYPE_PERMANENT ? "Perm" : "Temp",
				n, (unsigned)entry->crc, (unsigned)crc);
		return false;
	}
	*log = *entry;
	return true;
}

/*** end of file ***/
