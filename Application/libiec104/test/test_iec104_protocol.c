/*
 * test_iec104_protocol.c
 *
 * Host tests for the libiec104 protocol core, driven through a loopback
 * transport: every APDU the stack emits is captured in tx_log[] and can be
 * inspected byte by byte, while the SCADA side is simulated by feeding
 * frames into iec104_data_received().
 *
 * The transport can be told to reject frames, which is how the sequence
 * accounting, the k window, the STOPDT gate and the resumable fault
 * emission are exercised.
 *
 * Usage: make run   (Application/libiec104/test)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "iec104.h"
#include "iec104_types.h"
#include "mock_platform.h"

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                           \
    do                                                                   \
    {                                                                    \
        if ((cond) != 0)                                                 \
        {                                                                \
            test_pass++;                                                 \
            printf("PASS: %s\r\n", (name));                              \
        }                                                                \
        else                                                             \
        {                                                                \
            test_fail++;                                                 \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__); \
        }                                                                \
    } while (0)

/* ---------------------------------------------------------------- */
/* Loopback transport                                                */
/* ---------------------------------------------------------------- */

#define TX_LOG_MAX      256U
#define TX_FRAME_MAX    256U

static uint8_t  tx_log[TX_LOG_MAX][TX_FRAME_MAX];
static uint16_t tx_log_len[TX_LOG_MAX];
static uint16_t tx_count;

/* <0 accepts everything; otherwise the number of frames still accepted. */
static int transport_budget;

static iec104_event_t last_event;
static unsigned int   event_count;

static int fake_send(const uint8_t *data, uint16_t length)
{
    if (0 == transport_budget)
    {
        return -1;
    }

    if ((tx_count >= TX_LOG_MAX) || (length > TX_FRAME_MAX))
    {
        return -1;
    }

    (void)memcpy(tx_log[tx_count], data, length);
    tx_log_len[tx_count] = length;
    tx_count++;

    if (transport_budget > 0)
    {
        transport_budget--;
    }

    return 0;
}

static void fake_on_event(iec104_event_t evt)
{
    last_event = evt;
    event_count++;
}

static void tx_clear(void)
{
    (void)memset(tx_log, 0, sizeof(tx_log));
    (void)memset(tx_log_len, 0, sizeof(tx_log_len));
    tx_count = 0U;
}

/* ---------------------------------------------------------------- */
/* Frame accessors                                                   */
/* ---------------------------------------------------------------- */

static bool frame_is_u(const uint8_t *frame)
{
    return (0x03U == (frame[2] & 0x03U));
}

static bool frame_is_i(const uint8_t *frame)
{
    return (0U == (frame[2] & 0x01U));
}

static uint16_t frame_ns(const uint8_t *frame)
{
    return (uint16_t)((((uint16_t)frame[3] << 7) | ((uint16_t)frame[2] >> 1)) & 0x7FFFU);
}

static uint8_t frame_u_func(const uint8_t *frame)
{
    return (uint8_t)(frame[2] >> 2);
}

static uint8_t frame_asdu_type(const uint8_t *frame)
{
    return frame[6];
}

static uint8_t frame_asdu_cot(const uint8_t *frame)
{
    return (uint8_t)(frame[8] & 0x3FU);
}

static uint8_t frame_asdu_pn(const uint8_t *frame)
{
    return (uint8_t)((frame[8] >> 6) & 0x01U);
}

static uint16_t count_i_frames(void)
{
    uint16_t count = 0U;

    for (uint16_t idx = 0U; idx < tx_count; idx++)
    {
        if (frame_is_i(tx_log[idx]))
        {
            count++;
        }
    }

    return count;
}

/* Index of the first emitted frame carrying this ASDU type, or -1. */
static int find_asdu(uint8_t type_id, uint8_t cot)
{
    for (uint16_t idx = 0U; idx < tx_count; idx++)
    {
        if (frame_is_i(tx_log[idx]) &&
            (frame_asdu_type(tx_log[idx]) == type_id) &&
            (frame_asdu_cot(tx_log[idx]) == cot))
        {
            return (int)idx;
        }
    }

    return -1;
}

