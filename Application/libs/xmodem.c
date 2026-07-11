/*
 * xmodem.c
 *
 *  Created on: 23 May 2018
 *      Author: fozcan
 */

#include "xmodem.h"
#include <string.h>
#include "bsp.h"
#include "w25qxx.h"

#ifdef XMODEM_TEST
static uint8_t xmodem_test_frame[] = {
		0x01, 0x01, 0xFE, 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x34, 0xBC, 0x27, 0x56, 0xB6, 0xDC, 0x57, 0xBF,
		0xED, 0x0A, 0x22, 0x00, 0x00, 0x6E, 0x5C, 0x00, 0x09, 0x00, 0x00, 0x00, 0x68, 0x74, 0x65, 0x72, 0x6D, 0x2E, 0x65, 0x78, 0x65,
		0xEC, 0xBD, 0x7D, 0x78, 0x54, 0xC5, 0xFD, 0xFE, 0x7F, 0x02, 0x01, 0x56, 0x09, 0xB2, 0x40, 0xD4, 0xA8, 0x51, 0x57, 0x48, 0x6B,
		0x2C, 0x51, 0x57, 0x88, 0x98, 0x96, 0xA8, 0x0B, 0x04, 0x8C, 0x12, 0x74, 0x81, 0xA8, 0x69, 0x8D, 0x75, 0x85, 0x7C, 0xE8, 0x36,
		0xA6, 0x6D, 0x0A, 0x41, 0x63, 0x8D, 0xBA, 0x90, 0xA4, 0x4D, 0xDC, 0x44, 0x16, 0x88, 0x35, 0x48, 0x90, 0x40, 0xD2, 0x36, 0xB6,
		0xD8, 0xAE, 0x8A, 0x1A, 0x2A, 0xDA, 0xE5, 0xA1, 0x24, 0x96, 0xA8, 0x11, 0xD0, 0xC6, 0x4A, 0x6B, 0x94, 0x54, 0x87, 0x1A, 0x75,
		0x85, 0xA8, 0x01, 0x82, 0xFC, 0xF6, 0xCC};
#endif


#define XMODEM_SOH 0x01
#define XMODEM_STX 0x02
#define XMODEM_ETX 0x03
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_CAN 0x18
#define XMODEM_SUB 0x1A

#define XMODEM_TICK_MS            (10) //  ms
#define XMODEM_SESSION_TIMEOUT    ((10 * 1000) / XMODEM_TICK_MS)
#define XMODEM_READY_TIME         ((1 * 1000) / XMODEM_TICK_MS)
#define XMODEM_RX_TIMEOUT         (3000 / XMODEM_TICK_MS)
#define XMODEM_TIMER_DISABLED     0xFFFFU

#define XMODEM_RX_DATA_SIZE       128
#define XMODEM_CAN_THRESHOLD      2U


typedef enum
{
	XMODEM_INIT,
	XMODEM_WAITING,
	XMODEM_DOWNLOADING,
	XMODEM_CANCEL,
	XMODEM_END_OF_FILE
}xmodem_session_states_t;

typedef enum
{
	XMODEM_IDLE,
	XMODEM_SEQ1,
	XMODEM_SEQ2,
	XMODEM_DATA,
	XMODEM_CRC1,
	XMODEM_CRC2,
	XMODEM_PACKAGE_READY
}xmodem_state_t;

typedef struct
{
	uint8_t buff[XMODEM_RX_DATA_SIZE];
	volatile uint8_t  index;
	volatile uint8_t  state;
	uint16_t crc;
	uint8_t  package_num;
	uint8_t  package_num2;
	volatile uint16_t timer;
} xmodem_package_t;

typedef struct
{
	xmodem_package_t      pckg;
	uint8_t               package_no;
	uint16_t              total_packet;
	volatile uint8_t      session_state;
	volatile uint16_t     timer;
	uint8_t               can_count;
	volatile uint8_t      evt_pending;
}xmodem_t;

#ifdef XMODEM_USE_CACHE

#define CACHE_SIZE (XMODEM_RX_DATA_SIZE * 32)

#if (CACHE_SIZE % XMODEM_RX_DATA_SIZE) > 0
	#error Xmodem cache size must be multiple of XMODEM_FRAME_SIZE
#endif

