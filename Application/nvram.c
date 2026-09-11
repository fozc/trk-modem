/*
 * nvram.c
 *
 *  Created on: 15 Agu 2025
 *      Author: fatih
 */
#include "nvram.h"
#include "iec104.h"
#include <stdio.h>
#include <string.h>
#include "crc32.h"
#include "bsp.h"
#include "w25qxx.h"
#include "version.h"
#include "gsm/utils.h"
#include "console_logger.h"
#include "rf_config.h"
#include "elog.h"

static nvram_t nvram = {0};

/* =====================================================================
 * Iki slotlu (A ana / B yedek) depolama cekirdegi - doc/nvram.md:
 *   - A silinmeden once B, guncel goruntunun DOGRULANMIS kopyasini tasir.
 *   - Basarili save sonunda A ve B ayni goruntudedir.
 *   - Her hata islemi durdurur; tek saglam kopya, digeri dogrulanmadan
 *     asla silinmez.
 *   - Save/onarim sirasinda global RAM goruntusu degismez (flash->flash
 *     kopyalar kucuk sayfa tamponuyla yapilir; ikinci tam boy RAM/stack
 *     goruntusu YOKTUR).
 *   - Sequence yalnizca yeni kayitta, flash'taki son gecerli surumden
 *     hazirlanir; salt onarimda artmaz. Ilk kurulum disinda sifirlanmaz.
 * ===================================================================== */

#define NVRAM_SLOT_A 0U
#define NVRAM_SLOT_B 1U
#define NVRAM_SLOT_COUNT 2U

/* Goruntu tek sektorun icinde kalmali: slotlar bagimsiz silinir. */
_Static_assert(sizeof(nvram_t) <= 4096U,
               "nvram goruntusu tek 4K sektor sinirini asmamalidir");
/* Slot adresleri sektore hizali ve birbirinden farkli olmali. */
_Static_assert(((NVRAM_ADDRESS % 4096U) == 0U)
               && ((NVRAM_BACKUP_ADDRESS % 4096U) == 0U),
               "nvram slot adresleri 4K sektore hizali olmalidir");
_Static_assert(NVRAM_ADDRESS != NVRAM_BACKUP_ADDRESS,
               "nvram slot adresleri cakismamalidir");

static const uint32_t nvram_slot_addr[NVRAM_SLOT_COUNT] =
{
    NVRAM_ADDRESS,        /* A: ana alan  */
    NVRAM_BACKUP_ADDRESS  /* B: yedek alan */
};

/* Slot durumu - her save/onarim baslangicinda flash'tan kurulur. */
typedef struct
{
    bool     valid;      /* header + sema + CRC dogrulandi */
    uint32_t sequence;
    crc32_t  crc;        /* goruntunun saklanan CRC alani */
} nvram_slot_state_t;

/* Ortak kucuk calisma tamponu: akiskan dogrulama + flash->flash kopya.
 * Tam boy ikinci bir goruntu (RAM ya da stack) KULLANILMAZ. */
static uint8_t nvram_page_buf[256];

/* Kalicilastirilan surumun veri CRC'si: kirli-durum algisinin tek kaynagi
 * (calc == persisted ise veri degismedi demektir). */
static crc32_t nvram_persisted_crc;

/* Save/onarim kilidi: tek sahip, ic ice lock yok. Kilitliyken RAM
 * goruntusu donuktur - setter'lar mutasyonu reddeder. */
static bool nvram_busy;

/* Setter'larin save/onarim kilidini sorgulamasi icin (tanim asagida). */
static bool nvram_change_allowed(void);

/* Tasima guvenli tazelik karsilastirmasi (uint32 sarma). */
static bool nvram_seq_newer(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b)) > 0;
}

static uint32_t ioa_to_u32(ioa_3byte_t ioa)
{
	return ((uint32_t)ioa.ioa_high << 16) | ((uint32_t)ioa.ioa_mid << 8) | (uint32_t)ioa.ioa_low;
}

static crc32_t nvram_calculate_crc(void)
{
    crc32_t crc = crc32_init();
    crc = crc32_update(crc, &nvram, (sizeof(nvram) - sizeof(nvram.crc)));
    return crc32_finalize(crc);
}

void nvram_test_fill(void)
{
    nvram.breaker.line[0].breaker_state = 1;
    nvram.breaker.line[0].iec104.in_use = 1;
    nvram.breaker.line[0].iec104 = IEC_CONFIG_INIT(1001, 1002, 1003, 1004, 1005, 1006, 1007);


    //nvram.breaker.line[1].breaker_state = 1;
    nvram.breaker.line[1].iec104.in_use = 1;
    nvram.breaker.line[1].iec104 = IEC_CONFIG_INIT(2001, 2002, 2003, 2004, 2005, 2006, 2007);

    nvram.modem_config.serial_number = DEVICE_DEFAULT_SERIAL_NUMBER;
    nvram.iec104_config.originator_address = 1;
    nvram.iec104_config.common_address = 1;
    nvram.iec104_config.periodical_send_interval = 60;
    nvram.iec104_config.t0_max = 90;
    nvram.iec104_config.t1_max = 45;
    nvram.iec104_config.t2_max = 30;
    nvram.iec104_config.t3_max = 60;
    nvram.iec104_config.k_max = 12;
    nvram.iec104_config.w_max = 8;
    nvram.iec104_config.sbo_execute_timeout = IEC104_DEFAULT_SBO_EXECUTE_TIMEOUT;
}

