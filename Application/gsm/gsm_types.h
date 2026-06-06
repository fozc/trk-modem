/*
 * gsm_types.h
 *
 *  Created on: 14 Mar 2018
 *      Author: fozcan
 */

#ifndef GSM_TYPES_H_
#define GSM_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp.h"
#include "gsm_log.h"

#define _GSM_    1
#define WARNING  2

/**
 * @brief Level-aware LOG macro for GSM module.
 *
 * When x == WARNING the message is printed at NORMAL level (yellow).
 * Otherwise it is printed only at VERBOSE level (default color).
 */
#define LOG(x, ...)                                                   \
    do {                                                              \
        if ((x) == WARNING)                                           \
        {                                                             \
            GSM_LOG_WRN(__VA_ARGS__);                                 \
            GSM_LOG_NODT("\r\n");                                     \
        }                                                             \
        else                                                          \
        {                                                             \
            GSM_LOG_INF(__VA_ARGS__);                                 \
            GSM_LOG_NODT("\r\n");                                     \
        }                                                             \
    } while (0)

#define LOG_TRACE(x, ...) LOG(x, __VA_ARGS__)

#define GSM_PACKED __attribute__((packed, aligned(1)))

#define ISNUM(x) ((x) >= '0' && (x) <= '9')

/* yyyy -> Her yilin
 * MM   -> Her ayin
 * dd   -> her gunun
 * HH   -> Her saatin
 * mm   -> Her dakikasi
 *
 * Eger tanimli deger var ise;
 *
 * yyyyMMdd0015 -> Her yilin her ayin her gunu saat 00:15'de
 * yyyyMM03HH15 -> Her yilin her ayin 3. gunu her saati 15 gece
 *
 */
typedef union
{
	uint8_t pattern[12];
	struct time_pattern_fields
	{
		uint8_t year[4];
		uint8_t month[2];
		uint8_t day[2];
		uint8_t hour[2];
		uint8_t min[2];
	} fields;
}time_pattern_t; /* yyyyMMddHHmm */

#define MODULE_LE910R1   1
#define MODULE_UNDEFINED 0

#define NUM_OF_RELAY 2

typedef enum
{
	GSM_NO_SIGNAL = 0,
	GSM_2G = 2,
	GSM_3G = 3,
	GSM_4G = 4
} gsm_access_technology_t;

typedef enum
{
	LISTENER_SOCKET = 0,
	IEC104_LISTENER_SOCKET = 1,
	DIALER_SOCKET = 2,
	SOCKET_MAX
}socket_type_t;

#define GSM_MODEM_SOCKET_COUNT  6U  /* Modem soket sayisi (connId 1-6) */

/**
 * @brief AT#SI komut cevabindan parse edilen soket bilgisi.
 *
 * Her bir soket icin gonderilen/alinan bayt, buffer durumu ve ACK bilgisi.
 */
typedef struct
{
	uint32_t sent;         /* Gonderilen toplam bayt miktari  */
	uint32_t received;     /* Alinan toplam bayt miktari      */
	uint32_t buff_in;      /* Tampon'da bekleyen okunmamis bayt */
	uint32_t ack_waiting;  /* ACK bekleyen bayt (sadece TCP)   */
	bool     is_valid;     /* Parse basarili ise true           */
}socket_si_info_t;

typedef enum
{
	SOCKET_CLOSED,
	SOCKET_OPENED,     /* Soket acik */
	SOCKET_HAS_DATA,   /* Sokette okunmayi bekleyen data var */
	SOCKET_CONNECTED,  /* Sokete bagli cihaz var */
	SOCKET_DATA_MODE,  /* Soket veri gonderim modunda */
	SOCKET_LISTENING,  /* Soket acik ve dinleme yapiyor, aktif cihaz yok */
	SOCKET_FAIL,        /* Soket acilamadi, sadece soket acmaya calisirken bu hata meydana gelebilir. */
	SOCKET_STATE_MAX
}socket_state_t;


typedef struct
{
	uint8_t hour;
	uint8_t minute;
}GSM_PACKED short_time_t;

typedef struct
{
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
}gsm_timee_t;

typedef struct
{
	uint8_t day;
	uint8_t month;
	uint8_t year;
}gsm_date_t;

typedef struct
{
	uint8_t day;
	uint8_t month;
	uint8_t year;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
}GSM_PACKED date_time_t;

typedef struct
{
	unsigned hour:5;
	unsigned min:6;
	unsigned sec:6;
	unsigned day:5;
	unsigned month:4;
	unsigned year:5;
	unsigned reserved:1;
}GSM_PACKED package_date_time_t; /* 4 byte */

typedef enum
{
	MODEM_INIT,
	MODEM_NORMAL_MODE,
	MODEM_POWER_OUTAGE,
	MODEM_IDLE,
	MODEM_SLEEP,
	MODEM_TEST_MODE,
	MODEM_POWER_DOWN
}modem_states_t;

typedef struct
{
	modem_states_t state;

}modem_status_t;

typedef union
{
	uint32_t ip;
	struct
	{
		uint8_t d;
		uint8_t c;
		uint8_t b;
		uint8_t a;
	};
}GSM_PACKED ip_addr_t;

typedef struct
{
	uint8_t number[11];
}GSM_PACKED phone_number_t;

typedef struct
{
	uint8_t  domain[32];	/* www.vikosayac.com  */
	uint8_t  path[96];	/* /folder/sayac/app/G06_Modem.rar */
	uint16_t port;		/* 80 */
}GSM_PACKED url_t;

#define AES_KEY_LEN			16
#define APN_LEN	            16
#define APN_USERNAME_LEN	16
#define APN_USERPASS_LEN	16

typedef struct
{
	char apn[APN_LEN + 1];				/* Simkart APN nanme */
	char user_name[APN_USERNAME_LEN + 1]; /* Simkart APN username */
	char user_pass[APN_USERPASS_LEN + 1]; /* Simkart APN pass */
}apn_t;

typedef struct
{
	uint8_t                  version[4];     /* version id of the fw data */
	uint32_t                 size;      		/* len of the fw data */
	package_date_time_t install_date;   /* */
	package_date_time_t download_date;  /* date time of the download time */
	uint32_t                 fw_crc;         /* crc of the fw data */
	uint8_t                  approved;       /* yazilim yuklendikten sonra hersey ok ise bu flag sifirlanir */
	uint16_t                 crc;
}GSM_PACKED fw_meta_data_t;


typedef struct
{
	uint16_t 		   magic_sign; /* 0x03 0x57*/
	uint8_t             loaded_fw;  /* Modemdeki mevcut fw hakkinda bilgi tutar: FF-> Fw yok, yeni modem, 00h-> Fabrika fw yuklu, 01h-> guncel fw yuklu */
	uint8_t             mode;       /* Bootlader'in calisma modunu brlirler: 0x00 -> yazilim yuklu, calistir, 0x01 -> Fabrika fw'yi yukle, 0x02 -> Yeni yazilimi yukle, 0xFF -> Yeni modemi pc com u ac ve fw bekle */
	fw_meta_data_t fw[2];
	uint16_t            success_counter; /* Basarii guncelleme sayisi */
	uint16_t			   error_counter;   /* Toplam hata sayisi */
	uint16_t 		   crc;
}GSM_PACKED boot_data_t;

typedef struct
{
	ip_addr_t ip;
	gsm_timee_t time;
}ip_session_t;

#endif /* GSM_TYPES_H_ */
