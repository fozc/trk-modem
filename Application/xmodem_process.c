/*
 * xmodem_process.c
 *
 *  Created on: Apr 28, 2026
 *      Author: fatih.ozcan
 */
#include "xmodem_process.h"
#include "xmodem.h"
#include "bsp.h"
#include "spi_flash_organization.h"
#include "w25qxx.h"
#include "app_ipc.h"
#include "shell.h"
#include "console_logger.h"
#include "contiki.h"
#include "contiki_process.h"
#include <string.h>

#define XMODEM_CAN_BYTE           0x18U
#define XMODEM_INACTIVITY_TIMEOUT_MS  (10U * 60U * 1000U) /* 10 minutes */
#define XMODEM_START_TIMEOUT_MS       (1U * 60U * 1000U)  /* 1 minute */

#define FIRMWARE_FLASH_AREA_SIZE  SPIFLASH_SECTION_SIZE(SPIFLASH_SECTION_FIRMWARE_A)

/* Minimal superblock read — only what we need to determine download address */
#define BOOTLOADER_SUPERBLOCK_MAGIC  0xB007000BUL
#define BOOT_FW_SECTION_A  0U
#define BOOT_FW_SECTION_B  1U

static uint32_t get_download_address(void)
{
	uint8_t buf[12];
	w25qxx_read_buff(SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_BOOTLOADER_SB), buf, sizeof(buf));

	uint32_t magic;
	memcpy(&magic, &buf[0], sizeof(magic));

	if (magic != BOOTLOADER_SUPERBLOCK_MAGIC)
	{
		/* Superblock invalid — default to Section A */
		CSLOG_ERR("Superblock magic invalid, defaulting to FW_A.\r\n");
		return SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A);
	}

	/* backup_section is at offset 8 in the superblock struct */
	uint8_t backup_section = buf[8];

	if (backup_section == BOOT_FW_SECTION_A)
	{
		return SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_B);
	}
	return SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A);
}

static const char * get_download_area_name(uint32_t addr)
{
	if (addr == SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_A))
	{
		return "Firmware_A";
	}
	else if (addr == SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_FIRMWARE_B))
	{
		return "Firmware_B";
	}
	else
	{
		return "Unknown";
	}
}

static uint8_t  s_rx_buff[4096];
static uint16_t s_rx_buff_index = 0U;
static uint32_t s_write_addr = 0U;
static bool     s_transfer_overflow = false;

static volatile bool s_xmodem_active = false;
static volatile bool s_transfer_started = false;
static volatile uint32_t s_last_activity_tick = 0U;
static bool s_download_complete = false;
static bool s_download_success = false;
static bool s_cslog_was_enabled = false;

/*
 * Xmodem event handler
 *
 * XMODEM_EVT_FILE_DOWNLOAD_STARTED and XMODEM_EVT_FILE_DOWNLOAD_CANCELED events
 * are called in UART ISR context, do not do heavy tasks here!
 */
static void xmodem_event_handler(uint8_t evt, const void *data, uint32_t size)
{
	static uint32_t total_bytes_received = 0U;

	/* Any event counts as activity */
	s_last_activity_tick = bsp_get_tick();

	switch(evt)
	{
	case XMODEM_EVT_FILE_DOWNLOAD_STARTED:
		/* ISR context -- keep minimal */
		s_transfer_started = true;
		total_bytes_received = 0U;
		s_write_addr = 0U;
		s_transfer_overflow = false;
		break;

	case XMODEM_EVT_FILE_DOWNLOAD_CANCELED:
		/* ISR context -- keep minimal */
		total_bytes_received = 0U;
		s_write_addr = 0U;
		s_rx_buff_index = 0U;
		s_transfer_overflow = false;
		break;

	case XMODEM_EVT_FILE_DOWNLOAD_COMPLETE:
		CSLOG("Download Complete: Size: %u\r\n", total_bytes_received);

		if (s_transfer_overflow)
		{
			CSLOG("ERROR: Transfer was aborted due to size overflow.\r\n");
			s_rx_buff_index = 0U;
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

		s_download_success = true;
		s_download_complete = true;

		break;

	case XMODEM_EVT_FILE_CHUNK_RECEIVED:
		if(total_bytes_received == 0U)
		{
			s_write_addr = get_download_address();
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
		CSLOG("Xmodem Chunk Received: Size: [%u] Total: [%u] Addr: [0x%00000X]\r\n",
		      size, total_bytes_received, s_write_addr + total_bytes_received);
		break;

	default:
		break;
	}
}



/*
 * Xmodem poll process
 *
 * Active only while an XMODEM transfer session is running. Polls the xmodem
 * state machine every 10 ms. Started by xmodem_start_mode() and stopped by
 * xmodem_stop_mode() (on manual stop, cancel or timeout). As a safety net it
 * also self-terminates if the active flag is cleared by any other path.
 */
PROCESS(xmodem_poll_process, "xmodem-poll");
PROCESS_THREAD(xmodem_poll_process, ev, data)
{
	(void)ev;
	(void)data;

	static struct etimer poll_timer;

	PROCESS_BEGIN();

	etimer_set(&poll_timer, CLOCK_SECOND / 100U); /* 10 ms poll period */

	while (s_xmodem_active)
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&poll_timer));
		etimer_reset(&poll_timer);

		xmodem_timer_process();
		xmodem_poll();
	}

	PROCESS_END();
}

