/*
 * boot_ipc.h
 *
 * Shared IPC definitions for bootloader <-> application communication
 * via a dedicated SPI flash sector (one-shot protocol).
 *
 * Flow:
 *   1. Application writes boot_ipc_t to BOOT_IPC_FLASH_ADDR
 *   2. Application resets the MCU
 *   3. Bootloader reads and validates the IPC message in boot_init()
 *   4. Bootloader processes the message (approve FW, change mode, etc.)
 *   5. Bootloader erases the IPC sector
 *
 * If the sector is empty (all 0xFF), no erase occurs — no flash wear.
 *
 *  Created on: Apr 30, 2026
 *      Author: fatih.ozcan
 */

#ifndef BOOT_IPC_H
#define BOOT_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "spi_flash_organization.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */
#define BOOT_IPC_MAGIC          0x1BCD474AUL
#define BOOT_IPC_FLASH_ADDR     SPIFLASH_SECTION_ADDR(SPIFLASH_SECTION_IPC)

/* Requested boot mode codes */
#define BOOT_IPC_REQ_NONE       0x00U
#define BOOT_IPC_REQ_UPDATE_FW  0x01U
#define BOOT_IPC_REQ_STAY_IN_BL 0x02U

/* ------------------------------------------------------------------ */
/*  IPC message structure — written to SPI flash by application       */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint32_t magic;             /* Must be BOOT_IPC_MAGIC               */
    uint8_t  self_test_passed;  /* 1 = application passed self-test     */
    uint8_t  requested_mode;    /* BOOT_IPC_REQ_xxx                     */
    uint8_t  _reserved[2];
    uint32_t crc;               /* CRC32 of all preceding fields        */
} __attribute__((packed)) boot_ipc_t;

#ifdef __cplusplus
}
#endif

#endif /* BOOT_IPC_H */
