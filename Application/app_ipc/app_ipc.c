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
#include "boot.h"
#include "efw.h"
#include "w25qxx.h"
#include "crc32.h"
#include <string.h>
#include "console_logger.h"
#include "shell.h"
#include "elog.h"
/* CMSIS core for NVIC_SystemReset */
#include "stm32u3xx.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

int app_ipc_read_download_image_id(uint32_t *p_crc, uint32_t *p_size)
{
    uint8_t header[EFW_HEADER_SIZE];
    efw_t fw;

    if ((p_crc == NULL) || (p_size == NULL))
    {
        return -1;
    }

    /* Full v2 gate via the shared parser (byte-parity with the
     * bootloader): magic, file version and compression codec.  A v1 or
     * garbage header is rejected here so no update request is sent for
     * a package the bootloader would refuse anyway. */
    w25qxx_read_buff(boot_get_download_address(), header, sizeof(header));

    if (efw_parse(header, &fw) != 0)
    {
        return -1;
    }

    if ((fw.app_size == 0U) || (fw.app_crc == 0U))
    {
        return -1;
    }

    *p_size = fw.app_size;
    *p_crc  = fw.app_crc;

    return 0;
}

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
 * @param[in] expected_crc     Bound image app_crc (BL-21), 0 = unbound.
 * @param[in] expected_size    Bound image app_size (BL-21), 0 = unbound.
 * @param[in] do_reset          true = perform NVIC_SystemReset after write.
 * @return APP_IPC_OK on success (when do_reset is false).
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on failure.
 */
static int app_ipc_send_and_reset(uint8_t  self_test_passed,
                                  uint8_t  requested_mode,
                                  uint32_t expected_crc,
                                  uint32_t expected_size,
                                  bool     do_reset)
{
    boot_ipc_t msg;
    (void)memset(&msg, 0, sizeof(msg));

    msg.magic            = BOOT_IPC_MAGIC;
    msg.self_test_passed = self_test_passed;
    msg.requested_mode   = requested_mode;
    msg.expected_fw_crc  = expected_crc;
    msg.expected_fw_size = expected_size;
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
        return APP_IPC_OK_ALREADY_APPROVED; /* Already approved -- skip write. */
    }

    /* BL-21: bind the approval to the installed image identity read from
     * the superblock, so a stale message cannot approve a different
     * trial firmware.  Unbound (0/0) fallback when the identity is not
     * available. */
    uint32_t expected_crc  = 0U;
    uint32_t expected_size = 0U;
    const fw_info_t *p_installed = boot_get_installed_fw_info();

    if ((p_installed != NULL) && (p_installed->size != 0U) && (p_installed->fw_crc != 0U))
    {
        expected_crc  = p_installed->fw_crc;
        expected_size = p_installed->size;
    }

    /* Log before the send: with do_reset the IPC call never returns. */
    elog_log_fw_approved();
    return app_ipc_send_and_reset(1U, BOOT_IPC_REQ_NONE, expected_crc, expected_size, do_reset);
}

int app_ipc_request_update(uint32_t expected_crc, uint32_t expected_size, bool do_reset)
{
    /* Do NOT send self_test_passed here.  Approval + mode-change in
     * a single IPC message causes the bootloader to flip backup_section
     * before processing UPDATE_FW, which makes it install from the
     * wrong (old) firmware section. */
    return app_ipc_send_and_reset(0U, BOOT_IPC_REQ_UPDATE_FW, expected_crc, expected_size, do_reset);
}

int app_ipc_request_stay_in_bootloader(bool do_reset)
{
    /* Same rationale as app_ipc_request_update — never piggyback
     * approval onto a mode-change request. */
    return app_ipc_send_and_reset(0U, BOOT_IPC_REQ_STAY_IN_BL, 0U, 0U, do_reset);
}

/* ------------------------------------------------------------------ */
/*  Shell command                                                     */
/* ------------------------------------------------------------------ */

static int boot_shell_handler(int argc, char *argv[])
{
    if (argc < 2)
    {
        CSLOG("Usage: boot <stay|update|status>\r\n");
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
        elog_log_fw_update(ELOG_FW_SRC_BOOT_CMD, ELOG_FW_RESULT_START, 0U);
        /* Deliberately unbound (0/0): the operator's intent is "install
         * whatever is in the download section" (BL-21 legacy semantics). */
        int ret = app_ipc_request_update(0U, 0U, true);
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
                 "\tboot stay           - reset & stay in bootloader\r\n"
                 "\tboot update         - reset & enter update mode\r\n"
                 "\tboot status         - show firmware approval status",
        .level = SHELL_LVL_SUPER_USER,
        .func  = boot_shell_handler
    });
}