bool xmodem_is_mode_active(void)
{
	return s_xmodem_active;
}

void xmodem_on_uart_rx(uint8_t data)
{
	s_last_activity_tick = bsp_get_tick();
	xmodem_rx_isr(data);
}

static void xmodem_stop_mode(void)
{
	s_xmodem_active = false;
	s_transfer_started = false;

	process_exit(&xmodem_poll_process);

	/* Restore cslog to previous state */
	if (s_cslog_was_enabled)
	{
		console_logger_set_enabled(true, false);
	}

	CSLOG("XMODEM mode deactivated.\r\n");
}

static void xmodem_start_mode(void)
{
	CSLOG("XMODEM mode activated. Waiting for file transfer...\r\n");
	uint32_t dl_addr = get_download_address();
	CSLOG("Target area: %s (0x%06X)\r\n", get_download_area_name(dl_addr), (unsigned)dl_addr);
	CSLOG("Start timeout: 1 min, inactivity timeout: 10 min.\r\n");

	/* Save and disable cslog */
	s_cslog_was_enabled = console_logger_is_enabled();
	console_logger_set_enabled(false, false);

	s_xmodem_active = true;
	s_transfer_started = false;
	s_download_complete = false;
	s_download_success = false;
	s_last_activity_tick = bsp_get_tick();

	/* Start the 10 ms poll process for this transfer session */
	if (!process_is_running(&xmodem_poll_process))
	{
		process_start(&xmodem_poll_process, NULL);
	}
}

PROCESS(xmodem_watchdog_process, "xmodem-wd");
PROCESS_THREAD(xmodem_watchdog_process, ev, data)
{
	static struct etimer timer;

	PROCESS_BEGIN();

	etimer_set(&timer, CLOCK_SECOND);

	while (1)
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
		etimer_reset(&timer);

		if (!s_xmodem_active)
		{
			continue;
		}

		/* Check for completed download */
		if (s_download_complete)
		{
			if (s_download_success)
			{
				CSLOG("Firmware download successful. Requesting update & reset...\r\n");
				/* Small delay for log to flush */
				etimer_set(&timer, CLOCK_SECOND * 2);
				PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
				app_ipc_request_update(true);
			}
			else
			{
				CSLOG_ERR("Firmware download failed.\r\n");
				xmodem_stop_mode();
			}
			continue;
		}

		/* Check inactivity timeout */
		if (!s_transfer_started)
		{
			/* Transfer hasn't started yet — use 1 minute start timeout */
			if ((bsp_get_tick() - s_last_activity_tick) >= XMODEM_START_TIMEOUT_MS)
			{
				CSLOG_ERR("XMODEM start timeout (1 min). No transfer initiated. Aborting.\r\n");
				xmodem_stop_mode();
			}
		}
		else
		{
			/* Transfer in progress — use 10 minute inactivity timeout */
			if ((bsp_get_tick() - s_last_activity_tick) >= XMODEM_INACTIVITY_TIMEOUT_MS)
			{
				CSLOG_ERR("XMODEM inactivity timeout (10 min). Aborting.\r\n");
				xmodem_stop_mode();
			}
		}
	}

	PROCESS_END();
}

bool xmodem_is_active(void)
{
	return s_xmodem_active;
}
static int xmodem_shell_handler(int argc, char *argv[])
{
	if (argc < 2)
	{
		CSLOG("XMODEM mode: %s\r\n", s_xmodem_active ? "ACTIVE" : "inactive");
		return 0;
	}

	if (strcmp(argv[1], "start") == 0)
	{
		if (shell_get_session_level() < SHELL_LVL_SUPER_USER)
		{
			CSLOG_ERR("Permission denied. Requires superuser.\r\n");
			return -1;
		}

		if (s_xmodem_active)
		{
			CSLOG("XMODEM mode already active.\r\n");
			return 0;
		}

		xmodem_start_mode();
		return 0;
	}

	if (strcmp(argv[1], "stop") == 0)
	{
		if (shell_get_session_level() < SHELL_LVL_SUPER_USER)
		{
			CSLOG_ERR("Permission denied. Requires superuser.\r\n");
			return -1;
		}

		if (!s_xmodem_active)
		{
			CSLOG("XMODEM mode is not active.\r\n");
			return 0;
		}

		xmodem_stop_mode();
		return 0;
	}

	if (strcmp(argv[1], "status") == 0)
	{
		uint32_t dl_addr = get_download_address();
		CSLOG("XMODEM mode: %s\r\n", s_xmodem_active ? "ACTIVE" : "inactive");
		CSLOG("Target area: %s (0x%06X)\r\n", get_download_area_name(dl_addr), (unsigned)dl_addr);
		return 0;
	}

	CSLOG("Unknown argument: %s\r\n", argv[1]);
	return -1;
}

void xmodem_app_init(void)
{
	xmodem_init((xmodem_config_t){
		.event_callback = xmodem_event_handler,
		.write_byte = bsp_putchr
	});

	process_start(&xmodem_watchdog_process, NULL);

	shell_register_command(&(shell_cmd_t){
		.cmd   = "xmodem",
		.desc  = "XMODEM file transfer\r\n"
		         "xmodem         - show status\r\n"
		         "xmodem status  - show status\r\n"
		         "xmodem start   - enter xmodem mode (superuser)\r\n"
		         "xmodem stop    - exit xmodem mode (superuser)",
		.level = SHELL_LVL_USER,
		.func  = xmodem_shell_handler
	});
}

