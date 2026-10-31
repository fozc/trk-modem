/**
 * @file iec104_core.c
 * @brief Bounded TCP stream parser and IEC104 APCI state machine.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_core.h"

#include <limits.h>
#include <string.h>

#define CONTROL_START 1U
#define CONTROL_STOP 2U
#define CONTROL_TEST 4U

static bool timed_out(const iec104_core_t *core);

static uint16_t sequence_next(uint16_t value)
{
    return (uint16_t)((value + 1U) & 0x7FFFU);
}

static uint16_t sequence_distance(uint16_t first, uint16_t last)
{
    return (uint16_t)(((uint32_t)last + 32768U - first) & 0x7FFFU);
}

static bool elapsed(uint32_t now, uint32_t since, uint32_t interval)
{
    return (uint32_t)(now - since) >= interval;
}

static void notify(iec104_core_t *core, iec104_core_event_t event)
{
    if (core->io.event != NULL)
    {
        core->io.event(core->io.context, event);
    }
}

static void fail(iec104_core_t *core, iec104_core_event_t event)
{
    if (core->state != IEC104_CORE_CLOSED)
    {
        core->state           = IEC104_CORE_CLOSED;
        core->pending_count   = 0U;
        core->rx_length       = 0U;
        core->deferred        = false;
        core->control_pending = 0U;
        notify(core, event);
    }
}

bool iec104_core_init(iec104_core_t *core, const iec104_core_config_t *config, const iec104_core_io_t *io)
{
    if ((core == NULL) || (config == NULL) || (io == NULL) || (io->write == NULL) || (io->receive == NULL) ||
        (config->k == 0U) || (config->k > IEC104_CORE_WINDOW_MAX) || (config->w == 0U) ||
        (config->w > IEC104_CORE_WINDOW_MAX) || (config->t2_ms == 0U) || (config->t2_ms >= config->t1_ms) ||
        (config->t1_ms > INT32_MAX) || (config->t3_ms == 0U) || (config->t3_ms > INT32_MAX))
    {
        return false;
    }
    *core = (iec104_core_t){ .config = *config, .io = *io, .state = IEC104_CORE_CLOSED };
    return true;
}

void iec104_core_close(iec104_core_t *core)
{
    if (core != NULL)
    {
        fail(core, IEC104_CORE_DISCONNECTED);
    }
}

void iec104_core_abort(iec104_core_t *core)
{
    if (core != NULL)
    {
        fail(core, IEC104_CORE_APPLICATION_ERROR);
    }
}

void iec104_core_open(iec104_core_t *core, uint32_t now_ms)
{
    if (core == NULL)
    {
        return;
    }
    iec104_core_close(core);
    iec104_core_config_t config = core->config;
    iec104_core_io_t io         = core->io;
    *core = (iec104_core_t){ .config = config,
                             .io = io,
                             .state = IEC104_CORE_STOPPED,
                             .now_ms = now_ms,
                             .idle_since = now_ms };
}

static bool write_control(iec104_core_t *core, uint8_t code)
{
    /* S/U have APCI only: 68 04 C0 C1 C2 C3, six bytes in total.
     * S: 01 00 [N(R)<<1, LE]. U: named control code followed by three zeros.
     */
    uint8_t frame[6] = { 0x68U, 4U, code, 0U, 0U, 0U };
    if (code == IEC104_CONTROL_S_ACK)
    {
        iec104_write_u16(&frame[4], (uint16_t)(core->receive_sequence << 1U));
    }
    if (!core->io.write(core->io.context, frame, sizeof(frame)))
    {
        return false;
    }
    if (code == IEC104_CONTROL_S_ACK)
    {
        core->received_pending = 0U;
    }
    return true;
}

static void request_control(iec104_core_t *core, uint8_t flag)
{
    if (core->control_pending == 0U)
    {
        core->control_since = core->now_ms;
    }
    core->control_pending |= flag;
}

