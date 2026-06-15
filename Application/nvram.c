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
#include "gsm_log.h"

static nvram_t   nvram = {0};


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
    memset(&nvram, 0, sizeof(nvram));

    // Modem Config Defaults
	nvram.modem_config.serial_number = DEVICE_DEFAULT_SERIAL_NUMBER;
	nvram.modem_config.web_interface_port = 80;
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

            nvram.breaker.line[i].iec104.temporary_fault = iec104_make_ioa_3byte(base_ioa + 100 + (ph * 10));
            nvram.breaker.line[i].iec104.permanent_fault = iec104_make_ioa_3byte(base_ioa + 200 + (ph * 10));
        }
        
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

    nvram.gsm_log_level = 2U; /* GSM_LOG_VERBOSE */
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
			rf_config_t* rf_cfg = &nvram.breaker.line[i].rf_config;

			CSLOG_NODT("      - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\r\n");
			CSLOG_NODT("      RF Config %s\r\n", phase_name);
			CSLOG_NODT("      - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\r\n");
			CSLOG_NODT("      Config:\r\n");
			CSLOG_NODT("        - Device ID (R)          : %u\r\n", rf_cfg->r_device_id);
			CSLOG_NODT("        - Device ID (S)          : %u\r\n", rf_cfg->s_device_id);
			CSLOG_NODT("        - Device ID (T)          : %u\r\n", rf_cfg->t_device_id);
			CSLOG_NODT("        - Line ID                : %u\r\n", rf_cfg->hat_id);
			CSLOG_NODT("        - Zone ID                : %u\r\n", rf_cfg->zone_id);
			CSLOG_NODT("        - Mode                   : %u\r\n", rf_cfg->mode);
			CSLOG_NODT("        - Line Frequency (Hz)    : %u\r\n", rf_cfg->hat_frekansi);
			CSLOG_NODT("        - Nominal Current (A)    : %u\r\n", rf_cfg->sistem_nominal_akimi);
			CSLOG_NODT("        - Opening Current Thr.   : %u\r\n", rf_cfg->set_edilebilir_actirma_esik_akimi);
			CSLOG_NODT("        - Incremental Curr Thr.  : %u\r\n", rf_cfg->artimli_akim_esigi);
			CSLOG_NODT("        - Dead Line Current      : %u\r\n", rf_cfg->hat_kopuk_hat_bosta);
			CSLOG_NODT("        - Dead Line Valid Period : %u\r\n", rf_cfg->olu_hat_akimi_dogrulama_suresi);
			CSLOG_NODT("        - Refresh Reset Time (s) : %u\r\n", rf_cfg->yenilenme_sifirlama_suresi);
			CSLOG_NODT("        - Opening Errors Count   : %u\r\n", rf_cfg->set_edilebilir_acma_ariza_sayisi);
			CSLOG_NODT("\r\n");
	}

	if (!any_line_in_use) {
		CSLOG_NODT("  No lines are currently in use.\r\n");
	}

	/* CRC SECTION */
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("  NVRAM CRC                  : 0x%08X\r\n", nvram.crc);
	CSLOG_NODT("-----------------------------------------------------------------------------\r\n");
	CSLOG_NODT("\r\n");
}

static bool is_nvram_empty_or_uninitialized(void)
{
	const uint8_t* nvram_ptr = (const uint8_t*)&nvram;
	for(int i = 0; i < sizeof(nvram); i++){
		if(nvram_ptr[i] != 0xFF){
			return 0; // Not empty
		}
	}
	return 1; // Empty
}

