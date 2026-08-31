/*
 * i2c_slave.h
 *
 *  Created on: 5 Tem 2026
 *      Author: fatih
 */

#ifndef POWER_BOARD_I2C_SLAVE_H_
#define POWER_BOARD_I2C_SLAVE_H_

#include <stdbool.h>
#include <stdint.h>

/** Size of the slave register file (register-pointer address space).
 *  Covers the full PowerBoard (PROT_VER 0x09) map up to the power-block
 *  integrity byte at 0xB4 (0x00..0x5F telemetry, 0x60-0x71 restore,
 *  0x72-0x78 statblk, 0x80-0x9A lastgasp, 0xA0-0xB4 power). 0xC0 leaves
 *  headroom. */
#define I2C_SLAVE_REG_COUNT   ((uint8_t)192U)

/** PUSH blocks the master writes into the register file (PROT_VER 0x09). */
typedef enum
{
    I2C_SLAVE_BLK_NONE = 0,    /**< No block (sentinel).               */
    I2C_SLAVE_BLK_TELEMETRY,  /**< 0x00..0x5F, 96 B (master write).   */
    I2C_SLAVE_BLK_POWER,      /**< 0xA0..0xB4, 21 B (master write).   */
    I2C_SLAVE_BLK_LASTGASP    /**< 0x80..0x9A, 27 B (master write).   */
} i2c_slave_block_t;

/**
 * @brief I2C slave diagnostic counters (updated in the ISR context).
 *
 * Lets the bus be diagnosed without breakpoints in the ISR, which would
 * stall the clock and abort the master transaction.
 */
typedef struct
{
    uint32_t addr_write;   /**< Address matches, master writing.        */
    uint32_t addr_read;    /**< Address matches, master reading.        */
    uint32_t rx_bytes;     /**< Bytes received (pointer + data).        */
    uint32_t tx_bytes;     /**< Bytes transmitted to the master.        */
    uint32_t listen_cplt;  /**< STOP / listen-complete events.          */
    uint32_t err_af;       /**< Acknowledge-failure (normal read NACK). */
    uint32_t err_berr;     /**< Bus errors (spurious START/STOP).       */
    uint32_t err_arlo;     /**< Arbitration-lost errors.                */
    uint32_t err_ovr;      /**< Overrun/underrun errors.                */
    uint32_t err_other;    /**< Any other error bits.                   */
    uint32_t recover;      /**< Hard-error recoveries (peripheral re-init). */
    uint32_t last_error;   /**< Last raw HAL_I2C error code.            */
} i2c_slave_stats_t;

void i2c_slave_init(void);

/**
 * @brief Read a single register byte written by the I2C master.
 *
 * @param[in] reg_addr  Register address (0..I2C_SLAVE_REG_COUNT-1).
 *
 * @return Register value, or 0 if @p reg_addr is out of range. A single
 *         byte load is atomic on Cortex-M, so no critical section is
 *         needed for one byte.
 */
uint8_t i2c_slave_read_reg(uint8_t reg_addr);

/**
 * @brief Atomically test-and-clear a PUSH block "written" flag.
 *
 * The slave ISR sets a per-block dirty flag at STOP when the master
 * completes a write that matches a known PUSH block (telemetry / power /
 * lastgasp). This call reads and clears it so the application can process
 * the freshly written block from thread context (not the ISR).
 *
 * @param[in] b  Block to test.
 * @return true if the block was written since the last call; the flag is
 *         cleared on return so each write is processed exactly once.
 */
bool i2c_slave_take_block(i2c_slave_block_t b);

/**
 * @brief Copy a contiguous, tear-free snapshot of the register file.
 *
 * The copy runs with interrupts masked so the I2C ISR cannot update the
 * register file mid-copy, guaranteeing a consistent multi-byte view.
 *
 * @param[out] p_dst  Destination buffer (must hold at least @p len bytes).
 * @param[in]  start  First register address to copy.
 * @param[in]  len    Number of bytes to copy. Addresses beyond the
 *                    register file are returned as 0.
 */
void i2c_slave_snapshot(uint8_t *p_dst, uint8_t start, uint8_t len);

/**
 * @brief Copy a tear-free snapshot of the diagnostic counters.
 *
 * @param[out] p_out  Destination stats structure (ignored if NULL).
 */
void i2c_slave_get_stats(i2c_slave_stats_t *p_out);

/**
 * @brief Reset all diagnostic counters to zero.
 */
void i2c_slave_clear_stats(void);

/**
 * @brief Write a block into the slave register file (read-back region).
 *
 * Used by the application to publish the bytes the master reads (restore
 * block, config, REC_ACK). The copy runs with interrupts masked so the
 * master cannot read a half-updated block mid-write.
 *
 * @param[in] start  First register address to write.
 * @param[in] src    Source buffer (ignored if NULL).
 * @param[in] len    Number of bytes; addresses beyond the file are dropped.
 */
void i2c_slave_write_block(uint8_t start, const uint8_t *src, uint8_t len);

#endif /* POWER_BOARD_I2C_SLAVE_H_ */
