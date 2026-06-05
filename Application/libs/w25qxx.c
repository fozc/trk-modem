/*
 * w25q64.c
 *
 *  Created on: Dec 6, 2023
 *      Author: fatih
 */
#include <ctype.h>
#include "w25qxx.h"
#include "spi.h"
#include "main.h"
#include "bsp.h"

_Static_assert(sizeof(spi_flash_layout_t) <= W25QXX_SIZE,
               "Flash layout exceeds chip capacity!");			   

#define W25QXX_LOG

#ifndef W25QXX_LOG
	#undef CSLOG
	#define CSLOG(a...)
#endif


#define FLASH_DUMMY 0x00

#define CMD_JEDECID 0x9F
#define CMD_RELEASE_POWER_DOWN_DEVICE_ID 0xAB
#define CMD_READ_MANUFACTURER_DEVICE_ID 0x90
#define CMD_READ_STATUS_REGISTER_1  0x05
#define CMD_READ_STATUS_REGISTER_2  0x35
#define CMD_READ_STATUS_REGISTER_3  0x15
#define CMD_WRITE_STATUS_REGISTER_1 0x01
#define CMD_WRITE_STATUS_REGISTER_3 0x11
#define CMD_WRITE_STATUS_REGISTER_2 0x31


#define CMD_WRITE_ENABLE 0x06
#define CMD_SECTOR_ERASE 0x20  // 4Kb
#define CMD_BLOCK32_ERASE 0x52
#define CMD_BLOCK64_ERASE 0xD8
#define CMD_READ_DATA 0x03
#define CMD_FAST_READ_DATA 0x0B
#define CMD_PAGE_PROGRAM 0x02
#define CMD_ENABLE_RESET 0x66
#define CMD_RESET_DEVICE 0x99

#define STATUS1_SRP  0x80 /* STATUS REGISTER PROTECT */
#define STATUS1_SEC  0x40 /* SECTOR PROTECT */
#define STATUS1_TB   0x20 /* TOP/BOTTOM PROTECT */
#define STATUS1_BP2  0x10 /* BLOCK PROTECT BIT-2 */
#define STATUS1_BP1  0x08 /* BLOCK PROTECT BIT-1 */
#define STATUS1_BP0  0x04 /* BLOCK PROTECT BIT-0 */
#define STATUS1_WEL  0x02 /* WRITE ENABLE LATCH */
#define STATUS1_BUSY 0x01 /* ERASE/WRITE IN PROGRESS */

#define w25qxx_wait_for_busy w25qxx_wait_for_write_or_erase

typedef struct
{
	uint8_t  data[W25QXX_SECTOR_SIZE];
	uint32_t sector_addr;
	uint8_t  dirty;           // RAM'deki veriler flash'a yaildi mi?, 1: yazildi , 0: yazilmadi
	uint8_t  valid;           // Cache gecerli mi (ilk yukleme için)
}cache_mem_t;

typedef struct {
    uint8_t id;
    uint32_t size_bytes;
    const char *size_str;
} flash_capacity_t;

static const flash_capacity_t capacity_table[] =
{
		{0x15, 1UL << 21, "2 MB (16 Mbit, W25Q16)"},
	    {0x16, 1UL << 22, "4 MB (32 Mbit, W25Q32)"},
	    {0x17, 1UL << 23, "8 MB (64 Mbit, W25Q64)"},
	    {0x18, 1UL << 24, "16 MB (128 Mbit, W25Q128)"},
	    {0x19, 1UL << 25, "32 MB (256 Mbit, W25Q256)"},
	    {0x20, 1UL << 26, "64 MB (512 Mbit, W25Q512)"},
	    {0x21, 1UL << 27, "128 MB (1 Gbit, W25Q01)"},
};

static cache_mem_t cache_mem = {0};