void nvram_set_defaults(void)
{
    if (!nvram_change_allowed())
    {
        return;
    }

    /* Fabrika reseti mevcut sequence ile devam etmeli: sequence sifirlanirsa
     * kismi yazma hatasinda hayatta kalan ESKI yedek kopya "daha yeni" sayilir
     * ve fabrika ayarlari sessizce geri alinir. Ilk kurulum (bakir flash) yolu
     * nvram_init icinde acikca sifirdan baslatir. */
    uint32_t keep_sequence = nvram.sequence;

    memset(&nvram, 0, sizeof(nvram));

    /* Layout gecerlilik isaretleri - bunlar olmadan yazilan default bir
     * sonraki acilista yine default-reset'e dusardi. */
    nvram.magic = NVRAM_MAGIC;
    nvram.schema_version = NVRAM_SCHEMA_VERSION;
    nvram.length = (uint32_t)sizeof(nvram_t);
    nvram.sequence = keep_sequence;

    // Modem Config Defaults
	nvram.modem_config.serial_number = DEVICE_DEFAULT_SERIAL_NUMBER;
	nvram.modem_config.web_interface_port = 80;
	nvram.modem_config.web_first_data_timeout_sec = 45;
	nvram.modem_config.web_idle_timeout_sec = 0;
	nvram.modem_config.iec104_first_data_timeout_sec = 45;
	nvram.modem_config.iec104_idle_timeout_sec = 0;
	nvram.modem_config.sim_card_pin = 1234;
	strncpy(nvram.modem_config.apn.apn, "mgbs", sizeof(nvram.modem_config.apn.apn) - 1);
	strncpy(nvram.modem_config.apn.user_name, "", sizeof(nvram.modem_config.apn.user_name) - 1);
	strncpy(nvram.modem_config.apn.user_pass, "password", sizeof(nvram.modem_config.apn.user_pass) - 1);
	strncpy(nvram.modem_config.ntp_server, "pool.ntp.org", sizeof(nvram.modem_config.ntp_server) - 1);
	nvram.modem_config.ntp_server_port = 123;
	nvram.modem_config.time = 0;
	nvram.modem_config.time_zone = +3; // UTC
	nvram.modem_config.coordinates.mcc[0] = '\0';
	nvram.modem_config.coordinates.mnc[0] = '\0';
	nvram.modem_config.coordinates.lac[0] = '\0';
	nvram.modem_config.coordinates.ci[0] = '\0';
	nvram.modem_config.production_date = 0;
	nvram.modem_config.lifetime = 0;
	nvram.modem_config.periodic_modem_reset_period = 86400; // 24 hours
	nvram.modem_config.commissioning_time = 0;
	nvram.modem_config.rf_firmware_version[0] = 0;
	nvram.modem_config.rf_firmware_version[1] = 0;
	nvram.modem_config.rf_firmware_version[2] = 0;
	nvram.modem_config.rf_firmware_version[3] = 1;

    // IEC 104 Defaults
	uint32_t ip = 0;
	ipv4_to_int("188.59.76.59", &ip);
	nvram.iec104_config.scada_ip_address = ip;
	nvram.iec104_config.scada_port = 2404;
    nvram.iec104_config.originator_address = IEC104_DEFAULT_ORIGINATOR_ADDRESS;
    nvram.iec104_config.common_address = IEC104_DEFAULT_COMMON_ADDRESS;
    nvram.iec104_config.periodical_send_interval = IEC104_DEFAULT_PERIODICAL_SEND_INTERVAL;
    nvram.iec104_config.t0_max = IEC104_DEFAULT_T0_MAX;
    nvram.iec104_config.t1_max = IEC104_DEFAULT_T1_MAX;
    nvram.iec104_config.t2_max = IEC104_DEFAULT_T2_MAX;
    nvram.iec104_config.t3_max = IEC104_DEFAULT_T3_MAX;
    nvram.iec104_config.k_max = IEC104_DEFAULT_K_MAX;
    nvram.iec104_config.w_max = IEC104_DEFAULT_W_MAX;
    nvram.iec104_config.sbo_execute_timeout = IEC104_DEFAULT_SBO_EXECUTE_TIMEOUT;
    nvram.iec104_config.is_sbo_active = IEC104_DEFAULT_SBO_STATE;
    nvram.iec104_config.ioa_aku_uyarisi = iec104_make_ioa_3byte(10000);
    nvram.iec104_config.ioa_modem_reset = iec104_make_ioa_3byte(10001);

    /* Initialize all power lines with default values */
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        // Mark all lines as unused
        nvram.breaker.line[i].iec104.in_use = 0;
        nvram.breaker.line[i].modbus.in_use = 0;
        
        // Initialize IEC104 line config with default IOA values
        uint32_t base_ioa = 1000 + (i * 100);  // Line 0: 1000, Line 1: 1100, etc.
        for(int ph = 0; ph < PHASE_MAX; ++ph) 
		{
            nvram.breaker.line[i].iec104.ariza_akimi[ph] = iec104_make_ioa_3byte(base_ioa + ph);
            nvram.breaker.line[i].iec104.ariza_suresi[ph] = iec104_make_ioa_3byte(base_ioa + 10 + ph);
            nvram.breaker.line[i].iec104.ariza_kalicimi[ph] = iec104_make_ioa_3byte(base_ioa + 20 + ph);
            nvram.breaker.line[i].iec104.anlik_akim[ph] = iec104_make_ioa_3byte(base_ioa + 30 + ph);
            nvram.breaker.line[i].iec104.enerji_varyok[ph] = iec104_make_ioa_3byte(base_ioa + 40 + ph);
            nvram.breaker.line[i].iec104.nominal_akim_varyok[ph] = iec104_make_ioa_3byte(base_ioa + 50 + ph);
            nvram.breaker.line[i].iec104.rf_haberlesme_varyok[ph] = iec104_make_ioa_3byte(base_ioa + 60 + ph);
        }

        /* temporary/permanent fault: skaler alan bir BAZ adrestir; faz
         * (x20) ve kayit ofsetlerini tuketici hesaplar (iec104_config.c).
         * Faz dongusu icinde ph*10 ile atamak R/S degerlerini eziyordu
         * (Y3.12) - dongu disinda sabit baz atanir. */
        nvram.breaker.line[i].iec104.temporary_fault = iec104_make_ioa_3byte(base_ioa + 100);
        nvram.breaker.line[i].iec104.permanent_fault = iec104_make_ioa_3byte(base_ioa + 200);
        
        // Initialize Modbus line config with default register addresses.
        // Contiguous map (see MODBUS_REGISTER_MAP.md): FLOAT32 fields take two
        // registers (the configured address is the high word, address+1 the low
        // word), UINT16 fields take one. Per-line stride is 100.
        //   ariza_akimi  R/S/T -> base + 0 / 2 / 4   (FLOAT32)
        //   anlik_akim   R/S/T -> base + 6 / 8 / 10  (FLOAT32)
        //   ariza_suresi R/S/T -> base + 12 / 13 / 14 (UINT16, ms)
        //   ariza_kalicimi      -> base + 15 / 16 / 17 (UINT16)
        //   enerji_varyok       -> base + 18 / 19 / 20 (UINT16)
        //   nominal_akim_varyok -> base + 21 / 22 / 23 (UINT16)
        //   rf_haberlesme_varyok-> base + 24 / 25 / 26 (UINT16)
        uint16_t base_addr = 40000 + (i * 100);  // Line 0: 40000, Line 1: 40100, etc.
        for(int ph = 0; ph < PHASE_MAX; ++ph) 
		{
            nvram.breaker.line[i].modbus.ariza_akimi[ph] = base_addr + 0 + (ph * 2);
            nvram.breaker.line[i].modbus.anlik_akim[ph] = base_addr + 6 + (ph * 2);
            nvram.breaker.line[i].modbus.ariza_suresi[ph] = base_addr + 12 + ph;
            nvram.breaker.line[i].modbus.ariza_kalicimi[ph] = base_addr + 15 + ph;
            nvram.breaker.line[i].modbus.enerji_varyok[ph] = base_addr + 18 + ph;
            nvram.breaker.line[i].modbus.nominal_akim_varyok[ph] = base_addr + 21 + ph;
            nvram.breaker.line[i].modbus.rf_haberlesme_varyok[ph] = base_addr + 24 + ph;

            // Fault-log blocks are reserved (offsets 27..99) and not yet mapped.
            nvram.breaker.line[i].modbus.temporary_fault[ph].ariza_akimi = 0;
            nvram.breaker.line[i].modbus.temporary_fault[ph].ariza_suresi = 0;
        }
    }

    nvram.modbus_config.device_addr = 23;
    nvram.modbus_config.baud_rate = 115200;
    nvram.modbus_config.last_error_code = 0;
    nvram.modbus_config.addr_aku_uyarisi = 50000;
    nvram.modbus_config.addr_modem_reset = 50001;

    /* RF ayirici default'lari - spec R2 section 3 (codec default blogu tek
     * kaynak). in_use=false, fider=0 (provizyonsuz), EUI'ler atanmamis. */
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++) {
        rf_config_defaults(&nvram.breaker.line[i].rf);
    }

    nvram.gsm_log_level    = 2U; /* LOG_LVL_VERBOSE (gsm varsayilan) */
    nvram.rf_log_level     = 1U; /* LOG_LVL_NORMAL (ham paket dokumu VERBOSE'ta) */
    nvram.iec104_log_level = 1U; /* LOG_LVL_NORMAL (baglanti akisi VERBOSE'ta) */
}

