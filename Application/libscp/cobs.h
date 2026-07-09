/**
 * @file cobs.h
 * @brief COBS (Consistent Overhead Byte Stuffing) encode / decode.
 * @version 1.0.0
 * @author Fatih Ozcan
 *
 * Generic, protocol-independent implementation.
 */

#ifndef COBS_H
#define COBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Encode data using COBS.
 *
 * @param[in]  p_input      Raw input data (may be NULL when input_len == 0).
 * @param[in]  input_len    Number of bytes to encode.
 * @param[out] p_output     Output buffer (must hold at least input_len + 1 bytes).
 * @param[in]  output_size  Capacity of the output buffer.
 * @param[out] p_output_len Actual number of encoded bytes written.
 *
 * @return true on success, false on error (NULL pointer or buffer overflow).
 */
bool cobs_encode(const uint8_t *p_input,
                 size_t input_len,
                 uint8_t *p_output,
                 size_t output_size,
                 size_t *p_output_len);

/**
 * @brief Decode COBS-encoded data.
 *
 * @param[in]  p_input      COBS-encoded data (must not contain 0x00).
 * @param[in]  input_len    Number of encoded bytes.
 * @param[out] p_output     Output buffer for decoded data.
 * @param[in]  output_size  Capacity of the output buffer.
 * @param[out] p_output_len Actual number of decoded bytes written.
 *
 * @return true on success, false on error (NULL pointer, invalid data, or overflow).
 */
bool cobs_decode(const uint8_t *p_input,
                 size_t input_len,
                 uint8_t *p_output,
                 size_t output_size,
                 size_t *p_output_len);

/**
 * @brief Decode COBS-encoded data in-place.
 *
 * Decodes the buffer into itself. Safe because COBS decode always produces
 * output that is shorter than or equal to the input: write_idx <= read_idx
 * is maintained throughout the algorithm, so no byte is overwritten before
 * it has been read.
 *
 * @param[in,out] p_data      Buffer containing COBS-encoded data on entry;
 *                            decoded data on return (must not contain 0x00).
 * @param[in]     input_len   Number of encoded bytes in the buffer.
 * @param[out]    p_output_len Actual number of decoded bytes written.
 *
 * @return true on success, false on error (NULL pointer or invalid data).
 */
bool cobs_decode_inplace(uint8_t *p_data,
                         size_t input_len,
                         size_t *p_output_len);

#ifdef __cplusplus
}
#endif

#endif /* COBS_H */