static void flush_control(iec104_core_t *core)
{
    if (((core->control_pending & CONTROL_TEST) != 0U) && write_control(core, IEC104_CONTROL_TESTFR_CON))
    {
        core->control_pending &= (uint8_t)~CONTROL_TEST;
    }
    if (((core->control_pending & CONTROL_START) != 0U) && write_control(core, IEC104_CONTROL_STARTDT_CON))
    {
        core->control_pending &= (uint8_t)~CONTROL_START;
        if (core->state == IEC104_CORE_STOPPED)
        {
            core->state = IEC104_CORE_STARTED;
            notify(core, IEC104_CORE_LINK_STARTED);
        }
    }
    if ((core->state == IEC104_CORE_STOPPING) && (core->received_pending != 0U))
    {
        (void)write_control(core, IEC104_CONTROL_S_ACK);
    }
    if (((core->control_pending & CONTROL_STOP) != 0U) && (core->pending_count == 0U) &&
        (core->received_pending == 0U) && write_control(core, IEC104_CONTROL_STOPDT_CON))
    {
        core->control_pending &= (uint8_t)~CONTROL_STOP;
        core->state = IEC104_CORE_STOPPED;
        notify(core, IEC104_CORE_LINK_STOPPED);
    }
}

/** @brief Apply a cumulative peer N(R) to the bounded sent-frame window.
 * @param[in,out] core Pending timestamps/tags and next unacknowledged sequence.
 * @param[in] sequence Peer next-expected sequence, already decoded from APCI.
 * @return True for an in-window or repeated latest ACK; false for a backward/future ACK.
 * @note Emits delivered for each covered nonzero frame tag, at most once per
 * frame. Partial ACK keeps the original timestamps of the remaining frames.
 */
static bool acknowledge(iec104_core_t *core, uint16_t sequence)
{
    uint16_t count = sequence_distance(core->acknowledged, sequence);
    if ((size_t)count > core->pending_count)
    {
        /* TCP preserves order: N(R) may repeat but must not go backward.
         * Modulo distance also rejects future ACKs and handles 32767 -> 0.
         */
        return false;
    }
    for (uint16_t index = 0U; index < count; index++)
    {
        uint32_t tag       = core->pending[core->pending_head].tag;
        core->pending_head = (core->pending_head + 1U) % IEC104_CORE_WINDOW_MAX;
        core->pending_count--;
        core->acknowledged = sequence_next(core->acknowledged);
        if ((tag != 0U) && (core->io.delivered != NULL))
        {
            core->io.delivered(core->io.context, tag);
        }
    }
    return true;
}

bool iec104_core_send(iec104_core_t *core, const uint8_t *asdu, size_t length, uint32_t tag)
{
    iec104_asdu_t view;
    if ((core == NULL) || (core->state != IEC104_CORE_STARTED) || (core->control_pending != 0U) ||
        (core->pending_count >= core->config.k) || !iec104_asdu_parse(&view, asdu, length))
    {
        return false;
    }
    /* APCI[6] + ASDU[length]. The length byte excludes 68 and itself,
     * so it counts control[4] + ASDU. Sequence bit 0 is always zero.
     * See process_frame() for the complete APDU/control-field map.
     */
    /* Yalniz gonderilecek baytlar yazilir; kullanilmayan tampon kuyrugu sifirlanmaz. */
    uint8_t frame[IEC104_CORE_FRAME_MAX];
    frame[0] = 0x68U;
    frame[1] = (uint8_t)(length + 4U);
    iec104_write_u16(&frame[2], (uint16_t)(core->send_sequence << 1U));
    iec104_write_u16(&frame[4], (uint16_t)(core->receive_sequence << 1U));
    memcpy(&frame[6], asdu, length);
    if (!core->io.write(core->io.context, frame, length + 6U))
    {
        return false;
    }
    size_t tail         = (core->pending_head + core->pending_count) % IEC104_CORE_WINDOW_MAX;
    core->pending[tail] = (iec104_core_pending_t){ core->now_ms, tag };
    core->pending_count++;
    core->send_sequence    = sequence_next(core->send_sequence);
    core->received_pending = 0U;
    return true;
}