const flash_capacity_t* w25q_lookup_capacity(uint8_t cid)
{
    for (int i = 0; i < sizeof(capacity_table)/sizeof(capacity_table[0]); i++) {
        if (capacity_table[i].id == cid) {
            return &capacity_table[i];
        }
    }
    return &(static flash_capacity_t){0, 0, "Unknown"};
}


static void delay(int val)
{
	while(val--)
	{
		for(volatile uint8_t i = 0; i < 192; i++)
		{
			__NOP();
			__NOP();
			__NOP();
		}
	}
}

uint8_t w25qxx_read_register(uint8_t reg)
{
	spi_cs_low();
	spi_send_byte(reg);
	uint8_t res = spi_read_byte();
	spi_cs_high();

	return res;
}

static inline uint8_t w25qxx_read_status(void)
{
	return w25qxx_read_register(CMD_READ_STATUS_REGISTER_1);
}

int w25qxx_wait_for_write_or_erase(void)
{
	volatile uint32_t timeout = 300000u;
	while(w25qxx_read_status() & STATUS1_BUSY && (timeout > 0u)){
		timeout--;
	}

	if(timeout == 0){
		CCSLOG(XCOLOR_RED, "W25QXX wait for write/erase timeout!\r\n");
		return W25QXX_RES_TIMEOUT;
	}

	return W25QXX_RES_OK;
}

void w25qxx_write_enable(void)
{
	spi_cs_low();
	spi_send_byte(CMD_WRITE_ENABLE);
	spi_cs_high();
}

void w25qxx_reset(void)
{
	spi_cs_high();
	delay(1);
	spi_cs_low();
	spi_send_byte(CMD_ENABLE_RESET);
	spi_cs_high();

	delay(2);

	spi_cs_low();
	spi_send_byte(CMD_RESET_DEVICE);
	spi_cs_high();

	delay(10);
}

int w25qxx_disable_block_protect(void)
{
	w25qxx_write_enable();
	delay(1);

	if(!(w25qxx_read_status() & STATUS1_WEL))
	{
		CCSLOG(XCOLOR_RED, "Error!: WEL is not set!\r\n");
		return W25QXX_RES_WEL_NOT_SET;
	}

	spi_cs_low();
	spi_send_byte(CMD_WRITE_STATUS_REGISTER_1);
	spi_send_byte(0x00); // Disable all block protect, Status Reg-1
	spi_cs_high();

	delay(1);
	int res = w25qxx_wait_for_write_or_erase();
	return res;
}

int w25qxx_erase_chip(void)
{
	w25qxx_write_enable();
	delay(1);

	spi_cs_low();
	spi_send_byte(0xC7); // Chip Erase
	spi_cs_high();

	delay(1);
	int res = w25qxx_wait_for_write_or_erase();
	return res;
}

