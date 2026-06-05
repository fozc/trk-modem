/*
 * types.h
 *
 *  Created on: 11 Agu 2025
 *      Author: fatih
 */

#ifndef TYPES_H_
#define TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "iec104_types.h"
#include "rf_types.h"
#include "modem_types.h"


typedef enum 
{
	SBO_IDLE = 0,
	SBO_SELECTED = 1
} sbo_state_enum_t;

typedef struct 
{
	uint8_t state; // 0: IDLE, 1: SELECTED
	uint8_t value; // 0: Undefined, 1: ON, 2: OFF, 3: Unknown
	uint32_t select_time;  // Selection timestamp
}__attribute__((packed)) sbo_state_t;

typedef struct
{
	ioa_3byte_t ariza_akimi;          //m_me_tf_1
	ioa_3byte_t ariza_suresi;         //m_me_tf_1
	ioa_3byte_t enerji_varyok;        //m_sp_tb_1
	ioa_3byte_t nominal_akim_varyok;  //m_sp_tb_1

}__attribute__((packed)) iec104_fault_log_config_t;

typedef struct
{
	iec104_fault_log_config_t temporary_fault[PHASE_MAX][15];
	iec104_fault_log_config_t permanent_fault[PHASE_MAX][15];
}__attribute__((packed)) iec104_fault_log_line_config_t;

typedef struct
{
	ioa_3byte_t ariza_akimi[PHASE_MAX];          //m_me_tf_1
	ioa_3byte_t ariza_suresi[PHASE_MAX];         //m_me_tf_1
	ioa_3byte_t anlik_akim[PHASE_MAX];           //m_me_tf_1
	ioa_3byte_t ariza_kalicimi[PHASE_MAX];       //m_sp_tb_1
	ioa_3byte_t enerji_varyok[PHASE_MAX];        //m_sp_tb_1
	ioa_3byte_t nominal_akim_varyok[PHASE_MAX];  //m_sp_tb_1
	ioa_3byte_t rf_haberlesme_varyok[PHASE_MAX]; //m_sp_tb_1

	ioa_3byte_t m_me_tf_1_ioa[PHASE_MAX]; //Faz akim bilgisi
	ioa_3byte_t m_sp_tb_1_ioa[PHASE_MAX]; //Ayirici durum bilgisi
#ifdef C_SC_NA_1_ENABLED
	ioa_3byte_t c_sc_na_1_ioa; //Hatti acma komut bilgisi
#endif

	ioa_3byte_t temporary_fault; // 15 kayit icin baz adres
	ioa_3byte_t permanent_fault; // 15 kayit icin baz adres

	uint8_t in_use;
}__attribute__((packed)) iec104_line_config_t;

typedef struct
{
    uint32_t scada_ip_address; //Ipv6 ?
    uint16_t scada_port;
	uint32_t periodical_send_interval; // periodical send interval for lines information, seconds
	uint16_t t0_max;  // Baglanti kurma zaman asimi, RTU'da onemsiz(?)
	uint16_t t1_max;  // Onay zaman asimi. RTU I-frame gonderdikten sonra baslar, sure sonuna kadar scada'dan onay gelmesi lazim, gelmezse paket kaybolmus denilir, tekrar gonderim yapilir. Belirli sayida tekrar gonderimden sonra  ack alamaz ise bagantiyi koparir
	uint16_t t2_max;  // Bir istasyonun (RTU veya SCADA) aldigı I-frame paketlerine onay gondermek icin bekleyebilecegi maks sure
	uint16_t t3_max;  // Idle timeout. Hat bu sure boyunca bosta kalirsa TESTFR gonderimi yaparak baglantiyi test eder. SCADA'da yapabilir.
	uint8_t  k_max;   // Send-window-limit: Max number of unacknowledged I format frames
	uint8_t  w_max;   // W, alim penceresi siniri: Max number of unacknowledged I format frames to be received
	uint16_t sbo_execute_timeout; // seconds. Time between SBO select and execute command
	uint8_t  is_sbo_active; // 0: Not active, 1: Active
	uint8_t  originator_address;
	uint16_t common_address;

	ioa_3byte_t ioa_aku_uyarisi;
    ioa_3byte_t ioa_modem_reset;

	uint32_t crc;
}__attribute__((packed)) iec104_config_t;


typedef struct
{
	uint16_t ariza_akimi;
	uint16_t ariza_suresi;
	uint16_t enerji_varyok;
	uint16_t nominal_akim_varyok;

}__attribute__((packed)) modbus_fault_log_config_t;