void nvram_dump()
{
	CSLOG_NODT("\r\n");
	CSLOG_NODT("=============================================================================\r\n");
	CSLOG_NODT("                              NVRAM DUMP                                     \r\n");
	CSLOG_NODT("                         Total Size: %u bytes                               \r\n", sizeof(nvram));
	CSLOG_NODT("=============================================================================\r\n");
	CSLOG_NODT("\r\n");

	/* MODEM CONFIG SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  MODEM CONFIG [%u bytes]\r\n", sizeof(nvram.modem_config));
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  Serial Number              : %u\r\n", nvram.modem_config.serial_number);
	CSLOG_NODT("  Web Interface Port         : %u\r\n", nvram.modem_config.web_interface_port);
	CSLOG_NODT("  SIM Card PIN               : %u\r\n", nvram.modem_config.sim_card_pin);
	CSLOG_NODT("  SIM Card APN               : %.16s\r\n", nvram.modem_config.apn.apn);
	CSLOG_NODT("  SIM Card APN Username      : %.16s\r\n", nvram.modem_config.apn.user_name);
	CSLOG_NODT("  SIM Card APN Password      : %.16s\r\n", nvram.modem_config.apn.user_pass);
	CSLOG_NODT("  NTP Server Address         : %.64s\r\n", nvram.modem_config.ntp_server);
	CSLOG_NODT("  NTP Server Port            : %u\r\n", nvram.modem_config.ntp_server_port);
	CSLOG_NODT("  Time Zone                  : %d\r\n", nvram.modem_config.time_zone);
	CSLOG_NODT("  Time (Epoch)               : %u\r\n", nvram.modem_config.time);
	CSLOG_NODT("  Coordinates:\r\n");
	CSLOG_NODT("    - MCC                    : %.4s\r\n", nvram.modem_config.coordinates.mcc);
	CSLOG_NODT("    - MNC                    : %.4s\r\n", nvram.modem_config.coordinates.mnc);
	CSLOG_NODT("    - LAC                    : %.4s\r\n", nvram.modem_config.coordinates.lac);
	CSLOG_NODT("    - CI                     : %.4s\r\n", nvram.modem_config.coordinates.ci);
	CSLOG_NODT("  Production Date (Epoch)    : %u\r\n", nvram.modem_config.production_date);
	CSLOG_NODT("  Lifetime (seconds)         : %u\r\n", nvram.modem_config.lifetime);
	CSLOG_NODT("  Periodic Reset Period (s)  : %u\r\n", nvram.modem_config.periodic_modem_reset_period);
	CSLOG_NODT("  Commissioning Time (Epoch) : %u\r\n", nvram.modem_config.commissioning_time);
	CSLOG_NODT("  RF Firmware Version        : %u.%u.%u.%u\r\n",
			nvram.modem_config.rf_firmware_version[0],
			nvram.modem_config.rf_firmware_version[1],
			nvram.modem_config.rf_firmware_version[2],
			nvram.modem_config.rf_firmware_version[3]);
	CSLOG_NODT("  WEB Session Counter            : %u\r\n", nvram.modem_config.web_session_counter);
	CSLOG_NODT("  IEC Session Counter            : %u\r\n", nvram.modem_config.iec_session_counter);
	CSLOG_NODT("\r\n");

	/* IEC104 CONFIG SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  IEC104 CONFIG [%u bytes]\r\n", sizeof(nvram.iec104_config));
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  SCADA IP Address           : %u\r\n", nvram.iec104_config.scada_ip_address);
	CSLOG_NODT("  SCADA Port                 : %u\r\n", nvram.iec104_config.scada_port);
	CSLOG_NODT("  Originator Address         : %u\r\n", nvram.iec104_config.originator_address);
	CSLOG_NODT("  Common Address             : %u\r\n", nvram.iec104_config.common_address);
	CSLOG_NODT("  Periodical Send Interval   : %u\r\n", nvram.iec104_config.periodical_send_interval);
	CSLOG_NODT("  Timing Parameters:\r\n");
	CSLOG_NODT("    - T0 Max (Connect)       : %u\r\n", nvram.iec104_config.t0_max);
	CSLOG_NODT("    - T1 Max (ACK)           : %u\r\n", nvram.iec104_config.t1_max);
	CSLOG_NODT("    - T2 Max (Recv ACK)      : %u\r\n", nvram.iec104_config.t2_max);
	CSLOG_NODT("    - T3 Max (Idle)          : %u\r\n", nvram.iec104_config.t3_max);
	CSLOG_NODT("  Window Parameters:\r\n");
	CSLOG_NODT("    - K Max (Send Window)    : %u\r\n", nvram.iec104_config.k_max);
	CSLOG_NODT("    - W Max (Recv Window)    : %u\r\n", nvram.iec104_config.w_max);
	CSLOG_NODT("  SBO Parameters:\r\n");
	CSLOG_NODT("    - Is SBO Active          : %s\r\n", nvram.iec104_config.is_sbo_active ? "Yes" : "No");
	CSLOG_NODT("    - SBO Execute Timeout    : %u\r\n", nvram.iec104_config.sbo_execute_timeout);
	CSLOG_NODT("  CRC                        : 0x%08X\r\n", nvram.iec104_config.crc);
	CSLOG_NODT("\r\n");

	/* MODBUS CONFIG SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  MODBUS CONFIG [%u bytes]\r\n", sizeof(nvram.modbus_config));
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  Device Address             : %u\r\n", nvram.modbus_config.device_addr);
	CSLOG_NODT("  Baud Rate                  : %u\r\n", nvram.modbus_config.baud_rate);
	CSLOG_NODT("  Last Error Code            : %u\r\n", nvram.modbus_config.last_error_code);
	CSLOG_NODT("\r\n");


	/* BREAKER CONFIG SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  BREAKER CONFIG [%u bytes]\r\n", sizeof(nvram.breaker));
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  CRC                        : 0x%08X\r\n", nvram.breaker.crc);
	CSLOG_NODT("\r\n");

	bool any_line_in_use = false;
	for (int i = 0; i < MAX_POWER_LINE_COUNT; i++) {
		if (nvram.breaker.line[i].iec104.in_use == 0) {
			continue;
		}
		any_line_in_use = true;

		CSLOG_NODT("  ---------------------------------------------------------------------\r\n");
		CSLOG_NODT("  LINE %d [%u bytes]\r\n", i, sizeof(power_line_t));
		CSLOG_NODT("  ---------------------------------------------------------------------\r\n");
		CSLOG_NODT("    In Use                   : %s\r\n", nvram.breaker.line[i].iec104.in_use ? "Yes" : "No");
		CSLOG_NODT("    Breaker State            : %u\r\n", nvram.breaker.line[i].breaker_state);
		CSLOG_NODT("    SBO State:\r\n");
		CSLOG_NODT("      - State                : %s\r\n", nvram.breaker.line[i].sbo_state.state == SBO_IDLE ? "IDLE" : "SELECTED");
		CSLOG_NODT("      - Value                : %u\r\n", nvram.breaker.line[i].sbo_state.value);
		CSLOG_NODT("      - Select Time          : %u\r\n", nvram.breaker.line[i].sbo_state.select_time);
		CSLOG_NODT("    IEC104 IOA Config:\r\n");
		CSLOG_NODT("      M_SP_TB_1 (Switch State):\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_sp_tb_1_ioa[PHASE_L1]));
		CSLOG_NODT("        - Phase S            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_sp_tb_1_ioa[PHASE_L2]));
		CSLOG_NODT("        - Phase T            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_sp_tb_1_ioa[PHASE_L3]));
		CSLOG_NODT("      M_ME_TF_1 (Current Meas):\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_me_tf_1_ioa[PHASE_L1]));
		CSLOG_NODT("        - Phase S            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_me_tf_1_ioa[PHASE_L2]));
		CSLOG_NODT("        - Phase T            : %u\r\n", ioa_to_u32(nvram.breaker.line[i].iec104.m_me_tf_1_ioa[PHASE_L3]));
		CSLOG_NODT("    Modbus Address Config:\r\n");
		CSLOG_NODT("      Ariza Akimi Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.ariza_akimi[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.ariza_akimi[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.ariza_akimi[PHASE_L3]);
		CSLOG_NODT("      Ariza Suresi Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.ariza_suresi[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.ariza_suresi[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.ariza_suresi[PHASE_L3]);
		CSLOG_NODT("      Ariza Kalicimi Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.ariza_kalicimi[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.ariza_kalicimi[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.ariza_kalicimi[PHASE_L3]);
		CSLOG_NODT("      Anlik Akim Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.anlik_akim[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.anlik_akim[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.anlik_akim[PHASE_L3]);
		CSLOG_NODT("      Enerji VarYok Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.enerji_varyok[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.enerji_varyok[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.enerji_varyok[PHASE_L3]);
		CSLOG_NODT("      Nominal Akim VarYok Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.nominal_akim_varyok[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.nominal_akim_varyok[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.nominal_akim_varyok[PHASE_L3]);
		CSLOG_NODT("      RF Haberlesme VarYok Addr:\r\n");
		CSLOG_NODT("        - Phase R            : %u\r\n", nvram.breaker.line[i].modbus.rf_haberlesme_varyok[PHASE_L1]);
		CSLOG_NODT("        - Phase S            : %u\r\n", nvram.breaker.line[i].modbus.rf_haberlesme_varyok[PHASE_L2]);
		CSLOG_NODT("        - Phase T            : %u\r\n", nvram.breaker.line[i].modbus.rf_haberlesme_varyok[PHASE_L3]);
		CSLOG_NODT("\r\n");

		/* Phase details */
			const char* phase_name = "R-S-T";
			rf_feeder_t* rf_cfg = &nvram.breaker.line[i].rf;
			char eui_hex[RF_EUI64_HEX_LEN];

			CSLOG_NODT("      - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\r\n");
			CSLOG_NODT("      RF Config %s\r\n", phase_name);
			CSLOG_NODT("      - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\r\n");
			CSLOG_NODT("      Config:\r\n");
			rf_eui64_to_hex(rf_cfg->r_eui64, eui_hex);
			CSLOG_NODT("        - Device EUI-64 (L1)      : %s\r\n", eui_hex);
			rf_eui64_to_hex(rf_cfg->s_eui64, eui_hex);
			CSLOG_NODT("        - Device EUI-64 (L2)      : %s\r\n", eui_hex);
			rf_eui64_to_hex(rf_cfg->t_eui64, eui_hex);
			CSLOG_NODT("        - Device EUI-64 (L3)      : %s\r\n", eui_hex);
			CSLOG_NODT("        - Line ID                : %u\r\n", rf_cfg->config.fider_id);
			CSLOG_NODT("        - Zone ID                : %u\r\n", rf_cfg->config.zone_id);
			CSLOG_NODT("        - Mode                   : %u\r\n", rf_cfg->config.operating_mode);
			CSLOG_NODT("        - Trip Mode              : %u\r\n", rf_cfg->config.trip_mode);
			CSLOG_NODT("        - Line Frequency (Hz)    : %u\r\n", rf_cfg->config.line_frequency);
			CSLOG_NODT("        - Nominal Current (A)    : %.1f\r\n", rf_cfg->config.nominal_current);
			CSLOG_NODT("        - Opening Current Thr.   : %.1f\r\n", rf_cfg->config.ia_threshold);
			CSLOG_NODT("        - Safety Current Thr.(A) : %.3f\r\n", rf_cfg->config.is_safety);
			CSLOG_NODT("        - Incremental Curr (A/s) : %.1f\r\n", rf_cfg->config.di_dt_threshold);
			CSLOG_NODT("        - Dead Line Current (A)  : %.2f\r\n", rf_cfg->config.line_break_threshold);
			CSLOG_NODT("        - Dead Line Valid (ms)   : %u\r\n", rf_cfg->config.dead_line_verify_ms);
			CSLOG_NODT("        - Refresh Reset Time (s) : %u\r\n", rf_cfg->config.t_reclaim_sec);
			CSLOG_NODT("        - Opening Errors Count   : %u\r\n", rf_cfg->config.set_count);
			CSLOG_NODT("\r\n");
	}

	if (!any_line_in_use) {
		CSLOG_NODT("  No lines are currently in use.\r\n");
	}

	/* CRC SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  NVRAM Version/Length/Seq    : v%u / %u / %u\r\n",
	           nvram.schema_version, nvram.length, nvram.sequence);
	CSLOG_NODT("  NVRAM CRC                  : 0x%08X\r\n", nvram.crc);
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("\r\n");
}

/* Slot basligi - nvram_t'nin ilk 16 bayti ile ayni sirada
 * (types.h'teki offsetof assert'leri bu sozlesme korur). */
typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t length;
    uint32_t sequence;
} nvram_hdr_t;

static void nvram_read_hdr(uint32_t addr, nvram_hdr_t *hdr)
{
    w25qxx_read_buff(addr, hdr, sizeof(nvram_hdr_t));
}

/* Header akil sagligi kontrolu: magic + uzunluk sinirlari (ucuz on bakis;
 * tam dogrulama nvram_validate_slot'in CRC akisindadir). */
static bool nvram_hdr_sane(const nvram_hdr_t *hdr)
{
    return (hdr->magic == NVRAM_MAGIC) &&
           (hdr->length >= (uint32_t)(offsetof(nvram_t, crc) + 4U)) &&
           (hdr->length <= (uint32_t)sizeof(nvram_t));
}

/* ---- Kucuk yardimcilar (doc/nvram.md tablosu) ------------------------ */

/* Slotu AKISKAN dogrula: header -> sema -> CRC. Goruntu RAM'e YUKLENMEZ;
 * 256 B sayfa tamponuyla okunur, CRC artimli hesaplanir - boylece ikinci
 * tam boy goruntuye gerek kalmaz ve dogrulama fiziksel readback olur. */
static bool nvram_validate_slot(uint32_t slot, nvram_slot_state_t *st)
{
    const uint32_t addr = nvram_slot_addr[slot];
    nvram_hdr_t hdr;
    crc32_t crc;
    uint32_t off;
    uint32_t remaining;
    uint32_t chunk;
    uint32_t stored_crc;

    *st = (nvram_slot_state_t){0};   /* gecersiz durumda alanlar da tanimli olsun */

    nvram_read_hdr(addr, &hdr);
    if (!nvram_hdr_sane(&hdr))
    {
        return false;
    }
    if (hdr.schema_version != NVRAM_SCHEMA_VERSION)
    {
        return false;
    }

    crc = crc32_init();
    off = 0U;
    remaining = hdr.length - 4U;

    while (remaining > 0U)
    {
        chunk = (remaining > (uint32_t)sizeof(nvram_page_buf))
                    ? (uint32_t)sizeof(nvram_page_buf) : remaining;
        w25qxx_read_buff(addr + off, nvram_page_buf, chunk);
        crc = crc32_update(crc, nvram_page_buf, chunk);
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
static bool nvram_slots_equal(const nvram_slot_state_t *a,
                              const nvram_slot_state_t *b)
{
    return a->valid && b->valid
           && (a->sequence == b->sequence)
           && (a->crc == b->crc);
}

/* Global RAM goruntusunu slota yaz ve dogrula. Surucu erase-once yazar ve
 * fiziksel readback ile dogrular: tek cagri = tek sektor silme + dogrulama. */
static int nvram_write_slot(uint32_t slot)
{
    if (w25qxx_write_buff(nvram_slot_addr[slot], &nvram, sizeof(nvram))
            != W25QXX_RES_OK)
    {
        return -1;
    }
    return 0;
}

/* Flash'tan flash'a slot kopyasi: hedefi BIR kez sil, sayfa sayfa programla,
 * ardindan akiskan dogrula (fiziksel readback). Global RAM'e DOKUNMAZ -
 * kaydedilmemis ayarlar korunur. Kopya, kaynak goruntuyu AYNI sequence ile
 * tasiyacak bicimde birebir kopyalar. */
static int nvram_copy_slot(uint32_t dst, uint32_t src)
{
    const uint32_t dst_addr = nvram_slot_addr[dst];
    const uint32_t src_addr = nvram_slot_addr[src];
    nvram_slot_state_t st;
    uint32_t off;
    uint32_t chunk;

    if (w25qxx_erase_sector(dst_addr) != W25QXX_RES_OK)
    {
        return -1;
    }

    for (off = 0U; off < (uint32_t)sizeof(nvram_t);
         off += (uint32_t)sizeof(nvram_page_buf))
    {
        chunk = (uint32_t)sizeof(nvram_t) - off;
        if (chunk > (uint32_t)sizeof(nvram_page_buf))
        {
            chunk = (uint32_t)sizeof(nvram_page_buf);
        }
        w25qxx_read_buff(src_addr + off, nvram_page_buf, chunk);
        if (w25qxx_page_write(dst_addr + off, nvram_page_buf, chunk)
                != W25QXX_RES_OK)
        {
            return -1;
        }
    }

    if (!nvram_validate_slot(dst, &st))
    {
        return -1;
    }
    return 0;
}

/* Faz 1 - yedek tamamlama: A silinmeden ONCE her iki slot elden gecirilir.
 * Tum kopyalar flash->flash yapilir; global RAM goruntusune dokunulmaz.
 * Donus -1: tek saglam kopya dogrulanmadan kayda gecilmez. */
static int nvram_prepare_slots(nvram_slot_state_t *a, nvram_slot_state_t *b)
{
    /* Yalniz B gecerliyse (ya da B daha tazeyse): once A'yi B'den kur -
     * boylece kayit A'yi silerken B eldeki guncel goruntuyu tasir. */
    if (b->valid && (!a->valid || nvram_seq_newer(b->sequence, a->sequence)))
    {
        if (nvram_copy_slot(NVRAM_SLOT_A, NVRAM_SLOT_B) != 0)
        {
            return -1;
        }
        (void)nvram_validate_slot(NVRAM_SLOT_A, a);
    }

    /* A gecerli ve B onun aynisi degilse: B'yi A'dan tazele. */
    if (a->valid && !nvram_slots_equal(a, b))
    {
        if (nvram_copy_slot(NVRAM_SLOT_B, NVRAM_SLOT_A) != 0)
        {
            return -1;
        }
        (void)nvram_validate_slot(NVRAM_SLOT_B, b);
    }
    return 0;
}

/* ---- Acilis ----------------------------------------------------------- */

int nvram_init(void)
{
#ifdef IEC104_TEST
    nvram_test_fill();
#endif
    nvram_slot_state_t a;
    nvram_slot_state_t b;
    int res = 0;

    (void)nvram_validate_slot(NVRAM_SLOT_A, &a);
    (void)nvram_validate_slot(NVRAM_SLOT_B, &b);

    if (a.valid || b.valid)
    {
        /* En guncel GECERLI goruntuyu sec ve global yapiya yukle. */
        uint32_t load_slot = NVRAM_SLOT_A;

        if (!a.valid)
        {
            load_slot = NVRAM_SLOT_B;
        }
        else if (b.valid && nvram_seq_newer(b.sequence, a.sequence))
        {
            load_slot = NVRAM_SLOT_B;
        }

        w25qxx_read_buff(nvram_slot_addr[load_slot], &nvram, sizeof(nvram));
        nvram_persisted_crc = nvram.crc;

        if (load_slot == NVRAM_SLOT_B)
        {
            elog_log_nvram_recovery(ELOG_NVRAM_RESTORED_FROM_BACKUP,
                                    a.sequence, b.sequence);
            CSLOG("NVRAM yedek slottan yuklendi (ana sequence=%u, yedek=%u).\r\n",
                  (unsigned)a.sequence, (unsigned)b.sequence);
        }
        else
        {
            CSLOG("NVRAM loaded successfully.\r\n");
        }

        /* Acilis onarimi: iki kopya da ayni dogrulanmis goruntuyu tasin.
         * Basarisizsa ayarlar okunabilir kalir; sonraki nvram_sync,
         * onarimi Faz 1'de tamamlamadan A'yi silmez. */
        if (nvram_prepare_slots(&a, &b) != 0)
        {
            CSLOG_ERR("NVRAM: slot repair at boot FAILED - settings usable, "
                      "repair retried on next sync.\r\n");
        }
    }
    else
    {
        xcprintf(XCOLOR_RED, "NVRAM: kullanilabilir slot yok - defaults\r\n");
        nvram_set_defaults();
        nvram.sequence = 0U;    /* ilk kurulum: bakir flash sifirdan baslar */
        elog_log_nvram_recovery(ELOG_NVRAM_DEFAULTS_REWRITTEN, 0U, 0U);
        if (nvram_sync(true) != 0)
        {
            xcprintf(XCOLOR_RED, "Failed to write default NVRAM values to flash.\r\n");
            res = -1;
        }
    }

    console_logger_init();   /* on/off + modul seviyelerini NVRAM'den yukler */
    nvram_dump();

    return res;
}

/* ---- Save ------------------------------------------------------------- */

static int nvram_sync_locked(bool crc_no_check)
{
    nvram_slot_state_t a;
    nvram_slot_state_t b;
    crc32_t ram_crc = nvram_calculate_crc();

    (void)nvram_validate_slot(NVRAM_SLOT_A, &a);
    (void)nvram_validate_slot(NVRAM_SLOT_B, &b);

    /* Faz 1 - yedek tamamlama (flash->flash; RAM goruntusu donuk). */
    if (nvram_prepare_slots(&a, &b) != 0)
    {
        xcprintf(XCOLOR_RED,
                 "NVRAM: slot prepare FAILED - save rejected, good copy untouched.\r\n");
        return -1;
    }

    if (!a.valid && !b.valid)
    {
        /* Bakir kurulum akisi: defaults init'ten buraya seq=0 ile gelir. */
    }
    else if (!crc_no_check && (ram_crc == nvram_persisted_crc))
    {
        /* Veri degismedi; slotlar Faz 1'de esitlendi - gereksiz yazma yok. */
        return 0;
    }
    else if (a.valid && (a.crc == ram_crc))
    {
        /* Retry tamamlanmasi: A zaten RAM goruntusunun AYNISINI tasiyor ve
         * B de Faz 1'de esitlendi - yeniden yazmaya gerek yok. */
        nvram.crc = ram_crc;
        nvram.sequence = a.sequence;
        nvram_persisted_crc = ram_crc;
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
            base = nvram_seq_newer(a.sequence, b.sequence) ? a.sequence : b.sequence;
        }
        else if (a.valid)
        {
            base = a.sequence;
        }
        else if (b.valid)
        {
            base = b.sequence;
        }
        nvram.sequence = base + 1U;
    }
    nvram.length = (uint32_t)sizeof(nvram_t);
    nvram.crc = nvram_calculate_crc();

    /* A once: A silinmeden once B, mevcut goruntunun dogrulanmis kopyasini
     * tasir (Faz 1). A yazilamazsa B saglam kalir. */
    if (nvram_write_slot(NVRAM_SLOT_A) != 0)
    {
        xcprintf(XCOLOR_RED,
                 "NVRAM: slot A write FAILED - B keeps the last verified image.\r\n");
        return -1;
    }

    /* B sonra: B silinirken A yeni goruntunun dogrulanmis kopyasini tasir. */
    if (nvram_write_slot(NVRAM_SLOT_B) != 0)
    {
        xcprintf(XCOLOR_RED,
                 "NVRAM: slot B write FAILED - A holds the new verified image.\r\n");
        return -1;
    }

    nvram_persisted_crc = nvram.crc;
    return 0;
}

int nvram_sync(bool crc_no_check)
{
    int res;

    if (nvram_busy)
    {
        /* Ic ice lock yasak: tek sahip kurali. */
        CSLOG_ERR("NVRAM: sync re-entry rejected (save in progress).\r\n");
        return -1;
    }

    nvram_busy = true;
    res = nvram_sync_locked(crc_no_check);
    nvram_busy = false;
    return res;
}

bool nvram_is_busy(void)
{
    return nvram_busy;
}

bool nvram_is_cslog_enabled(void)
{
    return (bool)nvram.cslog_enabled;
}

/* Save/onarim sirasinda RAM goruntusu donuktur: setter'lar mutasyonu
 * reddeder (doc: kayit beklerken goruntunun degistirilmesine izin verilmez). */
static bool nvram_change_allowed(void)
{
    if (nvram_busy)
    {
        CSLOG_ERR("NVRAM: change rejected - save/repair in progress.\r\n");
        return false;
    }
    return true;
}

void nvram_set_cslog_enabled(bool enabled)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.cslog_enabled = (uint8_t)enabled;
}