int w25qxx_init(void)
{
	int res = 0;
	const uint32_t jid = 0x001540EF;

	w25qxx_reset();
	uint32_t read_jid = w25qxx_read_jedecid();

	uint8_t status_1 = w25qxx_read_status();
	uint8_t status_2 = w25qxx_read_register(CMD_READ_STATUS_REGISTER_2);
	uint8_t status_3 = w25qxx_read_register(CMD_READ_STATUS_REGISTER_3);

	if(status_1 != 0x00)
	{
		CCSLOG(XCOLOR_RED, "W25 Status Reg-1: 0x%02X\r\n", status_1);
	}
	if(status_2 != 0x02)
	{
		CCSLOG(XCOLOR_RED, "W25 Status Reg-2: 0x%02X\r\n", status_2);
	}
	if(status_3 != 0x60)
	{
		CCSLOG(XCOLOR_RED, "W25 Status Reg-3: 0x%02X\r\n", status_3);
	}

	if(status_1 & (STATUS1_SEC | STATUS1_TB | STATUS1_BP2 | STATUS1_BP1 | STATUS1_BP0))
	{
		CCSLOG(XCOLOR_RED, "W25 Block Protect is enabled. Disabling...\r\n");
		int protect_ok  = w25qxx_disable_block_protect();
		if(protect_ok != W25QXX_RES_OK)
		{
			CCSLOG(XCOLOR_RED, "Failed to disable block protect!\r\n");
			return W25QXX_RES_ERROR;
		}
		status_1 = w25qxx_read_status();
		CCSLOG(XCOLOR_RED, "W25 Status Reg-1: 0x%02X\r\n", status_1);
		if(status_1 & (STATUS1_SEC | STATUS1_TB | STATUS1_BP2 | STATUS1_BP1 | STATUS1_BP0))
		{
			CCSLOG(XCOLOR_RED, "W25 Block Protect Disable Failed!\r\n");
			res = W25QXX_RES_ERROR;
		}
	}

	CCSLOG(XCOLOR_RED, "W25 Capacity: %s\r\n", w25q_lookup_capacity(((uint8_t *)&read_jid)[2])->size_str);
	if(read_jid != jid)
	{
		CCSLOG(XCOLOR_RED,"Jedec Id Error: 0x%02X-0x%02X-0x%02X-0x%02X\r\n", ((uint8_t *)&read_jid)[0],
				((uint8_t *)&read_jid)[1], ((uint8_t *)&read_jid)[2], ((uint8_t *)&read_jid)[3]);
		res = W25QXX_RES_ERROR;
	}
	//w25qxx_erase_chip();

	return res;
}

void w25qxx_wakeup(void)
{

}

uint16_t w25qxx_read_manu_deviceid(void)
{
	uint8_t id[2] = {0};

	spi_cs_low();

	spi_send_byte(CMD_READ_MANUFACTURER_DEVICE_ID);
	spi_send_byte(0x00);
	spi_send_byte(0x00);
	spi_send_byte(0x00);

	for(int i = 0; i < 2; i++)
	{
		id[i] = spi_read_byte();
	}

	spi_cs_high();

	return id[0] | (id[1] << 8) ;
}

uint32_t w25qxx_read_jedecid(void)
{
	uint8_t jedec_id[4] = {0};

	spi_cs_low();

	spi_send_byte(CMD_JEDECID);
	for(int i = 0; i < 4; i++)
	{
		jedec_id[i] = spi_read_byte();
	}

	spi_cs_high();

	return jedec_id[0] | (jedec_id[1] << 8) | (jedec_id[2] << 16) | (jedec_id[3] << 24);
}



static int w25qxx_erase_(uint8_t erase_type, uint32_t addr)
{
	w25qxx_write_enable();
	delay(1);
	spi_cs_low();
	spi_send_byte(erase_type);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);
	spi_send_byte(addr);
	spi_cs_high();
	delay(1);
	int res = w25qxx_wait_for_write_or_erase();
	return res;
}

int w25qxx_erase_block64(uint32_t addr)
{
	if(addr % 65536)
	{
		CCSLOG(XCOLOR_RED, "Addr: %d w25qxx_erase_block64() The address is out of sector !", addr);
		return 1;
	}

	int res = w25qxx_erase_(CMD_BLOCK64_ERASE, addr);
	return res;
}

int w25qxx_erase_block32(uint32_t addr)
{
	if(addr % 32768)
	{
		CCSLOG(XCOLOR_RED, "Addr: %d w25qxx_erase_block32() The address is out of sector !", addr);
		return 1;
	}

	int res = w25qxx_erase_(CMD_BLOCK32_ERASE, addr);
	return res;
}

int w25qxx_erase_sector(uint32_t addr)
{
	if(addr % 4096)
	{
		CCSLOG(XCOLOR_RED,"Addr: %d w25qxx_erase_sector() The address is out of sector !", addr);
		return 1;
	}

	int res = w25qxx_erase_(CMD_SECTOR_ERASE, addr);
	return res;
}

uint8_t w25qxx_read_byte(uint32_t addr)
{
	spi_cs_low();
	spi_send_byte(CMD_READ_DATA);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);
	spi_send_byte(addr);

	uint8_t res =  spi_read_byte();
	spi_cs_high();

	return res;
}

