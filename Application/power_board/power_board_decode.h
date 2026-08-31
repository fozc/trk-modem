/**
 * @file  power_board_decode.h
 * @brief Pure PUSH-block decoders for the PowerBoard I2C protocol.
 *
 * Split out of power_board.c so the wire-format logic (integrity,
 * protocol-version gate, signed fields) is host-testable without the
 * Contiki/I2C shell around it. Input is a raw register-file snapshot;
 * output is the decoded struct from power_board.h.
 *
 * Reference: doc/PowerBoard_I2C_Protocol.md (Rev 1.0, PROT_VER 0x09);
 * block-layout authority: power-card repo, upper_board_reference/pwr_i2c_packets.h.
 */
#ifndef POWER_BOARD_POWER_BOARD_DECODE_H_
#define POWER_BOARD_POWER_BOARD_DECODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "power_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Salted XOR integrity over @c len bytes: XOR(b[0..len-1]) ^ 0x5A.
 *
 * Every block's LAST byte carries this value over all preceding bytes.
 * Shared by the PUSH decoders and the PULL-block builders.
 */
uint8_t power_board_xsum(const uint8_t *p_buf, uint8_t len);

/**
 * @brief Decode the telemetry block (0x00..0x5F, 96 B).
 *
 * Fields are always decoded. Validity requires BOTH the integrity byte
 * AND PROT_VER (0x1E) == POWER_BOARD_PROT_VER: XSUM catches corruption,
 * the version gate catches a field-shifted frame from another protocol
 * revision (single-contract rule, no compatibility layer).
 *
 * @param[in]  p_reg  Raw 96-byte snapshot of 0x00..0x5F.
 * @param[out] p_out  Decoded telemetry (fields filled even when invalid).
 * @return true when the frame is intact and at the expected PROT_VER.
 */
bool power_board_decode_telemetry(const uint8_t *p_reg,
                                  power_board_telemetry_t *p_out);

/**
 * @brief Decode the power block (0xA0..0xB4, 21 B).
 * @return true when the integrity byte matched.
 */
bool power_board_decode_power(const uint8_t *p_reg,
                              power_board_power_t *p_out);

/**
 * @brief Decode the last-gasp block (0x80..0x9A, 27 B).
 * @return true when marker (0xB5) and integrity byte matched.
 */
bool power_board_decode_lastgasp(const uint8_t *p_reg,
                                 power_board_lastgasp_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* POWER_BOARD_POWER_BOARD_DECODE_H_ */
