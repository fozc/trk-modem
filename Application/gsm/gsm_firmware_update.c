/*
 * gsm_firmware_update.c
 *
 *  Created on: Dec 28, 2025
 *      Author: fatih
 */
#include "gsm_firmware_update.h"
#include "http_handlers.h"
#include "raw_tcp_fw_update.h"
#include "boot.h"
#include "bsp.h"
#include "w25qxx.h"
#include "gsm_log.h"
#include "reboot.h"

static uint32_t flash_address = FIRMWARE_A_ADDRESS;
static uint8_t fw_update_buffer[4096] = {0}; 
static uint32_t fw_update_buffer_index = 0; 

static int fw_update_init(uint32_t total_size)
{
	GSM_LOG_INF_C(XCOLOR_CYAN, "Firmware update init, total size: %d bytes\r\n", total_size);

	/* Reject images that cannot fit in a firmware section before accepting the
	 * transfer (the per-write guard would otherwise fail only mid-download). */
	if(total_size > FIRMWARE_FLASH_AREA_SIZE)
	{
		GSM_LOG_ERR("Firmware update init error: size %lu exceeds max %lu bytes\r\n",
		            (unsigned long)total_size, (unsigned long)FIRMWARE_FLASH_AREA_SIZE);
		return -1;
	}

	flash_address = boot_get_download_address();
	fw_update_buffer_index = 0;

	GSM_LOG_INF_C(XCOLOR_CYAN, "Download target: section %c (0x%08lX)\r\n",
	       (flash_address == FIRMWARE_A_ADDRESS) ? 'A' : 'B',
	       (unsigned long)flash_address);

	return 0;
}

static int fw_update_write_handler(uint32_t offset, const uint8_t *data, uint32_t size)
{
	GSM_LOG_INF_C(XCOLOR_CYAN, "Firmware update write, offset: %d, size: %d bytes\r\n", offset, size);

	(void)offset;   /* offset kullanılmıyor; yazım konumu flash_address ile ilerler */

	if((data == NULL) || (size == 0U))
	{
		return 0;
	}

	while(size > 0U)
	{
		uint32_t space   = 4096U - fw_update_buffer_index;
		uint32_t to_copy = (size < space) ? size : space;   /* to_copy ∈ [1, 4096] */

		if(flash_address >= (FIRMWARE_A_ADDRESS + FIRMWARE_FLASH_AREA_SIZE))
		{
			GSM_LOG_ERR("Firmware update write error: Exceeded maximum firmware size!\r\n");
			return -1;
		}

		memcpy(&fw_update_buffer[fw_update_buffer_index], data, to_copy);
		fw_update_buffer_index += to_copy;
		data += to_copy;
		size -= to_copy;

		if(fw_update_buffer_index >= 4096U)
		{
			int res = w25qxx_write_buff(flash_address, fw_update_buffer, 4096);
			if(res != W25QXX_RES_OK)
			{
				GSM_LOG_ERR("Firmware update write error: Failed to write to flash! Error code: %d\r\n", res);
				return -1;
			}
			flash_address += 4096U;
			fw_update_buffer_index = 0U;
		}
	}

	return 0;
}

static int fw_update_finish_handler(uint32_t total_size)
{
	GSM_LOG_INF_C(XCOLOR_GREEN, "Firmware update finish, total size: %d bytes\r\n", total_size);

	/* Bug fix: flush the last partial sector if any bytes remain in the buffer.
	 * Previously the buffer was never flushed unless exactly 4096 bytes accumulated,
	 * causing the final (partial) sector of every firmware image to be silently lost. */
	if(fw_update_buffer_index > 0)
	{
		int res = w25qxx_write_buff(flash_address, fw_update_buffer, fw_update_buffer_index);
		if(res != W25QXX_RES_OK)
		{
			GSM_LOG_ERR("Firmware update finish: failed to flush last %lu bytes, error %d\r\n",
				   fw_update_buffer_index, res);
			return -1;
		}
		flash_address += fw_update_buffer_index;
		fw_update_buffer_index = 0;
	}

	return 0;
}

static void fw_reboot_handler(void)
{
	reboot_system_delayed(3000);
}

void gsm_firmware_update_init(void)
{
	/* HTTP firmware update callbacks */
	fw_update_callbacks_t callbacks = {
		.fw_init   = fw_update_init,
		.fw_write  = fw_update_write_handler,
		.fw_finish = fw_update_finish_handler,
		.fw_reboot = fw_reboot_handler
	};

	fw_update_register_callbacks(&callbacks);
}

/* ── RFWU flash callbacks ───────────────────────────────────────── */

/*
 * rfwu-specific init: sets the internal flash pointer to
 * FIRMWARE_A_ADDRESS + resume_offset so sequential fw_write calls land
 * in the right place.  resume_offset is always 4 KB-aligned.
 */
static int fw_update_rfwu_init(uint32_t total_size, uint32_t resume_offset)
{
	if(total_size > FIRMWARE_FLASH_AREA_SIZE)
	{
		GSM_LOG_ERR("RFWU init error: size %lu exceeds max %lu bytes\r\n",
		            (unsigned long)total_size, (unsigned long)FIRMWARE_FLASH_AREA_SIZE);
		return -1;
	}
	flash_address         = boot_get_download_address() + resume_offset;
	fw_update_buffer_index = 0U;
	return 0;
}

/* Thin wrapper: fw_update_write_handler ignores the offset parameter */
static int fw_update_rfwu_write(const uint8_t *p_data, uint32_t size)
{
	return fw_update_write_handler(0U, p_data, size);
}

void gsm_firmware_update_rfwu_init(void)
{
	static const rfwu_fw_ops_t rfwu_ops = {
		.fw_init   = fw_update_rfwu_init,
		.fw_write  = fw_update_rfwu_write,
		.fw_finish = fw_update_finish_handler,
		.fw_reboot = fw_reboot_handler,
	};

	rfwu_register_fw_ops(&rfwu_ops);
}
