/*
 * xmodem_process.c
 *
 *  Created on: Apr 28, 2026
 *      Author: fatih.ozcan
 */
#include <spi_flash_organization.h>
#include "xmodem_process.h"
#include "xmodem.h"
#include "bsp.h"
#include "w25qxx.h"
#include "boot.h"
#include "shell.h"
#include "console_logger.h"
#include <string.h>

#define XMODEM_CAN_BYTE         0x18U
#define XMODEM_WAIT_TIMEOUT_MS  30000U  /* Auto-stop if no transfer starts within 30s */

static uint8_t  s_rx_buff[4096];
static uint16_t s_rx_buff_index = 0U;
static uint32_t s_write_addr = 0U;
static bool     s_transfer_overflow = false;
static volatile bool s_xmodem_enabled = false;
static volatile bool s_transfer_active = false;
static uint32_t s_session_start_time = 0U;

/* ------------------------------------------------------------------ */
/*  Public query — called from ISR to route UART bytes                */
/* ------------------------------------------------------------------ */

bool xmodem_is_active(void)
{
    return s_xmodem_enabled;
}

/*
 * Xmodem event handler
 *
 * XMODEM_EVT_FILE_DOWNLOAD_STARTED and XMODEM_EVT_FILE_DOWNLOAD_CANCELED events
 * are called in UART ISR context, do not do heavy tasks here!
 */
static void xmodem_event_handler(uint8_t evt, const void *data, uint32_t size)
{
	static uint32_t total_bytes_received = 0U;
	switch(evt)
	{
	case XMODEM_EVT_FILE_DOWNLOAD_STARTED:
		/* ISR context -- keep minimal */
		total_bytes_received = 0U;
		s_write_addr = 0U;
		s_transfer_overflow = false;
		s_transfer_active = true;
		break;

	case XMODEM_EVT_FILE_DOWNLOAD_CANCELED:
		/* ISR context -- keep minimal */
		total_bytes_received = 0U;
		s_write_addr = 0U;
		s_rx_buff_index = 0U;
		s_transfer_overflow = false;
		s_transfer_active = false;
		s_xmodem_enabled = false;
		console_logger_set_enabled(true, false);
		break;

	case XMODEM_EVT_FILE_DOWNLOAD_COMPLETE:
		CSLOG("Download Complete: Size: %u\r\n", total_bytes_received);

		if (s_transfer_overflow)
		{
			CSLOG("ERROR: Transfer was aborted due to size overflow.\r\n");
			s_rx_buff_index = 0U;
			s_xmodem_enabled = false;
			console_logger_set_enabled(true, false);
			break;
		}

		if(s_rx_buff_index > 0U)
		{
			if(total_bytes_received < FIRMWARE_FLASH_AREA_SIZE)
			{
				w25qxx_erase_sector(s_write_addr);
				w25qxx_write_sector(s_write_addr, s_rx_buff, s_rx_buff_index);
				s_write_addr += 4096U;
			}
			s_rx_buff_index = 0U;
		}

		/* Mark download complete -- bootloader handles verification */
		boot_mark_new_firmware_downloaded(true);
		s_xmodem_enabled = false;
		console_logger_set_enabled(true, false);

		break;

	case XMODEM_EVT_FILE_CHUNK_RECEIVED:
		if(total_bytes_received == 0U)
		{
			s_write_addr = boot_get_download_address();
			s_rx_buff_index = 0U;
			s_transfer_overflow = false;
		}

		/* Abort if firmware exceeds max size. */
		if (s_transfer_overflow)
		{
			break;  /* Silently drop -- CAN already sent. */
		}

		if ((total_bytes_received + size) > FIRMWARE_FLASH_AREA_SIZE)
		{
			CSLOG("ERROR: Transfer exceeds max firmware size (%u bytes). "
			      "Aborting.\r\n", (unsigned)FIRMWARE_FLASH_AREA_SIZE);
			s_transfer_overflow = true;
			for (uint32_t can = 0U; can < 4U; can++)
			{
				bsp_putchr((int)XMODEM_CAN_BYTE);
			}
			break;
		}

		if(size + s_rx_buff_index <= sizeof(s_rx_buff))
		{
			memcpy(&s_rx_buff[s_rx_buff_index], data, size);
			s_rx_buff_index += (uint16_t)size;

			if(s_rx_buff_index >= sizeof(s_rx_buff))
			{
				if(total_bytes_received < FIRMWARE_FLASH_AREA_SIZE)
				{
					w25qxx_erase_sector(s_write_addr);
					w25qxx_write_sector(s_write_addr, s_rx_buff, s_rx_buff_index);
					s_write_addr += 4096U;
				}
				s_rx_buff_index = 0U;
			}
		}
		else
		{
			CSLOG("Xmodem buffer overflow ! Size: %u  Buff Index: %u\r\n", size, s_rx_buff_index);
		}

		total_bytes_received += size;
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/*  Shell command: xmodem start | stop                                */
/* ------------------------------------------------------------------ */

static int xmodem_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        CSLOG("Usage: xmodem <start|stop>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "start") == 0)
    {
        if (s_xmodem_enabled)
        {
            CSLOG("Xmodem already active\r\n");
            return 0;
        }
        CSLOG("Xmodem receive started (timeout: %us). Send file now...\r\n",
              XMODEM_WAIT_TIMEOUT_MS / 1000U);
        console_logger_set_enabled(false, false);
        s_transfer_active = false;
        s_session_start_time = bsp_get_run_time();
        s_xmodem_enabled = true;
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0)
    {
        s_xmodem_enabled = false;
        console_logger_set_enabled(true, false);
        CSLOG("Xmodem stopped\r\n");
        return 0;
    }

    CSLOG("Usage: xmodem <start|stop>\r\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Process & init                                                    */
/* ------------------------------------------------------------------ */

static void xmodem_auto_stop(void)
{
	s_xmodem_enabled = false;
	s_transfer_active = false;
	console_logger_set_enabled(true, false);
	CSLOG("Xmodem: session timeout, auto-stopped\r\n");
}

void xmodem_process(void)
{
	if (!s_xmodem_enabled)
	{
		return;
	}

	/* Auto-stop if waiting too long for transfer to begin */
	if (!s_transfer_active)
	{
		if ((bsp_get_run_time() - s_session_start_time) >= XMODEM_WAIT_TIMEOUT_MS)
		{
			xmodem_auto_stop();
			return;
		}
	}

	static uint32_t last_time = 0U;

	if(bsp_get_run_time() - last_time < 10U){
		return;
	}
	last_time = bsp_get_run_time();
	xmodem_poll();
	xmodem_timer_process();
}

void xmodem_app_init(void)
{
	xmodem_init((xmodem_config_t){
		.event_callback = xmodem_event_handler,
		.write_byte = bsp_putchr
	});

	shell_register_command(&(shell_cmd_t){
		.cmd   = "xmodem",
		.desc  = "Xmodem file transfer (start/stop)",
		.level = SHELL_LVL_USER,
		.func  = xmodem_shell_handler
	});
}