void w25qxx_read_buff(uint32_t addr, void *buff, uint32_t len)
{
	if(addr > W25QXX_END_ADDR || (addr + len) > W25QXX_SIZE)
	{
		CCSLOG(XCOLOR_RED, "Address out of the range!\r\n");
		return;
	}

	if(buff == NULL || len == 0)
	{
		CCSLOG(XCOLOR_RED, "Invalid parameters! addr: 0x%X, buff: %p, len: %u\r\n", addr, buff, len);
		return;
	}


	uint8_t *ptr = buff;
	spi_cs_low();
	spi_send_byte(CMD_FAST_READ_DATA);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);
	spi_send_byte(addr);
	spi_send_byte(FLASH_DUMMY);
	while(len--)
	{
		*ptr++ = spi_read_byte();
	}
	spi_cs_high();
}

int w25qxx_write_byte(uint32_t addr, uint8_t data)
{
	if(addr > W25QXX_END_ADDR)
	{
		CCSLOG(XCOLOR_RED, "Address out of the range!\r\n");
		return 1;
	}
	w25qxx_write_enable();
	if(!(w25qxx_read_status() & STATUS1_WEL))
	{
		CCSLOG(XCOLOR_RED,"Error!: WEL is not set!\r\n");
		return 1;
	}

	spi_cs_low();
	spi_send_byte(CMD_PAGE_PROGRAM);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);
	spi_send_byte(addr);

	spi_send_byte(data);
	spi_cs_high();

	return w25qxx_wait_for_write_or_erase();
}

int w25qxx_page_write(uint32_t addr, const void *buff, uint32_t len)
{
	const uint8_t *ptr = buff;

	if(addr > W25QXX_END_ADDR)
	{
		CCSLOG(XCOLOR_RED, "2- Address out of the range!\r\n");
		return 1;
	}
	
	//uint32_t page_start_addr = addr - (addr % W25QXX_PAGE_SIZE);
	uint32_t remain_len_in_page = W25QXX_PAGE_SIZE - (addr % W25QXX_PAGE_SIZE);
	if(remain_len_in_page < len)
	{
		CCSLOG(XCOLOR_RED, "1- Address out of the range!\r\n");
		return 1;
	}

	w25qxx_write_enable();
	delay(3);
	if(!(w25qxx_read_status() & STATUS1_WEL))
	{
		CCSLOG(XCOLOR_RED, "Error!: WEL is not set!\r\n");
		return 1;
	}

	spi_cs_low();
	spi_send_byte(CMD_PAGE_PROGRAM);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);

	// If an entire 256 byte page is to be programmed, the last address byte (the 8 LSB) should be set to 0.
	if(len == W25QXX_PAGE_SIZE){
		spi_send_byte(0x00);
	}
	else{
		spi_send_byte(addr);
	}
	while(len--){
		spi_send_byte(*ptr++);
	}

	spi_cs_high();
	delay(1);
	
	return w25qxx_wait_for_write_or_erase();
}

uint32_t w25qxx_get_sector_start_addr(uint32_t addr)
{
	return (addr - (addr % W25QXX_SECTOR_SIZE));
}

int w25qxx_write_sector(uint32_t addr, const void *buff, uint32_t lenght)
{
	int res = W25QXX_RES_OK;
	uint32_t len = lenght;
	uint32_t page_len;
	uint32_t current_addr = addr;
	const uint8_t *buff_ptr = (const uint8_t *)buff;
	uint32_t sector_addr = w25qxx_get_sector_start_addr(addr);

	if(buff == NULL || len == 0 || sector_addr != addr || (addr + lenght) > W25QXX_SIZE)
	{
		CCSLOG(XCOLOR_RED, "Cannot write buff is null [%d], len: [%d], sectoraddr != addr: [0x%X - 0x%X] addr err[0x%X]\r\n",
				buff == NULL, len, sector_addr, addr, ((addr + lenght) > W25QXX_SIZE));
		return W25QXX_RES_INVALID_PARAM;
	}

	while(len && (res == W25QXX_RES_OK))
	{
		if(len >= W25QXX_PAGE_SIZE)
			page_len = W25QXX_PAGE_SIZE;
		else
			page_len = len;
		res |= w25qxx_page_write(current_addr, buff_ptr, page_len);
		buff_ptr += page_len;
		current_addr += page_len;
		len  -= page_len;
	}

	if(len)
	{
		CSLOG("A problem occurred while writing. ECode:[%d]\r\n", res);
		return W25QXX_RES_ERROR;
	}

	return res;
}

