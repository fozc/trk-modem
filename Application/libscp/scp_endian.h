/**
 * @file scp_endian.h
 * @brief Little-endian wire-format serialisation helpers for SCP.
 * @version 1.0.0
 * @author Fatih Ozcan
 *
 * SCP defines a fixed **little-endian** wire byte order for all multi-byte
 * fields (CRC-16, and any future >1-byte fields).  These helpers always
 * serialise / deserialise in little-endian regardless of the host CPU's
 * native byte order, ensuring interoperability between heterogeneous nodes.
 *
 * Usage:
 *   scp_pack_u16(p_buf, value);     // uint16_t → 2 bytes (LSB first)
 *   scp_unpack_u16(p_buf);          // 2 bytes (LSB first) → uint16_t
 */

#ifndef SCP_ENDIAN_H
#define SCP_ENDIAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Serialise a uint16_t into two consecutive bytes (little-endian).
 * --------------------------------------------------------------------------- */

/**
 * @brief Pack @p value into @p p_buf[0..1] in little-endian wire order.
 *
 * @param[out] p_buf  Destination buffer (at least 2 bytes).
 * @param[in]  value  16-bit value to write.
 */
static inline void scp_pack_u16(uint8_t *p_buf, uint16_t value)
{
    p_buf[0] = (uint8_t)((uint32_t)value & 0xFFU);          /* LSB first */
    p_buf[1] = (uint8_t)(((uint32_t)value >> 8U) & 0xFFU);
}

/* ---------------------------------------------------------------------------
 * Deserialise a uint16_t from two consecutive bytes (little-endian).
 * --------------------------------------------------------------------------- */

/**
 * @brief Unpack a 16-bit value from @p p_buf[0..1] in little-endian wire order.
 *
 * @param[in] p_buf  Source buffer (at least 2 bytes).
 * @return Reconstructed uint16_t value.
 */
static inline uint16_t scp_unpack_u16(const uint8_t *p_buf)
{
    return (uint16_t)((uint32_t)p_buf[0] | ((uint32_t)p_buf[1] << 8U));
}

#ifdef __cplusplus
}
#endif

#endif /* SCP_ENDIAN_H */