/* ---------------------------------------------------------------- */
/* SCADA-side frame builders                                         */
/* ---------------------------------------------------------------- */

#define COT_ACTIVATION_REQ  6U
#define QOI_STATION_REQ     20U

static void feed_u_frame(uint8_t function_code)
{
    const uint8_t frame[6] = {
        0x68U, 0x04U, (uint8_t)((function_code << 2) | 0x03U), 0x00U, 0x00U, 0x00U
    };

    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
}

static void feed_s_frame(uint16_t receive_seq)
{
    const uint8_t frame[6] = {
        0x68U, 0x04U, 0x01U, 0x00U,
        (uint8_t)((receive_seq << 1) & 0xFEU),
        (uint8_t)((receive_seq >> 7) & 0xFFU)
    };

    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
}

/* C_IC_NA_1 station interrogation, 16 bytes on the wire. */
static void build_interrogation(uint8_t *frame, uint16_t send_seq, uint16_t receive_seq,
                                uint16_t common_address, uint8_t qoi)
{
    frame[0]  = 0x68U;
    frame[1]  = 0x0EU;
    frame[2]  = (uint8_t)((send_seq << 1) & 0xFEU);
    frame[3]  = (uint8_t)((send_seq >> 7) & 0xFFU);
    frame[4]  = (uint8_t)((receive_seq << 1) & 0xFEU);
    frame[5]  = (uint8_t)((receive_seq >> 7) & 0xFFU);
    frame[6]  = C_IC_NA_1;
    frame[7]  = 0x01U;                  /* VSQ: one object, SQ = 0 */
    frame[8]  = COT_ACTIVATION_REQ;
    frame[9]  = 0x00U;                  /* originator address */
    frame[10] = (uint8_t)(common_address & 0xFFU);
    frame[11] = (uint8_t)(common_address >> 8);
    frame[12] = 0x00U;                  /* IOA = 0 */
    frame[13] = 0x00U;
    frame[14] = 0x00U;
    frame[15] = qoi;
}

/* C_RP_NA_1 reset process command, 16 bytes on the wire. */
static void build_reset_process(uint8_t *frame, uint16_t send_seq, uint16_t receive_seq,
                                uint16_t common_address, uint32_t ioa, uint8_t qrp)
{
    frame[0]  = 0x68U;
    frame[1]  = 0x0EU;
    frame[2]  = (uint8_t)((send_seq << 1) & 0xFEU);
    frame[3]  = (uint8_t)((send_seq >> 7) & 0xFFU);
    frame[4]  = (uint8_t)((receive_seq << 1) & 0xFEU);
    frame[5]  = (uint8_t)((receive_seq >> 7) & 0xFFU);
    frame[6]  = C_RP_NA_1;
    frame[7]  = 0x01U;                  /* VSQ: one object, SQ = 0 */
    frame[8]  = COT_ACTIVATION_REQ;
    frame[9]  = 0x00U;                  /* originator address */
    frame[10] = (uint8_t)(common_address & 0xFFU);
    frame[11] = (uint8_t)(common_address >> 8);
    frame[12] = (uint8_t)(ioa & 0xFFU);
    frame[13] = (uint8_t)((ioa >> 8) & 0xFFU);
    frame[14] = (uint8_t)((ioa >> 16) & 0xFFU);
    frame[15] = qrp;
}

/* ---------------------------------------------------------------- */
/* Fixture                                                           */
/* ---------------------------------------------------------------- */

#define TEST_COMMON_ADDRESS   1U

/* The interrogation path reads live measurements through these; constant
 * answers keep the emitted objects deterministic. */
