/**
 * @file iec104_server.h
 * @brief Product services above the transport-independent IEC104 core.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_SERVER_H
#define IEC104_SERVER_H

#include "iec104_core.h"

#define IEC104_SERVER_VALUE_MAX 12U

/** Application information object, not an on-wire C struct.
 * ioa is the host integer address; the builder writes it as three LE bytes.
 * value contains encoded information-element bytes, excluding the IOA.
 * value_size selects the used prefix, not sizeof(value) or sizeof(this).
 * @code
 * Type     Offsets within value[]                      value_size
 *  1       [0:SIQ]                                         1
 * 13       [0..3:float LE][4:QDS]                           5
 * 30       [0:SIQ][1..7:CP56Time2a]                         8
 * 31       [0:DIQ][1..7:CP56Time2a]                         8
 * 36       [0..3:float LE][4:QDS][5..11:CP56Time2a]          12
 *
 * SQ=0 object on the wire: [IOA-low | IOA-mid | IOA-high | value...]
 * Object byte size = 3 + value_size (e.g. Type 36 is 15 bytes).
 * @endcode
 * SIQ: bit 0=SPI; DIQ: bits 1..0=DPI; QDS: bit 0=OV (overflow).
 * All share quality bits 7=IV, 6=NT, 5=SB, 4=BL; other low bits reserved.
 * IV=invalid, NT=not topical, SB=substituted, BL=blocked.
 * The provider owns this value buffer; the builder copies the used bytes.
 */
typedef struct
{
    uint32_t ioa;                           /**< Host integer, range 0..0xFFFFFF; serialized separately. */
    uint8_t type;                           /**< Selects the element layout above; not part of value[]. */
    uint8_t value[IEC104_SERVER_VALUE_MAX]; /**< Owned encoded element bytes. */
    size_t value_size;                      /**< Used prefix of value[], excluding IOA/header. */
} iec104_point_t;

typedef enum
{
    IEC104_POINT_VALUE,
    IEC104_POINT_SKIP,
    IEC104_POINT_END,
    IEC104_POINT_ERROR
} iec104_point_result_t;

/** Extension handlers use the same strict count/length and CA checks as
 * builtins. They run once per request, not once per send retry.
 * Return true with a complete response; false produces negative ACT_CON.
 * No session reentry. Static table/context must outlive the server.
 */
typedef struct
{
    uint8_t type;
    size_t value_size;
    bool (*handle)(void *context, const iec104_asdu_t *request, iec104_builder_t *response);
} iec104_handler_t;

typedef struct
{
    uint16_t common_address;
    uint8_t originator;
    uint32_t reset_ioa;
    /** Hard bound on query enumeration, including skipped positions. */
    size_t point_limit;
    const iec104_handler_t *handlers;
    size_t handler_count;
} iec104_server_config_t;

typedef struct
{
    void *context;
    bool (*write)(void *context, const uint8_t *data, size_t length);
    /** Enumerate a QOI 20..24 without changing storage/cursors.
     * Ordinals are stable for a query. ERROR closes instead of sending
     * a misleading successful ACT_TERM. SKIP represents unconfigured IOA.
     */
    iec104_point_result_t (*read_point)(void *context, uint8_t qoi, size_t index, iec104_point_t *point);
    bool (*set_clock)(void *context, const uint8_t time[7]);
    /** Schedule the existing delayed flush/close/reboot service.
     * Called only after the positive reset response is accepted by write.
     */
    void (*request_reset)(void *context);
    void (*event)(void *context, iec104_core_event_t event);
    void (*delivered)(void *context, uint32_t tag);
} iec104_server_io_t;

typedef enum
{
    IEC104_QUERY_IDLE,
    IEC104_QUERY_CONFIRM,
    IEC104_QUERY_DATA,
    IEC104_QUERY_TERMINATE
} iec104_query_state_t;

typedef struct
{
    iec104_core_t core;
    iec104_server_config_t config;
    iec104_server_io_t io;
    iec104_builder_t response;
    iec104_builder_t query_frame;
    bool response_pending;
    bool reset_pending;
    bool overloaded;
    bool initialization_pending;
    bool initialization_sent;
    iec104_query_state_t query_state;
    size_t query_index;
    uint8_t query_qoi;
    uint8_t query_originator;
} iec104_server_t;

/** @brief Bind product services and initialize an independent slave server.
 * @param[out] server Caller-owned storage; initialize while disconnected.
 * @param[in] link_config Core window/timer settings, copied at init.
 * @param[in] config CA/OA, enumeration limit and optional handler-table reference.
 * @param[in] io Product/transport callbacks; write and read_point are required.
 * @return True on valid settings/callbacks; false leaves server unchanged.
 * @note The config/io structs are copied. Referenced handler tables and callback
 * context remain caller-owned and must stay valid while the server is used.
 */
bool iec104_server_init(iec104_server_t *server, const iec104_core_config_t *link_config,
                        const iec104_server_config_t *config, const iec104_server_io_t *io);
/** @brief Begin a new TCP session and arm one initialization ASDU after START.
 * @param[in,out] server Initialized server, not a replacement for socket open.
 * @param[in] now_ms Current monotonic millisecond tick.
 */
void iec104_server_open(iec104_server_t *server, uint32_t now_ms);
/** @brief Record socket closure and cancel replies/query state without delivery.
 * @param[in,out] server Initialized server; NULL is harmless.
 */
void iec104_server_close(iec104_server_t *server);
/** @brief Process received TCP bytes and advance replies after each full APDU.
 * @param[in,out] server Open server whose core validates and assembles frames.
 * @param[in] data Borrowed TCP data; it may split or combine APDUs.
 * @param[in] length Number of available bytes.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @return Consumed prefix length; retain any suffix if the link is still open.
 * @note Builtin command dispatch may set the clock or schedule a reset response;
 * malformed input and bounded reply overload can close the session.
 */
size_t iec104_server_process_data(iec104_server_t *server, const uint8_t *data, size_t length, uint32_t now_ms);
/** @brief Service link timers and progress initialization, command replies or GI.
 * @param[in,out] server Initialized server; no sends while closed/stopped.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @note A GI data step performs at most 32 point reads and sends one query ASDU.
 * Prepared query bytes remain cached across K-window/transport backpressure.
 */
void iec104_server_poll(iec104_server_t *server, uint32_t now_ms);
/** @brief Build and send one homogeneous spontaneous point batch (COT=3).
 * @param[in,out] server Started server with no active query/queued reply/init.
 * @param[in] points Borrowed points, all of the same type and value_size.
 * @param[in] count Number of points, 1..127; the encoded ASDU must also fit.
 * @param[in] tag Caller ACK tag, zero for untracked live snapshots.
 * @return True on complete transport acceptance; false leaves session unchanged.
 * @note Rejection does not retain points; caller retries. Nonzero tags are
 * reported through delivered() on peer ACK, never on transport acceptance.
 */
bool iec104_server_publish(iec104_server_t *server, const iec104_point_t *points, size_t count, uint32_t tag);

#endif
/*** end of file ***/
