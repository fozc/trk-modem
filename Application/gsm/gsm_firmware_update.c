/*
 * File: gsm_firmware_update.c
 * Created on: Dec 28, 2025
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * One sector buffer shared by HTTP and RFWU in cooperative context.
 */
#define CSLOG_MODULE LOG_MOD_GSM
#include "gsm_firmware_update.h"
#include "http_handlers.h"
#include "raw_tcp_fw_update.h"
#include "boot.h"
#include "bsp.h"
#include "w25qxx.h"
#include "gsm_log.h"
#include "reboot.h"
#include <string.h>

static uint32_t flash_address;
static uint32_t section_end;
/* Upper-layer offset advances only after an entire callback succeeds. */
static uint32_t accepted_offset;
static uint8_t fw_update_buffer[4096];
static uint32_t fw_update_buffer_index;

static uint32_t write_offset(void)
{
    return flash_address - boot_get_download_address() + fw_update_buffer_index;
}

static int fw_update_init(uint32_t total_size)
{
    if ((total_size == 0U) || (total_size > FIRMWARE_FLASH_AREA_SIZE) || !rfwu_cancel())
    {
        return -1;
    }
    flash_address = boot_get_download_address();
    section_end = flash_address + total_size;
    fw_update_buffer_index = 0U;
    accepted_offset = 0U;
    CCSLOG(XCOLOR_CYAN, "Download target: section %c (0x%08lX)\r\n",
           (flash_address == FIRMWARE_A_ADDRESS) ? 'A' : 'B', (unsigned long)flash_address);
    return 0;
}

static int fw_update_write_handler(uint32_t offset, const uint8_t *data, uint32_t size)
{
    const uint32_t base = boot_get_download_address();
    const uint32_t total_size = section_end - base;
    if ((data == NULL) || (size == 0U) || (offset != accepted_offset) ||
        (offset > total_size) || (size > total_size - offset))
    {
        return -1;
    }
    const uint32_t next_offset = offset + size;
    const uint32_t committed = flash_address - base;
    /* An earlier attempt may have committed a whole-sector prefix of
     * this same request. Skip that prefix; rebuild only the failed sector.
     * No extra sector buffer or retry queue is needed. */
    if (committed > offset)
    {
        const uint32_t skip = committed - offset;
        if (skip > size)
        {
            return -1;
        }
        data += skip;
        size -= skip;
    }
    uint32_t previous_index = fw_update_buffer_index;
    while (size > 0U)
    {
        const uint32_t space = (uint32_t)sizeof(fw_update_buffer) - fw_update_buffer_index;
        const uint32_t count = (size < space) ? size : space;
        memcpy(&fw_update_buffer[fw_update_buffer_index], data, count);
        fw_update_buffer_index += count;
        data += count;
        size -= count;
        if (fw_update_buffer_index == sizeof(fw_update_buffer))
        {
            if (w25qxx_write_buff(flash_address, fw_update_buffer, sizeof(fw_update_buffer)) != W25QXX_RES_OK)
            {
                fw_update_buffer_index = previous_index;
                CSLOG_ERR("Firmware update flash write failed\r\n");
                return -1;
            }
            flash_address += (uint32_t)sizeof(fw_update_buffer);
            fw_update_buffer_index = 0U;
            previous_index = 0U;
        }
    }
    accepted_offset = next_offset;
    return 0;
}

static int fw_update_finish_handler(uint32_t total_size)
{
    if ((total_size == 0U) || (section_end - boot_get_download_address() != total_size) ||
        (accepted_offset != total_size) || (write_offset() != total_size))
    {
        return -1;
    }
    if (fw_update_buffer_index != 0U)
    {
        if (w25qxx_write_buff(flash_address, fw_update_buffer, fw_update_buffer_index) != W25QXX_RES_OK)
        {
            return -1;
        }
        flash_address += fw_update_buffer_index;
        fw_update_buffer_index = 0U;
    }
    CSLOG("FW image stored in section %c (0x%08lX), not applied\r\n",
          (boot_get_download_address() == FIRMWARE_A_ADDRESS) ? 'A' : 'B',
          (unsigned long)boot_get_download_address());
    return 0;
}

static void fw_reboot_handler(void)
{
    reboot_system_delayed(3000U);
}

void gsm_firmware_update_init(void)
{
    const fw_update_callbacks_t callbacks =
    {
        .fw_init = fw_update_init,
        .fw_write = fw_update_write_handler,
        .fw_finish = fw_update_finish_handler,
        .fw_reboot = fw_reboot_handler
    };
    fw_update_register_callbacks(&callbacks);
}

static int fw_update_rfwu_init(uint32_t total_size, uint32_t resume_offset)
{
    if ((total_size == 0U) || (total_size > FIRMWARE_FLASH_AREA_SIZE) ||
        (resume_offset > total_size) ||
        ((resume_offset != total_size) && ((resume_offset % sizeof(fw_update_buffer)) != 0U)))
    {
        return -1;
    }
    /* A new RFWU HELLO takes the same writer and invalidates HTTP resume/apply. */
    fw_update_reset();
    const uint32_t base = boot_get_download_address();
    flash_address = base + resume_offset;
    section_end = base + total_size;
    fw_update_buffer_index = 0U;
    accepted_offset = resume_offset;
    return 0;
}

static int fw_update_rfwu_write(const uint8_t *data, uint32_t size)
{
    return fw_update_write_handler(accepted_offset, data, size);
}

void gsm_firmware_update_rfwu_init(void)
{
    static const rfwu_fw_ops_t ops =
    {
        .fw_init = fw_update_rfwu_init,
        .fw_write = fw_update_rfwu_write,
        .fw_finish = fw_update_finish_handler,
        .fw_reboot = fw_reboot_handler
    };
    rfwu_register_fw_ops(&ops);
}

/*** end of file ***/