static bool dispatch(iec104_core_t *core)
{
    iec104_asdu_t asdu;
    /* rx is a complete APDU; ASDU begins after the six-byte APCI prefix.
     * ASDU offset 0 is therefore rx[6], and its object payload is rx[12].
     */
    if (!iec104_asdu_parse(&asdu, &core->rx[6], core->rx_length - 6U))
    {
        fail(core, IEC104_CORE_PROTOCOL_ERROR);
        return false;
    }
    core->deferred = !core->io.receive(core->io.context, &asdu);
    if (!core->deferred)
    {
        core->rx_length = 0U;
    }
    return !core->deferred;
}

static bool process_i_frame(iec104_core_t *core)
{
    /* Control words are little-endian 15-bit sequence numbers shifted left
     * by one: rx[2..3]=N(S)<<1, rx[4..5]=N(R)<<1. N(R) names the next
     * expected I frame, cumulatively acknowledging the preceding frames.
     */
    uint16_t send_sequence    = iec104_read_u16(&core->rx[2]) >> 1U;
    uint16_t receive_sequence = iec104_read_u16(&core->rx[4]) >> 1U;
    if ((core->state != IEC104_CORE_STARTED) || (core->rx_length < 12U) || ((core->rx[4] & 1U) != 0U) ||
        (send_sequence != core->receive_sequence) || !acknowledge(core, receive_sequence))
    {
        fail(core, IEC104_CORE_PROTOCOL_ERROR);
        return false;
    }
    if (core->received_pending == 0U)
    {
        core->receive_since = core->now_ms;
    }
    if (core->received_pending < UINT16_MAX)
    {
        core->received_pending++;
    }
    core->receive_sequence = sequence_next(core->receive_sequence);
    bool accepted          = dispatch(core);
    if ((core->state != IEC104_CORE_CLOSED) && (core->received_pending >= core->config.w))
    {
        (void)write_control(core, IEC104_CONTROL_S_ACK);
    }
    return accepted;
}

static bool process_u_frame(iec104_core_t *core, uint8_t control)
{
    switch (control)
    {
        case IEC104_CONTROL_STARTDT_ACT:
            if (core->state == IEC104_CORE_STOPPING)
            {
                return false;
            }
            request_control(core, CONTROL_START);
            break;
        case IEC104_CONTROL_STOPDT_ACT:
            /* STOP supersedes an unsent START confirmation. */
            core->control_pending &= (uint8_t)~CONTROL_START;
            core->state = IEC104_CORE_STOPPING;
            request_control(core, CONTROL_STOP);
            break;
        case IEC104_CONTROL_TESTFR_ACT:
            request_control(core, CONTROL_TEST);
            break;
        case IEC104_CONTROL_TESTFR_CON:
            core->test_pending = false;
            break;
        default:
            return false;
    }
    return true;
}

/** Interpret a complete APDU. Offsets are relative to core->rx[0].
 * @code
 * offset    0      1        2      3      4      5        6..end
 *         [68] [length]  [ C0 | C1 | C2 | C3 ]       [ ASDU ... ]
 *         <------------- APCI: 6 bytes ------------> <--- I only --->
 *
 * I: C0 bit 0=0; words C0/C1=N(S)<<1, C2/C3=N(R)<<1 (LE).
 * S: C0=01, C1=00; C2/C3=N(R)<<1 (LE); no ASDU.
 * U: C0=START/STOP/TEST code, C1/C2/C3=00; no ASDU.
 *
 * length = bytes after offset 1: four control bytes + optional ASDU.
 * total APDU size = length + 2; maximum 255 bytes (length <= 253).
 * @endcode
 * U/S require length=4. The ASDU decoder sees only the slice at offset 6.
 */
