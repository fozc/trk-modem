/*
 * app_ipc.h
 *
 * Application-side IPC module for communicating with the bootloader
 * via the shared SPI flash IPC sector.
 *
 * Usage:
 *   - Call app_ipc_approve_firmware() after self-test passes to approve
 *     the currently running firmware.  The MCU will reset automatically.
 *
 *   - Call app_ipc_request_update() to request a firmware update on the
 *     next boot.  The MCU will reset automatically.
 *
 *   - Call app_ipc_request_stay_in_bootloader() to force the bootloader
 *     to stay in shell/XMODEM mode.  The MCU will reset automatically.
 *
 * All functions erase the IPC sector, write the message, verify it,
 * and then perform a system reset.  They do NOT return on success.
 * On failure they return a negative error code.
 *
 *  Created on: Apr 30, 2026
 *      Author: fatih.ozcan
 */

#ifndef APP_IPC_H
#define APP_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Status codes                                                      */
/* ------------------------------------------------------------------ */
#define APP_IPC_OK                  0
#define APP_IPC_ERR_ERASE          -1
#define APP_IPC_ERR_WRITE          -2
#define APP_IPC_ERR_VERIFY         -3

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Check if the currently installed firmware is approved.
 *
 * Reads the bootloader superblock from SPI flash and validates its
 * CRC before checking the is_approved field.
 *
 * @return true if firmware is approved, false otherwise.
 */
bool app_ipc_is_firmware_approved(void);

/**
 * @brief Approve the currently running firmware.
 *
 * If the firmware is already approved, returns APP_IPC_OK immediately
 * without writing to flash or resetting.
 *
 * Writes an IPC message with self_test_passed = 1 to the IPC sector.
 * On the next boot the bootloader will approve the active firmware,
 * back it up, and clear error counters.
 *
 * @param[in] do_reset  true = perform NVIC_SystemReset after write,
 *                      false = return after write.
 *
 * @return APP_IPC_OK on success (or already approved).
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on flash write failure.
 */
int app_ipc_approve_firmware(bool do_reset);

/**
 * @brief Request a firmware update.
 *
 * Writes an IPC message with self_test_passed = 1 and
 * requested_mode = BOOT_IPC_REQ_UPDATE_FW.  The bootloader will
 * approve the current firmware (if not already approved) and enter
 * UPDATE_FW mode.
 *
 * @param[in] do_reset  true = perform NVIC_SystemReset after write,
 *                      false = return after write.
 *
 * @return APP_IPC_OK on success.
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on flash write failure.
 */
int app_ipc_request_update(bool do_reset);

/**
 * @brief Request to stay in bootloader.
 *
 * Writes an IPC message with self_test_passed = 1 and
 * requested_mode = BOOT_IPC_REQ_STAY_IN_BL.  The bootloader will
 * approve the current firmware (if not already approved) and stay
 * in shell mode.
 *
 * @param[in] do_reset  true = perform NVIC_SystemReset after write,
 *                      false = return after write.
 *
 * @return APP_IPC_OK on success.
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on flash write failure.
 */
int app_ipc_request_stay_in_bootloader(bool do_reset);

/**
 * @brief Initialize the IPC shell commands.
 */
void app_ipc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_IPC_H */
