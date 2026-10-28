/**
 * @file iec104_asdu.h
 * @brief Fixed IEC104 profile: COT=2, CA=2, IOA=3; explicit wire encoding.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_ASDU_H
#define IEC104_ASDU_H

#include "iec104_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IEC104_CORE_ASDU_MAX 249U
#define IEC104_CORE_FRAME_MAX 255U

/** Parsed ASDU header plus a borrowed payload view, not a packed wire layout.
 * Payload excludes the six-byte ASDU header. No payload copy is made here;
 * the source buffer must remain valid while this view is used.
 * Do not send this struct or infer frame size from sizeof(iec104_asdu_t).
 */
typedef struct
{
    uint8_t type;           /**< ASDU[0]: Type ID, see iec104_type_id_t. */
    uint8_t count;          /**< ASDU[1] bits 6..0: object count. */
    uint8_t cause;          /**< ASDU[2] bits 5..0: COT without P/N/test. */
    uint8_t originator;     /**< ASDU[3]: OA, second byte of COT. */
    uint16_t address;       /**< ASDU[4..5]: CA decoded to host integer. */
    bool sequence;          /**< ASDU[1] bit 7: SQ, consecutive IOA encoding. */
    bool negative;          /**< ASDU[2] bit 6: P/N, true=negative response. */
    bool test;              /**< ASDU[2] bit 7: test-marked ASDU. */
    const uint8_t *payload; /**< Borrowed ASDU[6..end], including first IOA. */
    size_t payload_len;     /**< ASDU length minus six-byte header. */
} iec104_asdu_t;

/** Caller-owned encoded ASDU buffer; add failures leave it unchanged.
 * data[0..length) contains the wire ASDU, without the six-byte APCI prefix.
 * value_size is the information-element width, excluding the three-byte IOA.
 */
typedef struct
{
    uint8_t data[IEC104_CORE_ASDU_MAX]; /**< Encoded header + object payload. */
    size_t length;                      /**< Used bytes, no APCI included. */
    size_t value_size;                  /**< One value, excluding its IOA. */
    uint32_t last_ioa;                  /**< Last address; checks SQ increment. */
} iec104_builder_t;

/** @name Primitive wire access
 * @pre Non-NULL storage of at least 2/3/4 bytes respectively.
 * The ASDU decoder/builder establish bounds before using these primitives.
 * @{
 */
uint16_t iec104_read_u16(const uint8_t *data);
uint32_t iec104_read_ioa(const uint8_t *data);
void iec104_write_u16(uint8_t *data, uint16_t value);
void iec104_write_ioa(uint8_t *data, uint32_t value);
void iec104_write_float(uint8_t *data, float value);
/** @} */
/** @brief Decode the ASDU header and expose its payload without copying it.
 * @param[out] asdu Parsed view; unchanged on failure.
 * @param[in] data ASDU slice, not a complete APDU; frame[6] is ASDU data[0].
 * @param[in] length Available ASDU bytes, including the six-byte header.
 * @return True for a valid generic header/length/count; false otherwise.
 * @note data must outlive use of the parsed view; the payload is borrowed.
 * The implementation documents each byte/bit offset and an example packet.
 * Type-specific extent is checked with iec104_asdu_objects_valid().
 * Failure leaves the output untouched.
 */
bool iec104_asdu_parse(iec104_asdu_t *asdu, const uint8_t *data, size_t length);
/** @brief Validate object count, SQ addressing and exact payload length.
 * @param[in] asdu Parsed view from iec104_asdu_parse().
 * @param[in] value_size One information element, excluding its three-byte IOA.
 * @return True if the payload fits exactly and implied IOAs cannot overflow.
 * @note This checks extent/addressing, not the semantic value of each element.
 */
bool iec104_asdu_objects_valid(const iec104_asdu_t *asdu, size_t value_size);
/** @brief Start a caller-owned ASDU builder with an empty object list.
 * @param[out] builder Destination; unchanged if the arguments are invalid.
 * @param[in] header Type, COT, OA, CA, SQ and flags; its payload/count are unused.
 * @param[in] value_size Element width excluding IOA; fixed for this ASDU.
 * @return True on success; false if pointers/cause/element size are invalid.
 * @note The result becomes sendable after at least one successful add.
 */
bool iec104_builder_begin(iec104_builder_t *builder, const iec104_asdu_t *header, size_t value_size);
/** @brief Append one IOA/value pair and update the ASDU object count.
 * @param[in,out] builder Initialized builder; unchanged on failure.
 * @param[in] ioa Host address in the 24-bit range; consecutive for SQ=1.
 * @param[in] value Encoded element of builder->value_size bytes, copied now.
 * @return True on append; false on capacity/count/address/SQ violations.
 * @note No APCI is added here; pass data/length to iec104_core_send().
 */
bool iec104_builder_add(iec104_builder_t *builder, uint32_t ioa, const uint8_t *value);
/** @brief Check whether a CP56Time2a may be applied as a clock setting.
 * @param[in] time Seven encoded CP56Time2a bytes, or NULL.
 * @return True for valid date/time/reserved bits in years 2000..2099.
 * @note This performs no RTC write. It is not a monitoring-quality filter.
 * IV=1 is rejected. Day of week 0 means unspecified; 1..7 are accepted.
 */
bool iec104_cp56_time_valid(const uint8_t time[7]);

#endif
/*** end of file ***/