static void process_frame(iec104_core_t *core)
{
    /* T3 measures receive inactivity; local TX cannot prove that the peer is alive. */
    core->idle_since = core->now_ms;
    if ((core->rx[2] & 1U) == 0U)
    {
        (void)process_i_frame(core);
        return;
    }
    bool valid = core->rx_length == 6U;
    if (valid && (core->rx[2] == IEC104_CONTROL_S_ACK))
    {
        /* STOPPED accepts U frames only; STOPPING still needs final I-frame ACKs. */
        valid = ((core->state == IEC104_CORE_STARTED) || (core->state == IEC104_CORE_STOPPING)) &&
                (core->rx[3] == 0U) && ((core->rx[4] & 1U) == 0U) &&
                acknowledge(core, iec104_read_u16(&core->rx[4]) >> 1U);
    }
    else if (valid)
    {
        valid = (core->rx[3] == 0U) && (core->rx[4] == 0U) && (core->rx[5] == 0U) && process_u_frame(core, core->rx[2]);
    }
    else
    {
        /* Invalid control-frame extent. */
    }
    if (!valid)
    {
        fail(core, IEC104_CORE_PROTOCOL_ERROR);
    }
    core->rx_length = 0U;
    if (core->state != IEC104_CORE_CLOSED)
    {
        flush_control(core);
    }
}

size_t iec104_core_process_data(iec104_core_t *core, const uint8_t *data, size_t length, uint32_t now_ms)
{
    /* TCP chunks are not frame boundaries: one chunk can contain part of
     * an APDU or several APDUs. rx holds one frame until rx[1]+2 bytes
     * have arrived; then process_frame consumes it before assembly resumes.
     */
    if ((core == NULL) || (data == NULL))
    {
        return 0U;
    }
    core->now_ms = now_ms;
    if ((core->state != IEC104_CORE_CLOSED) && timed_out(core))
    {
        fail(core, IEC104_CORE_TIMEOUT);
        return 0U;
    }
    size_t consumed = 0U;
    while ((consumed < length) && !core->deferred && (core->state != IEC104_CORE_CLOSED))
    {
        core->rx[core->rx_length++] = data[consumed++];
        if (((core->rx_length == 1U) && (core->rx[0] != 0x68U)) ||
            ((core->rx_length == 2U) && ((core->rx[1] < 4U) || (core->rx[1] > 253U))))
        {
            fail(core, IEC104_CORE_PROTOCOL_ERROR);
        }
        else if ((core->rx_length >= 6U) && (core->rx_length == (size_t)core->rx[1] + 2U))
        {
            process_frame(core);
        }
        else
        {
            /* Retain an incomplete frame across TCP segments. */
        }
    }
    return consumed;
}

static bool timed_out(const iec104_core_t *core)
{
    if ((core->pending_count != 0U) &&
        elapsed(core->now_ms, core->pending[core->pending_head].sent_ms, core->config.t1_ms))
    {
        return true;
    }
    if (core->test_pending && elapsed(core->now_ms, core->test_since, core->config.t1_ms))
    {
        return true;
    }
    if ((core->control_pending != 0U) && elapsed(core->now_ms, core->control_since, core->config.t1_ms))
    {
        return true;
    }
    /* Backpressure must not postpone an RX acknowledgement forever. */
    return (core->received_pending != 0U) && elapsed(core->now_ms, core->receive_since, core->config.t1_ms);
}

void iec104_core_poll(iec104_core_t *core, uint32_t now_ms)
{
    if ((core == NULL) || (core->state == IEC104_CORE_CLOSED))
    {
        return;
    }
    core->now_ms = now_ms;
    if (timed_out(core))
    {
        fail(core, IEC104_CORE_TIMEOUT);
        return;
    }
    flush_control(core);
    if (core->deferred)
    {
        (void)dispatch(core);
    }
    if ((core->received_pending >= core->config.w) ||
        ((core->received_pending != 0U) && elapsed(now_ms, core->receive_since, core->config.t2_ms)))
    {
        (void)write_control(core, IEC104_CONTROL_S_ACK);
    }
    if (!core->test_pending && elapsed(now_ms, core->idle_since, core->config.t3_ms))
    {
        if (write_control(core, IEC104_CONTROL_TESTFR_ACT))
        {
            core->test_pending = true;
            core->test_since   = now_ms;
        }
        else if (elapsed(now_ms, core->idle_since, core->config.t3_ms + core->config.t1_ms))
        {
            fail(core, IEC104_CORE_TIMEOUT);
        }
        else
        {
            /* The transport may become writable on the next poll. */
        }
    }
}
/*** end of file ***/