uint8_t nvram_get_gsm_log_level(void)
{
	return nvram.gsm_log_level;
}

void nvram_set_gsm_log_level(uint8_t level)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.gsm_log_level = level;
}

uint8_t nvram_get_rf_log_level(void)
{
	return nvram.rf_log_level;
}

void nvram_set_rf_log_level(uint8_t level)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.rf_log_level = level;
}

uint8_t nvram_get_iec104_log_level(void)
{
	return nvram.iec104_log_level;
}

void nvram_set_iec104_log_level(uint8_t level)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.iec104_log_level = level;
}


void nvram_set_is_sbo_active(bool is_active)
{
    if (!nvram_change_allowed())
    {
        return;
    }
    nvram.iec104_config.is_sbo_active = is_active;
}

bool nvram_get_is_sbo_active(void)
{
    return nvram.iec104_config.is_sbo_active;
}

void nvram_set_sbo_execute_timeout(uint16_t timeout)
{
    if (!nvram_change_allowed())
    {
        return;
    }
    nvram.iec104_config.sbo_execute_timeout = timeout;
}

uint16_t nvram_get_sbo_execute_timeout(void)
{
    return nvram.iec104_config.sbo_execute_timeout;
}

breaker_t* nvram_get_breaker(void)
{
    return &nvram.breaker;
}