typedef struct
{
	uint16_t ariza_akimi[PHASE_MAX];
	uint16_t ariza_suresi[PHASE_MAX];
	uint16_t ariza_kalicimi[PHASE_MAX];
	uint16_t anlik_akim[PHASE_MAX]; 
	uint16_t enerji_varyok[PHASE_MAX];
	uint16_t nominal_akim_varyok[PHASE_MAX];
	uint16_t rf_haberlesme_varyok[PHASE_MAX];

    modbus_fault_log_config_t temporary_fault[PHASE_MAX]; // 15 kayit icin baz adres
	modbus_fault_log_config_t permanent_fault[PHASE_MAX]; // 15 kayit icin baz adres

	uint8_t in_use;
}__attribute__((packed)) modbus_line_config_t;

typedef struct
{
    uint8_t device_addr;      // Modbus cihaz adresi 1-247
    uint8_t last_error_code;
    uint32_t baud_rate;
	uint16_t addr_aku_uyarisi;
    uint16_t addr_modem_reset;
}__attribute__((packed)) modbus_configs_t;

typedef struct
{
	uint8_t phase_id:2;   // 0:Undefined, 1=Phase R/A, 2=Phase S/B, 3=Phase T/C
	uint8_t line_id:6;    // 0=Undefined, 1-62
}__attribute__((packed)) line_id_t;


typedef struct
{
	iec104_line_config_t iec104; // IEC104 ile ilgili konfigurasyon bilgileri
	modbus_line_config_t modbus; // Modbus RTU ile ilgili konfigurasyon bilgileri
	rf_config_t rf_config;       // RF ayirici konfigurasyon bilgileri
	sbo_state_t sbo_state; 
	uint8_t breaker_state;
}__attribute__((packed)) power_line_t;

typedef struct
{
	float   ariza_akimi;          //m_me_tf_1
	float   anlik_akim;           //m_me_tf_1
	float   ariza_suresi;         //m_me_tf_1
	uint8_t ariza_kalicimi;       //m_sp_tb_1
	uint8_t enerji_varyok;        //m_sp_tb_1
	uint8_t nominal_akim_varyok;  //m_sp_tb_1
	uint8_t rf_haberlesme_varyok; //m_sp_tb_1
	cp56time2a_t tm_ariza_akimi;
	cp56time2a_t tm_anlik_akim;
	cp56time2a_t tm_ariza_suresi;
	cp56time2a_t tm_ariza_kalicimi;
	cp56time2a_t tm_enerji_varyok;
	cp56time2a_t tm_nominal_akim_varyok;
	cp56time2a_t tm_rf_haberlesme_varyok;
}phase_data_t;

typedef struct
{
	phase_data_t phase[PHASE_MAX];
}feeder_data_t;

typedef struct
{
	feeder_data_t feeder[MAX_POWER_LINE_COUNT];
}breaker_data_t;

typedef struct
{
	power_line_t line[MAX_POWER_LINE_COUNT];
	uint32_t crc;
}__attribute__((packed)) breaker_t;

typedef struct
{

	uint32_t crc;
}__attribute__((packed)) fault_log_config_t;


/**
 * @brief RFWU firmware-update session stored in NVRAM.
 *
 * magic == RFWU_SESSION_MAGIC (0xFEEDFACE) indicates a valid in-progress or
 * completed session.  received_bytes is always a multiple of 4096 so that a
 * resume always starts on a flash-sector boundary.
 */
typedef struct
{
    uint32_t magic;           /**< 0xFEEDFACE when record is valid          */
    uint32_t file_hash;       /**< CRC-32 of first min(1024, size) bytes     */
    uint32_t total_size;      /**< Declared firmware image size in bytes     */
    uint32_t received_bytes;  /**< Flash-committed offset (4 KB-aligned)     */
    uint32_t shared_key;      /**< PSK for auth token derivation             */
} rfwu_nvram_t;

typedef struct
{
	modem_config_t modem_config;
	iec104_config_t iec104_config;
	modbus_configs_t modbus_config;
	breaker_t breaker; 

	uint8_t cslog_enabled;
	uint8_t gsm_log_level;   /**< gsm_log_level_t persisted value */

    rfwu_nvram_t rfwu;

    uint32_t crc; 
}nvram_t;

#endif /* TYPES_H_ */