static int fake_get_measured(uint32_t power_line_index, uint8_t phase, float *value,
                             qds_t *quality, cp56time2a_t *timestamp)
{
    *value     = (float)((power_line_index * 10U) + phase);
    *quality   = (qds_t){0};
    *timestamp = (cp56time2a_t){0};

    return 0;
}

static int fake_get_state(uint32_t power_line_index, uint8_t phase, siq_t *value,
                          cp56time2a_t *timestamp)
{
    *value     = (siq_t){0};
    value->spi = (uint8_t)((power_line_index + phase) & 0x01U);
    *timestamp = (cp56time2a_t){0};

    return 0;
}

static const iec104_io_t test_io = {
    .send     = fake_send,
    .on_event = fake_on_event,

    .get_ariza_akimi           = fake_get_measured,
    .get_ariza_suresi          = fake_get_measured,
    .get_anlik_akim            = fake_get_measured,
    .get_ariza_kalicimi        = fake_get_state,
    .get_enerji_varyok         = fake_get_state,
    .get_nominal_akim_varyok   = fake_get_state,
    .get_rf_haberlesme_varyok  = fake_get_state,
};

static void setup(uint8_t k_max, uint8_t w_max)
{
    iec104_config_t cfg;

    mock_platform_reset();
    tx_clear();

    transport_budget = -1;
    last_event       = (iec104_event_t)0;
    event_count      = 0U;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.t0_max             = 30U;
    cfg.t1_max             = 15U;
    cfg.t2_max             = 10U;
    cfg.t3_max             = 20U;
    cfg.k_max              = k_max;
    cfg.w_max              = w_max;
    cfg.originator_address = 0U;
    cfg.common_address     = TEST_COMMON_ADDRESS;

    iec104_init(&test_io, &cfg);
}

/* Brings the link up, acknowledges the end-of-init frame so the send window
 * starts empty, and clears whatever STARTDT emitted. */
static void start_link(void)
{
    feed_u_frame(IEC104_STARTDT_ACT);
    feed_s_frame(iec104_get_send_sn());
    tx_clear();
}