int nvram_set_breaker(const breaker_t* breaker)
{
    if (!nvram_change_allowed())
    {
        return -1;
    }
    if (breaker == NULL)
    {
        return -1; // Invalid parameter
    }

    nvram.breaker = *breaker;
    return 0;
}

uint32_t nvram_get_device_serial_number(void)
{
    return nvram.modem_config.serial_number;
}

void nvram_set_device_serial_number(uint32_t serial_number)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.modem_config.serial_number = serial_number;
}

void nvram_set_modbus_device_addr(uint8_t slave_id)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	nvram.modbus_config.device_addr = slave_id;
}

uint8_t nvram_get_modbus_device_addr(void)
{
	return nvram.modbus_config.device_addr;
}
 

int nvram_set_m_sp_na_1_ioa(uint32_t line_index, uint8_t phase, uint32_t ioa)
{
    if (!nvram_change_allowed())
    {
        return -1;
    }
    if (line_index >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return -1; // Invalid parameters
    }

    nvram.breaker.line[line_index].iec104.m_sp_tb_1_ioa[phase] = iec104_make_ioa_3byte(ioa);
    return 0;
}

ioa_3byte_t nvram_get_m_sp_na_1_ioa(uint32_t line_index, uint8_t phase)
{
    if (line_index >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return (ioa_3byte_t){0}; // Invalid parameters
    }

    return nvram.breaker.line[line_index].iec104.m_sp_tb_1_ioa[phase];
}

