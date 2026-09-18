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
#define APP_IPC_OK_ALREADY_APPROVED 1
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
 * Writes an IPC message with self_test_passed = 1, bound to the
 * installed image identity (BL-21), to the IPC sector.  On the next
 * boot the bootloader will approve the active firmware only if the
 * binding matches, then back it up and clear error counters.
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
 * @brief Read the image identity (app_crc / app_size) from the EFW
 *        header stored in the SPI download section.
 *
 * Used by the download flows to bind their update request to the image
 * they actually transferred (BL-21 freshness binding).  Field offsets
 * match the bootloader's EFW header layout.
 *
 * @param[out] p_crc   App CRC32 from the header (plaintext CRC).
 * @param[out] p_size  App size from the header.
 *
 * @return 0 on success, -1 when the header is missing/invalid.
 */
int app_ipc_read_download_image_id(uint32_t *p_crc, uint32_t *p_size);

/**
 * @brief Request a firmware update.
 *
 * Writes an IPC message with self_test_passed = 0 and
 * requested_mode = BOOT_IPC_REQ_UPDATE_FW.  The bootloader will enter
 * UPDATE_FW mode.
 *
 * BL-21 binding: when expected_crc/expected_size are non-zero, the
 * bootloader installs from the download section only if its EFW header
 * carries this identity; a mismatch (stale/partial image) is refused
 * and the running firmware is kept.  0/0 = unbound, legacy semantics.
 *
 * @param[in] expected_crc  Bound image app_crc, 0 = unbound.
 * @param[in] expected_size Bound image app_size, 0 = unbound.
 * @param[in] do_reset  true = perform NVIC_SystemReset after write,
 *                      false = return after write.
 *
 * @return APP_IPC_OK on success.
 *         Does not return when do_reset is true and write succeeds.
 *         Negative error code on flash write failure.
 */
int app_ipc_request_update(uint32_t expected_crc, uint32_t expected_size, bool do_reset);

/**
 * @brief Request to stay in bootloader.
 *
 * Writes an IPC message with self_test_passed = 0 and
 * requested_mode = BOOT_IPC_REQ_STAY_IN_BL.  The bootloader will stay
 * in shell/XMODEM mode.
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