static void enable_all_lines(void)
{
    for (uint8_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
    {
        mock_breaker_set_line_in_use(feeder, true);
    }
}

/* ---------------------------------------------------------------- */
/* Tests                                                             */
/* ---------------------------------------------------------------- */

static void test_startdt_brings_link_up(void)
{
    setup(12U, 8U);

    TEST_CHECK(!iec104_is_link_active(), "link is down before STARTDT");

    feed_u_frame(IEC104_STARTDT_ACT);

    TEST_CHECK(iec104_is_link_active(), "STARTDT_ACT activates the link");
    TEST_CHECK(tx_count >= 2U, "STARTDT emits a confirmation and end-of-init");

    TEST_CHECK(frame_is_u(tx_log[0]) && (IEC104_STARTDT_CON == frame_u_func(tx_log[0])),
               "first emitted frame is STARTDT_CON");

    TEST_CHECK(frame_is_i(tx_log[1]) && (M_EI_NA_1 == frame_asdu_type(tx_log[1])),
               "end-of-init follows as an I-frame");

    TEST_CHECK(0U == frame_ns(tx_log[1]), "end-of-init carries N(S) = 0");
}

static void test_send_sequence_increments_by_one(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(count_i_frames() >= 2U, "interrogation produces several I-frames");

    bool contiguous = true;
    uint16_t expected = 0U;
    bool first = true;

    for (uint16_t idx = 0U; idx < tx_count; idx++)
    {
        if (!frame_is_i(tx_log[idx]))
        {
            continue;
        }

        if (first)
        {
            expected = frame_ns(tx_log[idx]);
            first = false;
        }

        if (frame_ns(tx_log[idx]) != expected)
        {
            contiguous = false;
            break;
        }

        expected = (uint16_t)((expected + 1U) & 0x7FFFU);
    }

    TEST_CHECK(contiguous, "N(S) advances by exactly one per I-frame");
    TEST_CHECK(iec104_get_send_sn() == expected, "send_sn matches the last emitted N(S) + 1");
}

static void test_rejected_frame_does_not_advance_counters(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    const uint16_t sn_before = iec104_get_send_sn();
    const uint16_t k_before  = iec104_get_k();

    transport_budget = 0;   /* transport refuses everything */

    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(0U == tx_count, "nothing reaches the transport while it refuses");
    TEST_CHECK(iec104_get_send_sn() == sn_before, "send_sn unchanged on transport rejection");
    TEST_CHECK(iec104_get_k() == k_before, "k_counter unchanged on transport rejection");
}

static void test_k_window_stops_and_reopens(void)
{
    uint8_t frame[16];
    const uint8_t k_max = 4U;

    setup(k_max, 2U);
    start_link();
    enable_all_lines();

    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(count_i_frames() <= k_max, "no more than k_max I-frames leave unacknowledged");
    TEST_CHECK(iec104_get_k() == k_max, "k_counter saturates at k_max");

    const uint16_t emitted = count_i_frames();

    /* SCADA acknowledges everything sent so far. */
    feed_s_frame(iec104_get_send_sn());

    TEST_CHECK(0U == iec104_get_k(), "acknowledgement empties the window");

    tx_clear();
    build_interrogation(frame, 1U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(count_i_frames() > 0U, "window reopens after the acknowledgement");
    TEST_CHECK(emitted > 0U, "the first burst was not empty");
}

static void test_stopdt_closes_the_i_frame_gate(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    feed_u_frame(IEC104_STOPDT_ACT);

    TEST_CHECK(!iec104_is_link_active(), "STOPDT_ACT deactivates the link");
    TEST_CHECK((tx_count >= 1U) && frame_is_u(tx_log[0]) &&
               (IEC104_STOPDT_CON == frame_u_func(tx_log[0])),
               "STOPDT_ACT is confirmed");

    tx_clear();

    /* An interrogation arriving after STOPDT must not produce user data. */
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(0U == count_i_frames(), "no I-frame leaves after STOPDT");

    /* The emitters must refuse too, not just the interrogation path. */
    mock_breaker_set_line_in_use(0U, true);
    mock_fault_log_fill(0U, 0U, FAULT_LOG_TYPE_TEMPORARY, 3U);

    iec104_fault_emit_state_t state = {0};
    (void)iec104_emit_feeder_temporary_faults(0U, PHASE_L1, COT_INTERROGATED_GROUP3, &state);

    TEST_CHECK(0U == count_i_frames(), "fault emitter stays silent after STOPDT");
}

static void test_testfr_is_retried_when_the_transport_refuses(void)
{
    setup(64U, 32U);
    start_link();

    transport_budget = 0;

    /* Idle long enough for t3 to expire. */
    for (uint16_t tick = 0U; tick <= 25U; tick++)
    {
        iec104_tick();
    }
    libiec104_poll();

    TEST_CHECK(0U == tx_count, "the refused TESTFR never reached the transport");

    transport_budget = -1;

    for (uint16_t tick = 0U; tick <= 25U; tick++)
    {
        iec104_tick();
    }
    libiec104_poll();

    TEST_CHECK((tx_count >= 1U) && frame_is_u(tx_log[0]) &&
               (IEC104_TESTFR_ACT == frame_u_func(tx_log[0])),
               "TESTFR is retried after the transport recovers");
}

static void test_fault_emission_resumes_without_gap_or_repeat(void)
{
    const uint8_t fault_count = 9U;

    setup(64U, 32U);
    start_link();

    mock_breaker_set_line_in_use(0U, true);
    mock_fault_log_fill(0U, PHASE_L1, FAULT_LOG_TYPE_TEMPORARY, fault_count);

    iec104_fault_emit_state_t state = {0};

    /* Let only two frames through, then stall the transport. */
    transport_budget = 2;

    bool done = iec104_emit_feeder_temporary_faults(0U, PHASE_L1,
                                                   COT_INTERROGATED_GROUP3, &state);

    TEST_CHECK(!done, "emission reports that it is unfinished");
    TEST_CHECK(2U == tx_count, "the transport accepted exactly its budget");

    const uint16_t ns_after_stall = iec104_get_send_sn();

    /* A stalled call must not have advanced anything on the dropped frame. */
    done = iec104_emit_feeder_temporary_faults(0U, PHASE_L1,
                                               COT_INTERROGATED_GROUP3, &state);

    TEST_CHECK(!done, "still unfinished while the transport is stalled");
    TEST_CHECK(iec104_get_send_sn() == ns_after_stall, "a stalled retry burns no sequence number");

    transport_budget = -1;

    unsigned int guard = 0U;
    while (!done && (guard < 32U))
    {
        done = iec104_emit_feeder_temporary_faults(0U, PHASE_L1,
                                                   COT_INTERROGATED_GROUP3, &state);
        guard++;
    }

    TEST_CHECK(done, "emission completes once the transport recovers");

    /* Every emitted I-frame must carry a contiguous N(S). */
    bool contiguous = true;
    uint16_t expected = frame_ns(tx_log[0]);

    for (uint16_t idx = 0U; idx < tx_count; idx++)
    {
        if (!frame_is_i(tx_log[idx]))
        {
            continue;
        }

        if (frame_ns(tx_log[idx]) != expected)
        {
            contiguous = false;
            break;
        }

        expected = (uint16_t)((expected + 1U) & 0x7FFFU);
    }

    TEST_CHECK(contiguous, "resumed emission keeps N(S) contiguous");

    /* Four fields x 9 faults, batched per frame - the object total must match. */
    uint16_t object_total = 0U;

    for (uint16_t idx = 0U; idx < tx_count; idx++)
    {
        if (frame_is_i(tx_log[idx]))
        {
            object_total += (uint16_t)(tx_log[idx][7] & 0x7FU);
        }
    }

    TEST_CHECK((uint16_t)(4U * fault_count) == object_total,
               "every fault object is emitted exactly once");
}

static void test_split_apdu_is_reassembled(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);

    /* Deliver the frame in two TCP-sized pieces. */
    iec104_data_received(frame, 7U);
    libiec104_poll();

    TEST_CHECK(0U == count_i_frames(), "a partial APDU produces no response");

    iec104_data_received(&frame[7], (uint16_t)(sizeof(frame) - 7U));
    libiec104_poll();

    TEST_CHECK(count_i_frames() > 0U, "the reassembled APDU is processed");
    TEST_CHECK(find_asdu(C_IC_NA_1, COT_ACTIVATION_CON) >= 0,
               "reassembled interrogation is confirmed");
}

static void test_interrogation_is_confirmed_before_its_data(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    enable_all_lines();

    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int con_idx  = find_asdu(C_IC_NA_1, COT_ACTIVATION_CON);
    const int term_idx = find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM);

    TEST_CHECK(con_idx >= 0, "interrogation is confirmed");
    TEST_CHECK(term_idx >= 0, "interrogation is terminated");
    TEST_CHECK((con_idx >= 0) && (term_idx > con_idx),
               "ACT_CON precedes ACT_TERM");

    /* Any data carried for this interrogation must sit between the two. */
    bool data_between = true;

    for (int idx = 0; idx < (int)tx_count; idx++)
    {
        if (!frame_is_i(tx_log[idx]))
        {
            continue;
        }

        if (COT_INTERROGATED_STATION == frame_asdu_cot(tx_log[idx]))
        {
            if ((idx < con_idx) || ((term_idx >= 0) && (idx > term_idx)))
            {
                data_between = false;
                break;
            }
        }
    }

    TEST_CHECK(data_between, "interrogated data stays between ACT_CON and ACT_TERM");
}