int nvram_set_m_me_tf_1_ioa(uint32_t line_index, uint8_t phase, uint32_t ioa)
{
    if (!nvram_change_allowed())
    {
        return -1;
    }
    if (line_index >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return -1; // Invalid parameters
    }

    nvram.breaker.line[line_index].iec104.m_me_tf_1_ioa[phase] = iec104_make_ioa_3byte(ioa);
    return 0;
}

ioa_3byte_t nvram_get_m_me_tf_1_ioa(uint32_t line_index, uint8_t phase)
{
    if (line_index >= MAX_POWER_LINE_COUNT || phase >= PHASE_MAX) {
        return (ioa_3byte_t){0}; // Invalid parameters
    }

    return nvram.breaker.line[line_index].iec104.m_me_tf_1_ioa[phase];
}

int nvram_set_ioa(uint8_t type_id, uint32_t line_index, uint8_t phase, uint32_t ioa)
{
    if (!nvram_change_allowed())
    {
        return -1;
    }
    switch (type_id)
    {
    case M_SP_NA_1:
        return nvram_set_m_sp_na_1_ioa(line_index, phase, ioa);
    case M_ME_TF_1:
        return nvram_set_m_me_tf_1_ioa(line_index, phase, ioa);
    default:
        return -1; // Invalid type_id
    }
}

