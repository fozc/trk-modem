/*
 * app_ipc.c
 *
 * Application-side IPC module -- writes one-shot messages to the
 * bootloader via a dedicated SPI flash sector.
 *
 *  Created on: Apr 30, 2026
 *      Author: fatih.ozcan
 */

#include "app_ipc.h"
#include "boot_ipc.h"
#include "w25qxx.h"
#include "crc32.h"
#include <string.h>
#include "console_logger.h"
#include "shell.h"
/* CMSIS core for NVIC_SystemReset */

#include "stm32u3xx.h"


/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Calculate CRC32 over the IPC message (all fields except crc).
 */
static uint32_t app_ipc_calc_crc(const boot_ipc_t *p_msg)
{
    crc32_t crc = crc32_init();
    crc = crc32_update(crc, p_msg, sizeof(*p_msg) - sizeof(p_msg->crc));
    return (uint32_t)crc32_finalize(crc);
}

/**
 * @brief Write an IPC message to the SPI flash IPC sector.
 *
 * Sequence: erase -> write -> verify.
 *
 * @param[in] p_msg  Pointer to the fully populated IPC message (with CRC).
 * @return APP_IPC_OK on success, negative error code on failure.
 */
static int app_ipc_write(const boot_ipc_t *p_msg)
{
    /* Erase IPC sector */
    if (w25qxx_erase_sector(BOOT_IPC_FLASH_ADDR) != 0)
    {
        return APP_IPC_ERR_ERASE;
    }

    /* Write message */
    if (w25qxx_write_buff(BOOT_IPC_FLASH_ADDR, p_msg, sizeof(*p_msg)) != 0)
    {
        return APP_IPC_ERR_WRITE;
    }

    /* Verify */
    if (w25qxx_verify(BOOT_IPC_FLASH_ADDR, p_msg, sizeof(*p_msg)) != 0)
    {
        return APP_IPC_ERR_VERIFY;
    }

    return APP_IPC_OK;
}

/**
 * @brief Build, write, and optionally reset with the given IPC parameters.
 *
 * @param[in] self_test_passed  1 = firmware approved, 0 = no approval.
 * @param[in] requested_mode   BOOT_IPC_REQ_xxx code.
 * @param[in] do_reset          true = perform NVIC_SystemReset after write.
 * @return APP_IPC_OK on success (when do_reset is false).
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on failure.
 */
static int app_ipc_send_and_reset(uint8_t self_test_passed,
                                  uint8_t requested_mode,
                                  bool    do_reset)
{
    boot_ipc_t msg;
    (void)memset(&msg, 0, sizeof(msg));

    msg.magic            = BOOT_IPC_MAGIC;
    msg.self_test_passed = self_test_passed;
    msg.requested_mode   = requested_mode;
    msg.crc              = app_ipc_calc_crc(&msg);

    int ret = app_ipc_write(&msg);
    if (ret != APP_IPC_OK)
    {
        return ret;
    }

    if (do_reset)
    {
        /* Message written successfully -- reset into bootloader. */
        __DSB();
        NVIC_SystemReset();
        /* Never reached */
    }

    return APP_IPC_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

bool app_ipc_is_firmware_approved(void)
{
    boot_ipc_t ipc;
    w25qxx_read_buff(BOOT_IPC_FLASH_ADDR, &ipc, sizeof(ipc));

    if (ipc.magic != BOOT_IPC_MAGIC)
    {
        return false;
    }

    /* Validate CRC. */
    crc32_t crc = crc32_init();
    crc = crc32_update(crc, &ipc, sizeof(ipc) - sizeof(ipc.crc));
    crc = crc32_finalize(crc);

    if ((uint32_t)crc != ipc.crc) /* Safe: crc32_t fits in uint32_t. */
    {
        return false;
    }

    return (ipc.self_test_passed != 0U);
}

int app_ipc_approve_firmware(bool do_reset)
{
    if (app_ipc_is_firmware_approved())
    {
        return APP_IPC_OK; /* Already approved -- skip write. */
    }

    return app_ipc_send_and_reset(1U, BOOT_IPC_REQ_NONE, do_reset);
}

int app_ipc_request_update(bool do_reset)
{
    uint8_t approved = app_ipc_is_firmware_approved() ? 1U : 0U;
    return app_ipc_send_and_reset(approved, BOOT_IPC_REQ_UPDATE_FW, do_reset);
}

int app_ipc_request_stay_in_bootloader(bool do_reset)
{
    uint8_t approved = app_ipc_is_firmware_approved() ? 1U : 0U;
    return app_ipc_send_and_reset(approved, BOOT_IPC_REQ_STAY_IN_BL, do_reset);
}

/* ------------------------------------------------------------------ */
/*  Shell command                                                     */
/* ------------------------------------------------------------------ */

static int boot_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        CSLOG("Usage: boot <staybootloader|update|status>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "stay") == 0)
    {
        CSLOG("[BOOT] Requesting stay in bootloader...\r\n");
        int ret = app_ipc_request_stay_in_bootloader(true);
        if (ret != APP_IPC_OK)
        {
            CSLOG_ERR("[BOOT] IPC write failed! err=%d\r\n", ret);
        }
        return ret;
    }

    if (strcmp(argv[1], "update") == 0)
    {
        CSLOG("[BOOT] Requesting firmware update...\r\n");
        int ret = app_ipc_request_update(true);
        if (ret != APP_IPC_OK)
        {
            CSLOG_ERR("[BOOT] IPC write failed! err=%d\r\n", ret);
        }
        return ret;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        CSLOG("[BOOT] Firmware approved: %s\r\n",
              app_ipc_is_firmware_approved() ? "YES" : "NO");
        return 0;
    }

    CSLOG("Unknown argument: %s\r\n", argv[1]);
    return -1;
}

void app_ipc_init(void)
{
    shell_register_command(&(shell_cmd_t){
        .cmd   = "boot",
        .desc  = "Boot control\r\n"
                 "boot staybootloader - reset & stay in bootloader\r\n"
                 "boot update         - reset & enter update mode\r\n"
                 "boot status         - show firmware approval status",
        .level = SHELL_LVL_SUPER_USER,
        .func  = boot_shell_handler
    });
}
