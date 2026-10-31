/**
 * @file iec104_core.h
 * @brief Caller-owned single-peer IEC104 slave session, without TCP or HAL.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_CORE_H
#define IEC104_CORE_H

#include "iec104_asdu.h"

#define IEC104_CORE_WINDOW_MAX 64U

typedef enum
{
    IEC104_CORE_CLOSED,
    IEC104_CORE_STOPPED,
    IEC104_CORE_STARTED,
    IEC104_CORE_STOPPING
} iec104_core_state_t;

typedef enum
{
    IEC104_CORE_LINK_STARTED,
    IEC104_CORE_LINK_STOPPED,
    IEC104_CORE_PROTOCOL_ERROR,
    IEC104_CORE_APPLICATION_ERROR,
    IEC104_CORE_TIMEOUT,
    IEC104_CORE_DISCONNECTED
} iec104_core_event_t;

typedef struct
{
    uint8_t k;      /**< Max sent I frames awaiting peer N(R). */
    uint8_t w;      /**< Received I-frame threshold for an S acknowledgement. */
    uint32_t t1_ms; /**< Send/confirmation deadline, milliseconds. */
    uint32_t t2_ms; /**< Delayed receive acknowledgement, milliseconds. */
    uint32_t t3_ms; /**< Idle interval before TESTFR_ACT, milliseconds. */
} iec104_core_config_t;

/** @brief Bind one protocol session to its transport/application callbacks.
 * The core never calls GSM, RTC, NVRAM or server functions directly.
 * iec104_server_init() supplies the actual callback functions and context.
 * This table is copied at init; it is not a queue, thread or heap allocation.
 * All callbacks execute synchronously in the owning main context.
 * Return promptly; write reports backpressure with false instead of waiting.
 * They must not reenter process_data/poll/send/open/close on this session.
 */
typedef struct
{
    void *context; /**< Caller-owned instance passed unchanged to each callback. */
    /** @brief Offer a complete APDU to the transport; mandatory callback.
     * True means the WHOLE frame was copied/accepted in order.
     * False means ZERO bytes accepted; partial TCP writes belong below here.
     * The pointer expires on return. No callback may close the core inline.
     */
    bool (*write)(void *context, const uint8_t *frame, size_t length);
    /** @brief Deliver a parsed ASDU upward; mandatory, NOT a TCP RX hook.
     * Called after APCI sequence checks. The server binds process_asdu().
     * False defers dispatch without changing application state.
     * Core owns the ASDU bytes until this returns true or the link closes.
     */
    bool (*receive)(void *context, const iec104_asdu_t *asdu);
    /** @brief Notify link transitions/errors; optional for a bare core.
     * The modem adapter uses errors/timeouts to schedule socket closure.
     */
    void (*event)(void *context, iec104_core_event_t event);
    /** @brief Confirm peer acknowledgement of a tagged I frame; optional.
     * Nonzero caller tag, only after peer N(R) covers the tagged I frame.
     * This is not a transport-accepted or UART-send-complete notification.
     * No delivered callback on close; durable storage stays pending.
     */
    void (*delivered)(void *context, uint32_t tag);
} iec104_core_io_t;

typedef struct
{
    uint32_t sent_ms;
    uint32_t tag;
} iec104_core_pending_t;

/** Fields are public for static allocation, not application mutation.
 * One owner, no ISR access, no locks/atomics/heap. Times are monotonic ms.
 */
typedef struct
{
    iec104_core_config_t config;
    iec104_core_io_t io;
    iec104_core_state_t state;
    uint16_t send_sequence;
    uint16_t receive_sequence;
    uint16_t acknowledged;
    uint16_t received_pending;
    iec104_core_pending_t pending[IEC104_CORE_WINDOW_MAX];
    size_t pending_head;
    size_t pending_count;
    uint8_t rx[IEC104_CORE_FRAME_MAX];
    size_t rx_length;
    bool deferred;
    uint8_t control_pending;
    uint32_t control_since;
    uint32_t now_ms;
    uint32_t idle_since; /**< Last complete received APDU (or TCP open), used by T3. */
    uint32_t receive_since;
    uint32_t test_since;
    bool test_pending;
} iec104_core_t;

/** @brief Initialize caller-owned session storage without opening TCP.
 * @param[out] core Session to initialize; allocate statically or in caller storage.
 * @param[in] config Window/timer settings, copied by value.
 * @param[in] io Callback table, copied by value; its context remains caller-owned.
 * @return True on valid configuration; false leaves core untouched.
 * @pre Use new storage or a closed session; do not reinitialize an active link.
 * @note write and receive are required. t2 < t1 is required.
 * Intervals must be 1..INT32_MAX; poll at least once per interval.
 */
bool iec104_core_init(iec104_core_t *core, const iec104_core_config_t *config, const iec104_core_io_t *io);
/** @brief Start a fresh TCP session in STOPPED state, awaiting STARTDT_ACT.
 * @param[in,out] core Initialized session; previous pending work is discarded.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @note Call on TCP open, not on every STARTDT; resets the sequence numbers.
 */
void iec104_core_open(iec104_core_t *core, uint32_t now_ms);
/** @brief Record a physical disconnect and discard pending session work.
 * @param[in,out] core Session to close; NULL and repeated close are harmless.
 * @note Emits DISCONNECTED once. Pending tags are never reported delivered.
 * This function does not close the physical socket itself.
 */
void iec104_core_close(iec104_core_t *core);
/** @brief Abort on a provider/handler failure with APPLICATION_ERROR.
 * @param[in,out] core Session to abort; NULL is harmless.
 * @note The event consumer must schedule physical socket closure.
 */
void iec104_core_abort(iec104_core_t *core);
/** @brief Assemble TCP chunks, validate APCI and dispatch complete ASDUs.
 * @param[in,out] core Open session; retains a partial frame between calls.
 * @param[in] data Borrowed TCP bytes, valid during this call only.
 * @param[in] length Available byte count; chunk boundaries need not match APDUs.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @return Number of bytes consumed from the beginning of data.
 * @note If the session stays open, retain/retry any unconsumed suffix after poll.
 * Malformed framing/sequence errors or expired deadlines close the core and
 * report an event. A receive callback may defer ASDU dispatch by returning false.
 */
size_t iec104_core_process_data(iec104_core_t *core, const uint8_t *data, size_t length, uint32_t now_ms);
/** @brief Advance deadlines, retry control replies and deferred dispatch.
 * @param[in,out] core Initialized session; a closed session performs no work.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @note Handles t1/t2/t3 and pending START/STOP/TEST/S replies without blocking.
 * It does not retransmit I frames; transport retransmission belongs to TCP.
 */
void iec104_core_poll(iec104_core_t *core, uint32_t now_ms);
/** @brief Wrap an encoded ASDU in an I-frame and offer it to the transport.
 * @param[in,out] core Started session with available K-window capacity.
 * @param[in] asdu Encoded ASDU without APCI, borrowed for this call only.
 * @param[in] length ASDU byte length, including its six-byte header.
 * @param[in] tag Caller delivery ID; zero disables the delivered callback.
 * @return True if the complete frame was accepted; false if invalid/not ready/full.
 * @note On false, no sequence/tag/timer is committed; caller retains ASDU.
 * True means transport acceptance, not peer acknowledgement. A nonzero tag is
 * reported later via io.delivered only after a covering N(R).
 * Time is from the latest process_data/poll/open; call poll before publishing.
 */
bool iec104_core_send(iec104_core_t *core, const uint8_t *asdu, size_t length, uint32_t tag);

#endif
/*** end of file ***/