int nvram_get_line_config(uint32_t line_index, iec104_line_config_t* config)
{
    if (line_index >= MAX_POWER_LINE_COUNT || config == NULL) {
        // Handle invalid line index or null pointer
        return -1;
    }
    *config = nvram.breaker.line[line_index].iec104;
    return 0;
}

int nvram_set_line_config(uint32_t line_index, const iec104_line_config_t* config, bool sync)
{
    if (!nvram_change_allowed())
    {
        return -1;
    }
    if (line_index >= MAX_POWER_LINE_COUNT || config == NULL) {
        // Handle invalid line index or null pointer
        return -1;
    }

    nvram.breaker.line[line_index].iec104 = *config;

    if(sync) {
        nvram_sync(false);
    }

    return 0;
}   

/////////////////////////////////////////////////////////////////////////
/////////////////// MODEM CONFIG GETTERS/SETTERS START //////////////////
/////////////////////////////////////////////////////////////////////////
 
const modem_config_t *nvram_get_modem_config(void)
{
    return &nvram.modem_config;
}

modem_config_t *nvram_get_modem_config_rw(void)
{
    return &nvram.modem_config;
}

void nvram_set_modem_config(const modem_config_t *info)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	if(info) {
		nvram.modem_config = *info;
	}
}

breaker_t *nvram_get_breaker_rw(void)
{
	return &nvram.breaker;
}

/////////////////// MODEM CONFIG GETTERS/SETTERS END ////////////////////
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////




const iec104_config_t *nvram_get_iec104_config(void)
{
	return &nvram.iec104_config;
}

iec104_config_t *nvram_get_iec104_config_rw(void)
{
	return &nvram.iec104_config;
}

iec104_evtlog_state_t *nvram_get_iec104_evtlog_state(void)
{
	return &nvram.iec104_evtlog;
}

void nvram_set_iec104_config(const iec104_config_t *cfg)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	if(cfg) {
		nvram.iec104_config = *cfg;
		nvram_sync(false);
	}
}

bool nvram_is_power_line_in_use(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return false;
	}

	return (nvram.breaker.line[line_index].iec104.in_use != 0);
}

const iec104_line_config_t* nvram_iec104_get_line_config(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}

	return &nvram.breaker.line[line_index].iec104;
}

bool nvram_iec104_set_line_config(uint32_t line_index, const iec104_line_config_t* config)
{
	if(line_index >= MAX_POWER_LINE_COUNT || config == NULL) {
		return false;
	}

	nvram.breaker.line[line_index].iec104 = *config;
	return true;
}

const modbus_line_config_t* nvram_modbus_get_line_config(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}

	return &nvram.breaker.line[line_index].modbus;
}

bool nvram_modbus_set_line_config(uint32_t line_index, const modbus_line_config_t* config)
{
	if(line_index >= MAX_POWER_LINE_COUNT || config == NULL) {
		return false;
	}

	nvram.breaker.line[line_index].modbus = *config;
	return true;
}

const modbus_configs_t *nvram_get_modbus_config(void)
{
	return &nvram.modbus_config;
}

modbus_configs_t *nvram_get_modbus_config_rw(void)
{
	return &nvram.modbus_config;
}

void nvram_set_modbus_config(const modbus_configs_t *cfg)
{
    if (!nvram_change_allowed())
    {
        return;
    }
	if(cfg) {
		nvram.modbus_config = *cfg;
		nvram_sync(false);
	}
}

/* ── RFWU session ───────────────────────────────────────────────── */

const rfwu_nvram_t *nvram_get_rfwu(void)
{
    return &nvram.rfwu;
}

void nvram_set_rfwu(const rfwu_nvram_t *p_rfwu)
{
    if (!nvram_change_allowed())
    {
        return;
    }
    if (p_rfwu == NULL) { return; }
    nvram.rfwu = *p_rfwu;
    (void)nvram_sync(false);
}
