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
/** Default fits current monitor elements; override consistently in every translation unit.
 * Large/variable services may use the ASDU builder directly instead of increasing every point.
 */
#ifndef IEC104_POINT_VALUE_MAX
#define IEC104_POINT_VALUE_MAX 12U
#endif
_Static_assert((IEC104_POINT_VALUE_MAX >= 12U) && (IEC104_POINT_VALUE_MAX <= IEC104_CORE_ASDU_MAX - 9U),
               "Point capacity must fit builtin types and one ASDU object");

/** Host fields, never a packed CP56Time2a wire layout.
 * milliseconds includes seconds: 0..59999 within a minute.
 * year is 0..99 for 2000..2099; day_of_week is 0 (unspecified) or 1..7.
 * Monitoring may carry invalid=true, including an unavailable source clock.
 */
typedef struct
{
    uint16_t milliseconds;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t day_of_week;
    uint8_t month;
    uint8_t year;
    bool invalid;
    bool summer_time;
} iec104_time_t;

/** Host fields of Type 45 SCO; QU is 0..31, select=true is S/E=1. */
typedef struct
{
    bool state;
    bool select;
    uint8_t qualifier;
} iec104_single_command_t;

/** @brief Decode SCO without bitfield casts; reject reserved bit 1, preserving output. */
bool iec104_single_command_parse(iec104_single_command_t *command, uint8_t sco);

/** Owned information element, not a wire C struct.
 * Use point_set_*() for supported types; the builder serializes IOA separately.
 * Extension types may supply their own encoded value and value_size.
 * @code
 * Type    value[] layout                                  value_size
 *  1      [SIQ]                                               1
 * 13      [float LE:4][QDS]                                    5
 * 30      [SIQ][CP56Time2a:7]                                  8
 * 31      [DIQ][CP56Time2a:7]                                  8
 * 36      [float LE:4][QDS][CP56Time2a:7]                      12
 * SQ=0: [IOA:3][value:N] for each object; never send sizeof(this).
 * @endcode
 */
typedef struct
{
    uint32_t ioa;
    uint8_t type;
    uint8_t value[IEC104_POINT_VALUE_MAX];
    size_t value_size;
} iec104_point_t;

/** @brief Store an extension element already encoded by a type-specific library encoder.
 * @param[out] point Destination, unchanged on invalid arguments.
 * @param[in] ioa 24-bit object address.
 * @param[in] type Nonzero Type ID; naming a type does not implement its service semantics.
 * @param[in] value Borrowed element bytes, copied now; may alias point->value.
 * @param[in] length Element length excluding IOA, 1..IEC104_POINT_VALUE_MAX.
 * @return True on copy; false for NULL arguments, overflow, empty value or type zero.
 * @note Bounds only, not type-specific semantic validation. Prefer typed setters when available.
 * Variable-size objects in the same ASDU must still share one element width in this builder.
 */
bool iec104_point_set_encoded(iec104_point_t *point, uint32_t ioa, uint8_t type, const uint8_t *value, size_t length);

/** @brief Prepare Type 45 from host command fields, including the standard S/E bit.
 * @return False on invalid output/IOA/QU, leaving the previous point unchanged.
 */
bool iec104_point_set_single_command(iec104_point_t *point, uint32_t ioa, bool state, bool select, uint8_t qualifier);

/** @brief Create Type 1 or, with a timestamp, Type 30 from host values.
 * @param[out] point Owned element, unchanged on invalid arguments.
 * @param[in] ioa 24-bit information-object address.
 * @param[in] value Single-point state, independent of quality.
 * @param[in] quality IV/NT/SB/BL bits; low four bits must be zero.
 * @param[in] time Source time, or NULL for an untimed element.
 * @return True on success; false for NULL output, IOA overflow or reserved quality bits.
 * @note Source time fields must fit CP56Time2a; unavailable time uses invalid=true.
 */
bool iec104_point_set_single(iec104_point_t *point, uint32_t ioa, bool value, uint8_t quality,
                             const iec104_time_t *time);
/** @brief Create Type 13 or, with a timestamp, Type 36 from a binary32 measurement.
 * @param[out] point Owned element, unchanged on invalid arguments.
 * @param[in] ioa 24-bit information-object address.
 * @param[in] value Measurement, encoded without pointer aliasing or numeric conversion.
 * @param[in] quality QDS IV/NT/SB/BL/OV bits; bits 1..3 must be zero.
 * @param[in] time Source time, or NULL for an untimed element.
 * @return True on success; false for invalid output/address/quality.
 */
bool iec104_point_set_float(iec104_point_t *point, uint32_t ioa, float value, uint8_t quality,
                            const iec104_time_t *time);
/** @brief Create timed Type 31; value is the two-bit DPI state (0..3).
 * @return False on invalid output/address/state/quality or NULL time; output is unchanged.
 * @note Quality uses IV/NT/SB/BL only. All four DPI states are representable.
 */
bool iec104_point_set_double(iec104_point_t *point, uint32_t ioa, uint8_t value, uint8_t quality,
                             const iec104_time_t *time);
/** @brief Decode a validated clock-setting CP56Time2a to host fields.
 * @return False for NULL arguments or invalid clock input, leaving output unchanged.
 * @note Rejects IV=1, reserved bits and invalid dates like iec104_cp56_time_valid().
 */
bool iec104_time_parse(iec104_time_t *time, const uint8_t data[7]);

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