int nvram_init(void)
{
#ifdef IEC104_TEST
    nvram_test_fill();
#endif

	int res = 0;

    w25qxx_read_buff(NVRAM_ADDRESS, &nvram, sizeof(nvram));
    crc32_t crc = nvram_calculate_crc();

    if(crc != nvram.crc)
    {
        xcprintf(XCOLOR_RED, "NVRAM CRC mismatch: stored=0x%08X, calculated=0x%08X\r\n", nvram.crc, crc);

    	bool is_main_nvram_empty_or_uninitialized = is_nvram_empty_or_uninitialized();
		if(is_main_nvram_empty_or_uninitialized){
			CSLOG("NVRAM uninitialized.\n");
		}
    	w25qxx_read_buff(NVRAM_BACKUP_ADDRESS, &nvram, sizeof(nvram));
    	crc = nvram_calculate_crc();

    	if(crc != nvram.crc)
    	{
    		xcprintf(XCOLOR_RED, "NVRAM backup CRC mismatch: stored=0x%08X, calculated=0x%08X\r\n", nvram.crc, crc);

    		bool is_backup_nvram_empty_or_uninitialized = is_nvram_empty_or_uninitialized();
        	if(is_backup_nvram_empty_or_uninitialized){
				CSLOG("NVRAM backup uninitialized.\r\n");
			}

			nvram_set_defaults();
			if(nvram_sync(true)){
				xcprintf(XCOLOR_RED, "Failed to write default NVRAM values to flash.\r\n");
				res = -1;
			}
    	}
    	else
    	{
    		CSLOG("NVRAM restored from backup.\r\n");
    	}
	}
    else
    {
		CSLOG("NVRAM loaded successfully.\r\n");
	}

    console_logger_init();
    gsm_log_init();
    nvram_dump();

    return res;
}

int nvram_sync(bool crc_no_check)
{
	crc32_t crc = nvram_calculate_crc();

    if(!crc_no_check && crc == nvram.crc)
	{
		xcprintf(XCOLOR_YELLOW, "NVRAM CRC unchanged, skipping write.\r\n");
		return 0;
	}

    nvram.crc = crc;
    int res1 = w25qxx_write_buff(NVRAM_ADDRESS, &nvram, sizeof(nvram));
    int res2 = w25qxx_write_buff(NVRAM_BACKUP_ADDRESS, &nvram, sizeof(nvram));

    CSLOG("NVRAM synchronized\r\n");
    return (res1 != W25QXX_RES_OK || res2 != W25QXX_RES_OK) ? -1 : 0;
}

bool nvram_is_cslog_enabled(void)
{
	return (bool)nvram.cslog_enabled;
}

void nvram_set_cslog_enabled(bool enabled)
{
	nvram.cslog_enabled = (uint8_t)enabled;
}

uint8_t nvram_get_gsm_log_level(void)
{
	return nvram.gsm_log_level;
}

void nvram_set_gsm_log_level(uint8_t level)
{
	nvram.gsm_log_level = level;
}


void nvram_set_is_sbo_active(bool is_active)
{
    nvram.iec104_config.is_sbo_active = is_active;
}

bool nvram_get_is_sbo_active(void)
{
    return nvram.iec104_config.is_sbo_active;
}

void nvram_set_sbo_execute_timeout(uint16_t timeout)
{
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
	nvram.modem_config.serial_number = serial_number;
}

void nvram_set_modbus_device_addr(uint8_t slave_id)
{
	nvram.modbus_config.device_addr = slave_id;
}

uint8_t nvram_get_modbus_device_addr(void)
{
	return nvram.modbus_config.device_addr;
}
 

int nvram_set_m_sp_na_1_ioa(uint32_t line_index, uint8_t phase, uint32_t ioa)
{
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

void nvram_set_iec104_config(const iec104_config_t *cfg)
{
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
	if(cfg) {
		nvram.modbus_config = *cfg;
		nvram_sync(false);
	}
}

// RF Config Getters/Setters
const rf_config_t* nvram_get_rf_config(uint32_t line_index)
{
	if(line_index >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}

	return &nvram.breaker.line[line_index].rf_config;
}

bool nvram_set_rf_config(uint32_t line_index, const rf_config_t* config)
{
	if(line_index >= MAX_POWER_LINE_COUNT || config == NULL) {
		return false;
	}

	nvram.breaker.line[line_index].rf_config = *config;
	return true;
}

/* ── RFWU session ───────────────────────────────────────────────── */

const rfwu_nvram_t *nvram_get_rfwu(void)
{
    return &nvram.rfwu;
}

void nvram_set_rfwu(const rfwu_nvram_t *p_rfwu)
{
    if (p_rfwu == NULL) { return; }
    nvram.rfwu = *p_rfwu;
    (void)nvram_sync(false);
}
