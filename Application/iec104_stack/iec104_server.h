/**
 * @file iec104_server.h
 * @brief Generic server services above the transport-independent IEC104 core.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_SERVER_H
#define IEC104_SERVER_H

#include "iec104_core.h"

/** Decoded application request. No product mapping or IOA enumeration lives in the server.
 * asdu.payload is borrowed and only available on the initial call; continuations retain
 * header fields, IOA and qualifier. time is decoded for Type 103. qualifier is QOI/QRP or the raw SCO; Type 45 also has host single_command fields.
 */
typedef struct
{
    iec104_asdu_t asdu;
    uint32_t ioa;
    uint8_t qualifier;
    iec104_time_t time;
    iec104_single_command_t single_command; /**< Decoded SCO for Type 45. */
    bool continuation; /**< Dahili GI devam cagrisi; kabloda yoktur, asdu.payload devamda kullanilamaz. */
} iec104_request_t;

/** One response chunk, initialized by the server before each application call.
 * Add typed points with iec104_response_add(). Set more=true only for an unfinished GI.
 * Empty accepted response means positive command echo or an empty/unfinished GI.
 * Prepared bytes are retained by the server on transport/window backpressure.
 */
typedef struct
{
    iec104_builder_t asdu;
    bool more;
} iec104_response_t;

/** @brief Uygulama callback sonucu; protokol COT kodu veya kablo uzerindeki bir alan degildir.
 * COT sabitleri iec104_cause_t icindedir. Server bu sonucu istek tipi ve asamasiyla
 * birlikte yorumlar; ornegin GI grubu desteklenmiyorsa negatif ACT_CON gonderir,
 * bilinmeyen komut tipi icin COT=44 kullanir. Bu nedenle COT ile bire bir enum degildir.
 */
typedef enum
{
    IEC104_REQUEST_UNSUPPORTED, /**< Uygulama bu istegi desteklemiyor; cevap istek turune baglidir. */
    IEC104_REQUEST_REJECTED,    /**< Bilinen istek reddedildi; negatif ACT_CON/DEACT_CON. */
    IEC104_REQUEST_UNKNOWN_IOA, /**< Tip biliniyor, IOA tanimli degil; negatif COT=47. */
    IEC104_REQUEST_ACCEPTED,    /**< Kabul edildi; server onay/veri/tamamlama akisina devam eder. */
    IEC104_REQUEST_ERROR        /**< Dahili hata; guvenilir cevap hazirlanamadi, server baglantiyi sonlandirir. */
} iec104_request_result_t;

/** @brief Process a validated request or prepare its next GI response chunk.
 * Application owns selection, source reads and continuation position. It must bound
 * its work per call and never reenter the server. Unsupported requests need no dummy
 * handler. Commands execute once; a retry of prepared bytes does not call this again.
 */
typedef iec104_request_result_t (*iec104_request_handler_t)(void *context, const iec104_request_t *request,
                                                            iec104_response_t *response);

typedef struct
{
    uint16_t common_address;
    uint8_t originator;
    uint32_t reset_ioa;
    bool reset_sbo;          /**< Require Type 45 Select/Execute for reset_ioa; reject direct Type 105 when true. */
    uint32_t sbo_timeout_ms; /**< Required nonzero and <=INT32_MAX when reset_sbo is enabled. */
} iec104_server_config_t;

typedef struct
{
    void *context;
    bool (*write)(void *context, const uint8_t *data, size_t length);
    iec104_request_handler_t handle_request; /**< Optional; NULL produces protocol errors, never dummy success. */
    /** Schedule an accepted reset after its last positive response enters transport (Type 45: ACT_TERM).
     * This is a post-response device action, not a second request-dispatch callback.
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

/** Reset cevabindan sonra yapilacak is; birlikte tutulan bayrak kombinasyonlari yoktur. */
typedef enum
{
    IEC104_RESET_NONE,
    IEC104_RESET_SEND_TERMINATION, /**< Type 45 ACT_CON sonrasi ACT_TERM gonder. */
    IEC104_RESET_SCHEDULE          /**< Son olumlu cevabin TX kabulunden sonra cihaz servisini cagir. */
} iec104_reset_state_t;

typedef struct
{
    iec104_core_t core;
    iec104_server_config_t config;
    iec104_server_io_t io;
    iec104_builder_t response;
    iec104_builder_t query_frame;
    bool response_pending;
    iec104_reset_state_t reset_state;
    bool overloaded;
    bool initialization_pending;
    bool initialization_sent;
    iec104_query_state_t query_state;
    bool query_more;
    uint8_t query_qoi;
    uint8_t query_originator;
    bool reset_selected;
    uint8_t reset_selected_command; /**< SCO without S/E, including value and QU. */
    uint8_t reset_selected_originator;
    uint32_t reset_selected_since;
} iec104_server_t;

/** @brief Append a typed point to the current response chunk.
 * @param[in,out] response Server-initialized chunk, only used during handle_request().
 * @param[in] point Typed information object with its own IOA.
 * @return False without mutation if invalid, full, or a different type/width.
 * @note On false with a nonempty chunk, application keeps that point/cursor for its next call.
 */
bool iec104_response_add(iec104_response_t *response, const iec104_point_t *point);

/** @brief Bind product services and initialize an independent slave server.
 * @param[out] server Caller-owned storage; initialize while disconnected.
 * @param[in] link_config Core window/timer settings, copied at init.
 * @param[in] config CA/OA, reset IOA and optional reset SBO window, copied at initialization.
 * @param[in] io Transport/lifecycle callbacks and one optional application request handler.
 * @return True on valid settings/callbacks; false leaves server unchanged.
 * @note Config/io are copied. The callback context remains caller-owned and must stay valid.
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
 * @note Each GI data poll invokes at most one application continuation and sends at most one ASDU.
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

/** @brief Publish one application-selected periodic batch with COT=1.
 * @param[in,out] server Started server with no pending request/initialization.
 * @param[in] points Same-type/value-width points prepared by the application.
 * @param[in] count Number of points, 1..127, fitting one ASDU.
 * @return True on transport acceptance. False leaves caller data and publication progress unchanged.
 * @note Application owns interval, content selection and continuation. No internal timer/queue is added.
 */
bool iec104_server_publish_periodic(iec104_server_t *server, const iec104_point_t *points, size_t count);

/** @brief Send one prepared spontaneous/periodic ASDU without an intermediate point array.
 * @param[in,out] server Started server with no active query, queued reply or initialization.
 * @param[in] asdu Borrowed builder; COT must be 1 or 3 and CA/OA must match server configuration.
 * @param[in] tag Peer-ACK delivery tag, zero for untracked publications.
 * @return True on whole-frame transport acceptance; false preserves builder and session state.
 * @note Checks header/object extent; the application owns type-specific value semantics.
 * The caller retains its cursor on false. No ASDU pointer is retained after return.
 */
bool iec104_server_publish_asdu(iec104_server_t *server, const iec104_builder_t *asdu, uint32_t tag);

#endif
/*** end of file ***/