typedef struct
{
	uint8_t  buff[CACHE_SIZE];
	uint16_t index;
	uint32_t len;
	uint32_t addr;
}cache_t;

static cache_t cache = {0};
#endif

static xmodem_t xm = {0};
static xmodem_config_t cfg = {0};

#ifdef XMODEM_USE_CRC_LUT_TABLE
static const uint16_t xmodem_crc_table [256] = {
0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5,
0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b,
0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210,
0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c,
0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401,
0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b,
0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6,
0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738,
0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5,
0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969,
0xa90a, 0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96,
0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc,
0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03,
0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6,
0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb,
0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1,
0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c,
0xe37f, 0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2,
0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb,
0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447,
0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2,
0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9,
0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827,
0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d, 0xdb5c,
0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0,
0x2ab3, 0x3a92, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d,
0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba,
0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

uint16_t CRC_CALC_XMODEM(const void *p, uint16_t len)
{
	const uint8_t *ptr = (const uint8_t *)p;
	uint16_t crc = 0;
	while (len-- > 0)
	  crc = (crc << 8) ^ xmodem_crc_table[(crc >> 8) ^ *ptr++];
	return crc;
}
#else

// See bottom of this page: http://www.nongnu.org/avr-libc/user-manual/group__util__crc.html
// Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
static uint16_t crc_xmodem_update(uint16_t crc, uint8_t data)
{
  crc = crc ^ ((uint16_t)data << 8);
  for (int i=0; i<8; i++)
  {
    if (crc & 0x8000)
      crc = (crc << 1) ^ 0x1021;
    else
      crc <<= 1;
  }
  return crc;
}

uint16_t CRC_CALC_XMODEM(const void *p, uint16_t len)
{
	const uint8_t *ptr = (const uint8_t *)p;
	  uint16_t crc = 0;

	  while(len--)
	  {
	    crc = crc_xmodem_update(crc, *ptr++);
	  }
	  return crc;
}
#endif

static inline void xmodem_write_byte(uint8_t c)
{
	cfg.write_byte(c);
}

void xmodem_write_buff(const void  *buff, uint16_t len)
{
	const uint8_t *ptr = (const uint8_t *)buff;
	for(uint32_t i = 0; i < len; i++)
	{
		xmodem_write_byte(*ptr++);
	}
}

void xmodem_send_ready(void)
{
	xmodem_write_byte('C');
}

void xmodem_send_ack(void)
{
	xmodem_write_byte(XMODEM_ACK);
}

void xmodem_send_nack(void)
{
	xmodem_write_byte(XMODEM_NAK);
}

void xmodem_reset_package(void)
{
	memset(&xm.pckg, 0, sizeof(xmodem_package_t));
	xm.pckg.timer = XMODEM_TIMER_DISABLED;
}

void xmodem_timer_process(void)
{
	if(xm.timer != XMODEM_TIMER_DISABLED)
	{
		xm.timer++;
	}

	if(xm.pckg.timer != XMODEM_TIMER_DISABLED)
	{
		xm.pckg.timer++;
		if(xm.pckg.timer > XMODEM_RX_TIMEOUT)
		{
			xmodem_reset_package();
			if(xm.total_packet > 0U)
			{
				xmodem_send_nack();
			}
			else
			{
				xmodem_send_ready();
			}
		}
	}
}

void xmodem_reset(void)
{
	memset(&xm, 0, sizeof(xmodem_t));
	xm.package_no = 1;
	xm.pckg.timer = XMODEM_TIMER_DISABLED;
}

void xmodem_rx_isr(uint8_t data)
{
	switch(xm.pckg.state)
	{
	case XMODEM_IDLE:
		if(data == XMODEM_SOH)
		{
			xm.can_count  = 0;
			xm.pckg.state = XMODEM_SEQ1;
			if(xm.session_state == XMODEM_WAITING)
			{
				xm.evt_pending  |= XMODEM_EVT_FILE_DOWNLOAD_STARTED;
				xm.session_state = XMODEM_DOWNLOADING;
				xm.timer = 0;
			}
			xm.pckg.timer = 0;
		}
		else if(data == XMODEM_CAN)
		{
			xm.can_count++;
			if(xm.can_count >= XMODEM_CAN_THRESHOLD)
			{
				if(xm.session_state == XMODEM_DOWNLOADING)
				{
					xm.evt_pending  |= XMODEM_EVT_FILE_DOWNLOAD_CANCELED;
					xm.session_state = XMODEM_CANCEL;
				}
				xm.can_count = 0;
			}
		}
		else if(data == XMODEM_EOT)
		{
			xm.can_count     = 0;
			xm.session_state = XMODEM_END_OF_FILE;
		}
		else
		{
			xm.can_count = 0;
		}
		break;

	case XMODEM_SEQ1:
		xm.pckg.package_num = data;
		xm.pckg.state       = XMODEM_SEQ2;
		xm.pckg.timer = 0;
		break;

	case XMODEM_SEQ2:
		xm.pckg.package_num2 = data;
		xm.pckg.state        = XMODEM_DATA;
		xm.pckg.timer = 0;
		break;

	case XMODEM_DATA:
		if(xm.pckg.index < XMODEM_RX_DATA_SIZE)
		{
			xm.pckg.buff[xm.pckg.index++] = data;
		}
		if(xm.pckg.index >= XMODEM_RX_DATA_SIZE)
		{
			xm.pckg.state = XMODEM_CRC1;
		}
		xm.pckg.timer = 0;
		break;

	case XMODEM_CRC1:
		xm.pckg.crc   = (uint16_t)data << 8;
		xm.pckg.state = XMODEM_CRC2;
		xm.pckg.timer = 0;
		break;

	case XMODEM_CRC2:
		xm.pckg.crc  += data;
		xm.pckg.state = XMODEM_PACKAGE_READY;
		xm.pckg.timer = XMODEM_TIMER_DISABLED;
		break;

	case XMODEM_PACKAGE_READY:
		break;

	default:
		break;
	}
}



uint32_t xmodem_is_package_ready(void)
{
	return (xm.pckg.state == XMODEM_PACKAGE_READY);
}

uint32_t xmodem_check_package(void)
{
	/* Dönüş: 0 = geçersiz (NAK), 1 = beklenen blok (kabul), 2 = yinelenen (sessiz ACK) */
	uint16_t crc = CRC_CALC_XMODEM(xm.pckg.buff, XMODEM_RX_DATA_SIZE);

	/* Sekans tamamlayıcı kontrolü: package_num + package_num2 == 0xFF */
	if((xm.pckg.package_num + xm.pckg.package_num2) != 0xFF)
	{
		CSLOG("Gecersiz paket (header). %d\r\n", xm.package_no);
		return 0U;
	}

	if(xm.package_no == xm.pckg.package_num)
	{
		/* Beklenen blok */
		if(crc == xm.pckg.crc)
		{
			return 1U;
		}
		CSLOG("Gecersiz paket (CRC). %d\r\n", xm.package_no);
		return 0U;
	}

	if(xm.pckg.package_num == (uint8_t)(xm.package_no - 1U))
	{
		/* Yinelenen blok: önceki ACK'miz kaybolmuş, gönderici aynı bloğu yeniden
		 * gönderdi. Veriyi tekrar yazmamak için sessizce ACK edilir; eğer bu
		 * yinelemenin CRC'si bozuksa yine de NAK (yeniden gönderim iste). */
		return (crc == xm.pckg.crc) ? 2U : 0U;
	}

	CSLOG("Gecersiz paket (beklenmeyen blok). %d\r\n", xm.package_no);
	return 0U;
}

void xmodem_poll(void)
{
	/* Dispatch events deferred from ISR context */
	if(xm.evt_pending != 0U)
	{
		uint8_t evt = xm.evt_pending;
		xm.evt_pending = 0;
		if(cfg.event_callback != NULL)
		{
			if(evt & XMODEM_EVT_FILE_DOWNLOAD_STARTED)
			{
				cfg.event_callback(XMODEM_EVT_FILE_DOWNLOAD_STARTED, NULL, 0);
			}
			if(evt & XMODEM_EVT_FILE_DOWNLOAD_CANCELED)
			{
				cfg.event_callback(XMODEM_EVT_FILE_DOWNLOAD_CANCELED, NULL, 0);
			}
		}
	}

	switch(xm.session_state)
	{
	case XMODEM_INIT:
		xmodem_reset();
		xm.package_no = 1;
#ifdef XMODEM_USE_CACHE
		cache.addr = 0;
		cache.index = 0;
		cache.len = 0;
#endif
		xm.total_packet = 1;
		xm.session_state = XMODEM_WAITING;
		break;
	case XMODEM_WAITING:
		if(xm.timer >= XMODEM_READY_TIME)
		{
			xm.timer = 0;
			xmodem_send_ready();
		}
		break;
	case XMODEM_DOWNLOADING:
		if(xmodem_is_package_ready())
		{
			uint32_t check = xmodem_check_package();

			if(check == 1U)
			{
				/* Copy data out of ISR buffer before resetting.
				 * Send ACK early so the sender can transmit the next block
				 * while the (potentially slow) event callback runs. */
				uint8_t chunk[XMODEM_RX_DATA_SIZE];
				memcpy(chunk, xm.pckg.buff, XMODEM_RX_DATA_SIZE);

				xm.total_packet++;
				++xm.package_no;
				xmodem_reset_package();
				xmodem_send_ack();

#ifdef XMODEM_USE_CACHE
				memcpy(&cache.buff[cache.index], chunk, XMODEM_RX_DATA_SIZE);
				cache.index += XMODEM_RX_DATA_SIZE;
				cache.len += XMODEM_RX_DATA_SIZE;
				if(cache.index >= CACHE_SIZE)
				{
					if(cache.addr == 0)
					{
						w25qxx_erase_block64(0);
					}

					if(cfg.event_callback)
					{
						cfg.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED, cache.buff, cache.index);
					}
					w25qxx_write_buff(cache.addr, cache.buff, CACHE_SIZE);
					cache.addr += 4096;
					cache.index = 0;
				}
#else
				if(cfg.event_callback){
					cfg.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED, chunk, XMODEM_RX_DATA_SIZE);
				}
#endif
			}
			else if(check == 2U)
			{
				/* Yinelenen blok (kayıp ACK): veriyi tekrar yazmadan sessizce ACK */
				xmodem_reset_package();
				xmodem_send_ack();
			}
			else
			{
				/* Geçersiz paket: NAK ile yeniden gönderim iste */
				xmodem_reset_package();
				xmodem_send_nack();
			}
			xm.timer = 0;
		}
		if(xm.timer >= XMODEM_SESSION_TIMEOUT)
		{
			xmodem_write_byte(XMODEM_CAN);
			xmodem_write_byte(XMODEM_CAN);
			xmodem_write_byte(XMODEM_CAN);
			xmodem_write_byte(XMODEM_CAN);
			xmodem_write_byte(XMODEM_SUB);
#ifdef XMODEM_USE_CACHE
			memset(cache.buff, 0, CACHE_SIZE);
#endif
			xmodem_reset();
			xm.timer = 0;
			xm.session_state = XMODEM_INIT;
		}
		break;
	case XMODEM_CANCEL:
#ifdef XMODEM_USE_CACHE
		memset(cache.buff, 0, CACHE_SIZE);
#endif
		xm.timer = 0;
		xm.session_state = XMODEM_INIT;
		CSLOG("XMODEM CANCEL !");
		break;
	case XMODEM_END_OF_FILE:
		xmodem_send_ack();
		CSLOG("XMODEM transfer tamamlandi. Total Pck[%u]\r\n", xm.total_packet);
#ifdef XMODEM_USE_CACHE
		if(cache.index > 0)
		{
			w25qxx_write_buff(cache.addr, cache.buff, cache.index);
			if(cfg.event_callback)
			{
				cfg.event_callback(XMODEM_EVT_FILE_CHUNK_RECEIVED, cache.buff, cache.index);
			}
		}
		if(cfg.event_callback)
		{
			cfg.event_callback(XMODEM_EVT_FILE_DOWNLOAD_COMPLETE, 0, 0);
		}
		CSLOG("File len: %d Last Sector Addr: %d Totoal Packet: %d\r\n", cache.len, cache.addr, xm.total_packet - 1);
#else
		if(cfg.event_callback)
		{
			cfg.event_callback(XMODEM_EVT_FILE_DOWNLOAD_COMPLETE, 0, 0);
		}
#endif

		xm.session_state = XMODEM_INIT;
		break;

	default:
		break;
	}
}

int xmodem_init(xmodem_config_t cfg_)
{
	cfg = cfg_;

	if (cfg.write_byte == NULL){
		return -1;
	}

	xm.session_state = XMODEM_INIT;
	xm.pckg.state = XMODEM_IDLE;

	return 0;
}