int w25qxx_verify(uint32_t addr, const void *data, uint32_t len)
{
	if(addr > W25QXX_END_ADDR || (addr + len) > W25QXX_SIZE)
	{
		CSLOG("Address out of the range!\r\n");
		return W25QXX_RES_INVALID_PARAM;
	}

	if(data == NULL || len == 0)
	{
		CSLOG("Invalid parameters! addr: 0x%X, data: %p, len: %u\r\n", addr, data, len);
		return W25QXX_RES_INVALID_PARAM;
	}

	const uint8_t *ptr = (const uint8_t *)data;
	uint8_t temp;
	int res = W25QXX_RES_OK;

	spi_cs_low();
	spi_send_byte(CMD_FAST_READ_DATA);
	spi_send_byte(addr >> 16);
	spi_send_byte(addr >> 8);
	spi_send_byte(addr);
	spi_send_byte(FLASH_DUMMY);
	while(len--)
	{
		temp = spi_read_byte();
		if(*ptr != temp)
		{
			CCSLOG(XCOLOR_RED, "Flash Verify Err. index: %u 0x%X->0x%X\r\n", (ptr - (const uint8_t *)data), *ptr, temp);
			res = W25QXX_RES_VERIFY_FAILED;
		}
		ptr++;
	}
	spi_cs_high();

	return res;
}


int w25qxx_write_buff(uint32_t addr, const void *buff, uint32_t buff_len)
{
	if(addr > W25QXX_END_ADDR)
	{
		CCSLOG(XCOLOR_RED, "Address out of the range!\r\n");
		return W25QXX_RES_INVALID_PARAM;
	}

	if(buff == NULL || buff_len == 0 || (addr + buff_len) > W25QXX_SIZE)
	{
		CCSLOG(XCOLOR_RED, "Invalid parameters! addr: 0x%X, buff: %p, len: %u\r\n", addr, buff, buff_len);
		return W25QXX_RES_INVALID_PARAM;
	}

	int res = W25QXX_RES_OK;
	const uint8_t *ptr = (const uint8_t *)buff;
	uint32_t sector_addr  = w25qxx_get_sector_start_addr(addr); /*Yazim yapilacak adresin sektor baslangic adresini hesapla*/
	uint32_t sector_index = (addr % 4096);                     /* Bu sektore addr den itibaren ne kadar yazim yapilabilir*/
	uint32_t sector_len   = 4096 - sector_index;               /* addr sektorun hangi indisden basliyor*/
	uint32_t buff_index   = 0;
	uint32_t len;

	while(buff_len)
	{
		if(buff_len < sector_len){
			len = buff_len;}
		else{
			len = sector_len;
		}

		w25qxx_read_buff(sector_addr,  cache_mem.data, W25QXX_SECTOR_SIZE);
		memcpy(&cache_mem.data[sector_index], (ptr + buff_index), len);
		cache_mem.sector_addr = sector_addr;
		cache_mem.dirty       = 1;
		cache_mem.valid       = 1;

		bool success = false;

		for(int attempt = 0; attempt < 3; attempt++)
		{
			bsp_kick_wdt();
			int erase_ok = w25qxx_erase_sector(sector_addr);

			if(erase_ok != W25QXX_RES_OK)
			{
				CCSLOG(XCOLOR_RED, "Flash Sector erase fail, try again. 0x%X", sector_addr);
				continue;
			}

			if(w25qxx_write_sector(sector_addr, cache_mem.data, W25QXX_SECTOR_SIZE) != W25QXX_RES_OK)
			{
				CCSLOG(XCOLOR_RED, "Flash Sector write fail, try again. 0x%X", sector_addr);
			}
			else
			{
				bsp_kick_wdt();
				if(w25qxx_verify(sector_addr, cache_mem.data, W25QXX_SECTOR_SIZE) == W25QXX_RES_OK)
				{
					cache_mem.dirty = 0;
					success = true;
					break; // Dogrulama basarili
				}
				else
				{
					CCSLOG(XCOLOR_RED, "Flash Sector verify fail, try again. 0x%X", sector_addr);
				}
			}
		}

		if(success == false)
		{
			CCSLOG(XCOLOR_RED, "Flash Sector write fail ! 0x%X", sector_addr);
			res = W25QXX_RES_WRITE_FAIL;
			break;
		}

		buff_index   += len;         //data ptr
		buff_len     -= len;         //Kalan veri miktari
		sector_addr  += W25QXX_SECTOR_SIZE; //Sonrali sektor adresi
		sector_index = 0;            //Artik yeni sektordeyiz ve sektore en basindan itibaren yazabiliriz.
	}

	return res;
}