static void test_common_address_mismatch_is_rejected(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    build_interrogation(frame, 0U, 1U, (uint16_t)(TEST_COMMON_ADDRESS + 1U), QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(find_asdu(C_IC_NA_1, COT_ACTIVATION_CON) < 0,
               "an interrogation for another station is not confirmed");
    TEST_CHECK(find_asdu(C_IC_NA_1, UkComAdrASDU) >= 0,
               "unknown common address is answered negatively");
}

static void test_i_frame_before_startdt_is_dropped(void)
{
    uint8_t frame[16];

    setup(64U, 32U);

    build_interrogation(frame, 0U, 0U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(0U == tx_count, "I-frames before STARTDT are ignored");
}

static void test_reset_process_general_reset_is_confirmed(void)
{
    uint8_t  frame[16];
    uint16_t own_ns;

    setup(64U, 32U);
    start_link();

    own_ns = iec104_get_send_sn();

    build_reset_process(frame, 0U, own_ns, TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int con = find_asdu(C_RP_NA_1, COT_ACTIVATION_CON);

    TEST_CHECK(con >= 0, "reset process is confirmed with COT 7");
    TEST_CHECK((con >= 0) && (0U == frame_asdu_pn(tx_log[con])),
               "a supported reset qualifier is confirmed positively");
    TEST_CHECK((con >= 0) && (own_ns == frame_ns(tx_log[con])),
               "the confirmation carries our own N(S), not the master's");
    TEST_CHECK(IEC104_EVT_REBOOT_REQUESTED == last_event,
               "a general reset asks the application to reboot");
}

static void test_reset_process_with_unknown_qrp_is_rejected(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    build_reset_process(frame, 0U, iec104_get_send_sn(), TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_NOT_USED);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int con = find_asdu(C_RP_NA_1, COT_ACTIVATION_CON);

    TEST_CHECK((con >= 0) && (1U == frame_asdu_pn(tx_log[con])),
               "an undefined reset qualifier is answered negatively");
    TEST_CHECK(IEC104_EVT_REBOOT_REQUESTED != last_event,
               "an undefined reset qualifier triggers no reboot");
}

static void test_reset_process_with_foreign_ioa_is_rejected(void)
{
    uint8_t  frame[16];
    uint16_t own_ns;

    setup(64U, 32U);
    start_link();

    own_ns = iec104_get_send_sn();

    build_reset_process(frame, 0U, own_ns, TEST_COMMON_ADDRESS, 12345U,
                        IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int con = find_asdu(C_RP_NA_1, COT_ACTIVATION_CON);

    TEST_CHECK((con >= 0) && (1U == frame_asdu_pn(tx_log[con])),
               "a reset for a foreign IOA is answered negatively");
    TEST_CHECK((con >= 0) && (own_ns == frame_ns(tx_log[con])),
               "the negative confirmation also carries our own N(S)");
    TEST_CHECK(IEC104_EVT_REBOOT_REQUESTED != last_event,
               "a reset for a foreign IOA triggers no reboot");
}

/* ---------------------------------------------------------------- */

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    printf("\r\n=== libiec104 protocol tests ===\r\n\r\n");

    test_startdt_brings_link_up();
    test_send_sequence_increments_by_one();
    test_rejected_frame_does_not_advance_counters();
    test_k_window_stops_and_reopens();
    test_stopdt_closes_the_i_frame_gate();
    test_testfr_is_retried_when_the_transport_refuses();
    test_fault_emission_resumes_without_gap_or_repeat();
    test_split_apdu_is_reassembled();
    test_interrogation_is_confirmed_before_its_data();
    test_common_address_mismatch_is_rejected();
    test_i_frame_before_startdt_is_dropped();
    test_reset_process_general_reset_is_confirmed();
    test_reset_process_with_unknown_qrp_is_rejected();
    test_reset_process_with_foreign_ioa_is_rejected();

    printf("\r\n--------------------------------\r\n");
    printf("passed: %u   failed: %u\r\n", test_pass, test_fail);

    return (0U == test_fail) ? 0 : 1;
}

/*** end of file ***/