void printf_buff(uint8_t *buff, uint32_t len, uint32_t addr)
{
	uint32_t row_len = 0;
	CSLOG_NODT("%06X: ", addr);
	for(int i = addr; i < (addr + len); i++)
	{
		row_len++;
		CSLOG_NODT("%02X ", *buff++);
		if(row_len == 16)
		{
			uint8_t *ptr = buff - 16;
			CSLOG_NODT("| ");
			for(int j = 0; j < 16; j++)
			{
				CSLOG_NODT("%c", isprint(*ptr) ? *ptr : '.');
				ptr++;
			}

			row_len = 0;
			if(((addr + len) - i) > 1)
				CSLOG_NODT("\r\n%06X: ", i+1);
			else
				CSLOG_NODT("\r\n");
		}
	}
	if(row_len)
	{
		uint8_t *ptr = buff - row_len;
		CSLOG_NODT("| ");
		for(int j = 0; j < 16; j++)
		{
			CSLOG_NODT("%c ", *ptr++);
		}

		row_len = 0;
		CSLOG_NODT("\r\n");
	}
	CSLOG_NODT("\r\n");
}

void w25qxx_test(void)
{
	uint8_t page[W25QXX_PAGE_SIZE] = {0};
	uint32_t addr = 0;

	w25qxx_read_buff(addr, page, 256);
	printf_buff(page, W25QXX_PAGE_SIZE, addr);


	w25qxx_erase_sector(addr);
	w25qxx_read_buff(addr, page, 256);
	printf_buff(page, W25QXX_PAGE_SIZE, addr);


	for(int i = 0; i < W25QXX_PAGE_SIZE; i++)
	{
		page[i] = i ;
	}
	w25qxx_page_write(addr, page, W25QXX_PAGE_SIZE);
	w25qxx_verify(addr, page, W25QXX_PAGE_SIZE);

	memset(page, 0, W25QXX_PAGE_SIZE);
	w25qxx_read_buff(addr, page, W25QXX_PAGE_SIZE);
	printf_buff(page, W25QXX_PAGE_SIZE, addr);


	for(int i = 0; i < sizeof(page); i++)
	{
		page[i] = i ;
	}

	addr = 32768 * 5; //Block 5
	w25qxx_erase_block32(addr);
	w25qxx_write_buff(addr, page, sizeof(page));
	w25qxx_verify(addr, page, sizeof(page));

	memset(page, 0, sizeof(page));
	w25qxx_read_buff(addr, page, sizeof(page));
	printf_buff(page, sizeof(page), addr);


}
