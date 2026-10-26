/*
 * test_iec104_protocol_scenario.c
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
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
 * Usage: ceedling test:test_iec104_protocol_scenario
 */

#include "unity.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "iec104.h"
#include "iec104_config.h"
#include "iec104_util.h"
#include "iec104_types.h"
#include "iec104_platform_fake.h"

#define TEST_CHECK(condition, message) \
    TEST_ASSERT_TRUE_MESSAGE((condition), (message))

TEST_SOURCE_FILE("iec104_platform_fake.c")
TEST_SOURCE_FILE("iec104_config.c")
TEST_SOURCE_FILE("iec104_util.c")
TEST_SOURCE_FILE("cp56time2a.c")

void setUp(void)
{
}

void tearDown(void)
{
}

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

/* When >= 0, the frame that would land at this tx_log index is refused once. */
static int transport_reject_index;

static iec104_event_t last_event;
static unsigned int   event_count;

static int fake_send(const uint8_t *data, uint16_t length)
{
    if (0 == transport_budget)
    {
        return -1;
    }

    if ((int)tx_count == transport_reject_index)
    {
        transport_reject_index = -1;
        return -1;
    }

    if ((tx_count >= TX_LOG_MAX) || (length > TX_FRAME_MAX))
    {
        return -1;
    }

    TEST_ASSERT_TRUE(length >= 6U);
    TEST_ASSERT_EQUAL_HEX8(0x68U, data[0]);
    TEST_ASSERT_EQUAL_UINT16(length, (uint16_t)((uint16_t)data[1] + 2U));

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
    return (uint16_t)((((uint32_t)frame[3] << 7U) |
                       ((uint32_t)frame[2] >> 1U)) & 0x7FFFU);
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

static uint8_t frame_asdu_oa(const uint8_t *frame)
{
    return frame[9];
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
static bool test_invalid_quality;
static cp56time2a_t test_sample_time;

static int fake_get_measured(uint32_t power_line_index, uint8_t phase, float *value,
                             qds_t *quality, cp56time2a_t *timestamp)
{
    *value     = (float)((power_line_index * 10U) + phase);
    *quality   = (qds_t){0};
    quality->invalid = test_invalid_quality && (2U == phase);
    *timestamp = test_sample_time;

    return 0;
}

static int fake_get_state(uint32_t power_line_index, uint8_t phase, siq_t *value,
                          cp56time2a_t *timestamp)
{
    *value     = (siq_t){0};
    value->spi = (uint8_t)((power_line_index + phase) & 0x01U);
    value->invalid = test_invalid_quality && (2U == phase);
    *timestamp = test_sample_time;

    return 0;
}

static const iec104_io_t test_io = {
    .send     = fake_send,
    .on_event = fake_on_event,

    .get_anlik_akim            = fake_get_measured,
    .get_enerji_varyok         = fake_get_state,
    .get_yuk_akimi_varyok   = fake_get_state,
    .get_rf_haberlesme_varyok  = fake_get_state,
};

static void setup_mode(uint8_t k_max, uint8_t w_max, bool alarms)
{
    iec104_config_t cfg;

    mock_platform_reset();
    tx_clear();
    test_invalid_quality = false;
    test_sample_time = (cp56time2a_t){0};

    transport_budget = -1;
    transport_reject_index = -1;
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

    iec104_io_t io = test_io;
    io.get_trip_failed = alarms ? fake_get_state : NULL;
    iec104_init(&io, &cfg);
}

static void setup(uint8_t k_max, uint8_t w_max)
{
    setup_mode(k_max, w_max, false);
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

static void setup_periodic(uint8_t window, uint32_t interval)
{
    setup(window, 1U);
    const iec104_config_t settings = {.common_address = TEST_COMMON_ADDRESS,
        .originator_address = 7U, .k_max = window, .w_max = 1U,
        .t1_max = 45U, .t2_max = 10U, .t3_max = 60U,
        .periodical_send_interval = interval};
    TEST_ASSERT_TRUE(iec104_apply_config(&settings));
}

static void configure_periodic_line(uint32_t index)
{
    iec104_line_config_t line = {.in_use = 1U};
    for (uint8_t phase = 0U; 3U > phase; phase++)
    {
        const uint32_t offset = index * 10U + phase;
        line.anlik_akim[phase] = iec104_make_ioa_3byte(1000U + offset);
        line.enerji_varyok[phase] = iec104_make_ioa_3byte(2000U + offset);
        line.yuk_akimi_varyok[phase] = iec104_make_ioa_3byte(3000U + offset);
        line.rf_haberlesme_varyok[phase] = iec104_make_ioa_3byte(4000U + offset);
    }
    TEST_ASSERT_TRUE(iec104_set_line_config(index, &line));
}

void test_periodic_disabled_and_before_deadline_emit_nothing(void)
{
    setup_periodic(64U, 0U);
    mock_breaker_set_line_in_use(0U, true);
    start_link();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    iec104_reset();
    setup_periodic(64U, 2U);
    mock_breaker_set_line_in_use(0U, true);
    start_link();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_missing_config_defaults_to_disabled_periodic_refresh(void)
{
    setup(64U, 32U);
    iec104_init(&test_io, NULL);
    configure_periodic_line(0U);
    start_link();
    for (uint8_t second = 0U; 61U > second; second++)
    {
        iec104_tick();
    }
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_periodic_publishes_only_active_lines_with_original_quality_time(void)
{
    setup_periodic(64U, 1U);
    configure_periodic_line(2U);
    test_invalid_quality = true;
    test_sample_time = cp56time2a_make(12345U, 12U, 13U, 1U, 19U, 10U, 26U);
    start_link();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(4U, tx_count);
    for (size_t index = 0U; tx_count > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(COT_SPONTANEOUS, frame_asdu_cot(tx_log[index]));
        TEST_ASSERT_EQUAL_UINT8(7U, tx_log[index][9]);
        TEST_ASSERT_EQUAL_UINT8(3U, tx_log[index][7] & 0x7FU);
        if (0U == index)
        {
            TEST_ASSERT_EQUAL_UINT8(M_ME_TF_1, tx_log[index][6]);
            const m_me_tf_1_t *objects =
                (const m_me_tf_1_t *)&tx_log[index][DATA_START_IDX];
            TEST_ASSERT_FLOAT_WITHIN(0.001F, 20.0F, objects[0].value);
            TEST_ASSERT_EQUAL_UINT32(1020U,
                iec104_ioa_3byte_to_uint32(objects[0].ioa));
            TEST_ASSERT_TRUE(objects[2].quality.invalid);
            TEST_ASSERT_EQUAL_MEMORY(&test_sample_time,
                &objects[0].timestamp, sizeof(test_sample_time));
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[index][6]);
            const m_sp_tb_1_t *objects =
                (const m_sp_tb_1_t *)&tx_log[index][DATA_START_IDX];
            TEST_ASSERT_TRUE(objects[2].siq.invalid);
            TEST_ASSERT_EQUAL_UINT32((index + 1U) * 1000U + 20U,
                iec104_ioa_3byte_to_uint32(objects[0].ioa));
            TEST_ASSERT_EQUAL_MEMORY(&test_sample_time,
                &objects[0].timestamp, sizeof(test_sample_time));
        }
    }
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(4U, tx_count);
    feed_s_frame(iec104_get_send_sn());
    tx_clear();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(4U, tx_count);
    const m_me_tf_1_t *repeated =
        (const m_me_tf_1_t *)&tx_log[0][DATA_START_IDX];
    TEST_ASSERT_EQUAL_MEMORY(&test_sample_time, &repeated[0].timestamp,
                             sizeof(test_sample_time));
}

void test_periodic_window_one_resumes_every_line_and_family_without_duplicates(void)
{
    setup_periodic(1U, 1U);
    for (uint32_t line = 0U; MAX_POWER_LINE_COUNT > line; line++)
    {
        configure_periodic_line(line);
    }
    start_link();
    iec104_tick();
    libiec104_poll();
    for (uint16_t index = 1U; 4U * MAX_POWER_LINE_COUNT > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT16(index, tx_count);
        feed_s_frame(iec104_get_send_sn());
    }
    TEST_ASSERT_EQUAL_UINT16(4U * MAX_POWER_LINE_COUNT, tx_count);
    for (size_t index = 0U; tx_count > index; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(COT_SPONTANEOUS, frame_asdu_cot(tx_log[index]));
        TEST_ASSERT_EQUAL_UINT8((MAX_POWER_LINE_COUNT > index) ?
            M_ME_TF_1 : M_SP_TB_1, tx_log[index][6]);
        const uint32_t base = (uint32_t)(index / MAX_POWER_LINE_COUNT + 1U);
        const uint32_t line = (uint32_t)(index % MAX_POWER_LINE_COUNT);
        const uint32_t ioa = (uint32_t)tx_log[index][DATA_START_IDX] |
            ((uint32_t)tx_log[index][DATA_START_IDX + 1U] << 8U) |
            ((uint32_t)tx_log[index][DATA_START_IDX + 2U] << 16U);
        TEST_ASSERT_EQUAL_UINT32(base * 1000U + line * 10U, ioa);
    }
}

void test_periodic_transport_rejection_keeps_cursor_and_reset_cancels_cycle(void)
{
    setup_periodic(64U, 2U);
    mock_breaker_set_line_in_use(0U, true);
    start_link();
    transport_budget = 0;
    iec104_tick();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    transport_budget = 1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_ME_TF_1, tx_log[0][6]);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(4U, tx_count);
    iec104_reset();
    start_link();
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_startdt_brings_link_up(void)
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

void test_send_sequence_increments_by_one(void)
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

void test_rejected_frame_does_not_advance_counters(void)
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

void test_k_window_stops_and_reopens(void)
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

    TEST_CHECK(iec104_get_k() <= k_max, "resumed data respects the window");
    TEST_CHECK(count_i_frames() > emitted,
               "acknowledgement allows pending interrogation data to resume");
    TEST_CHECK(emitted > 0U, "the first burst was not empty");
}

void test_stopdt_closes_the_i_frame_gate(void)
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

void test_testfr_is_retried_when_the_transport_refuses(void)
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

void test_testfr_con_timeout_closes_the_link(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();

    /* Idle long enough for t3 to expire and a TESTFR to go out. */
    for (uint16_t tick = 0U; tick <= 25U; tick++)
    {
        iec104_tick();
    }
    libiec104_poll();

    TEST_CHECK((tx_count >= 1U) && frame_is_u(tx_log[0]) &&
               (IEC104_TESTFR_ACT == frame_u_func(tx_log[0])),
               "an idle link is probed with TESTFR");

    /* Unrelated traffic must not count as a TESTFR confirmation. */
    for (uint16_t tick = 0U; tick <= 16U; tick++)
    {
        iec104_tick();

        build_interrogation(frame, iec104_get_receive_sn(), iec104_get_send_sn(),
                            TEST_COMMON_ADDRESS, QOI_STATION_REQ);
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
    }

    TEST_CHECK(IEC104_EVT_REQUEST_SOCKET_CLOSE == last_event,
               "a missing TESTFR_CON closes the link even while data flows");
}

void test_full_temporary_and_permanent_history_preserves_every_field_on_wire(void)
{
    for (uint8_t kind = 0U; 2U > kind; kind++)
    {
        setup(64U, 32U);
        iec104_line_config_t mapped = {.in_use = 1U};
        mapped.temporary_fault = iec104_make_ioa_3byte(100000U);
        mapped.permanent_fault = iec104_make_ioa_3byte(200000U);
        TEST_ASSERT_TRUE(iec104_set_line_config(0U, &mapped));
        const fault_log_type_t type = (0U == kind) ?
            FAULT_LOG_TYPE_TEMPORARY : FAULT_LOG_TYPE_PERMANENT;
        for (uint8_t phase = 0U; PHASE_MAX > phase; phase++)
        {
            mock_fault_log_fill(0U, phase, type, 15U);
        }
        start_link();
        iec104_fault_emit_state_t state = {0};
        if (0U == kind)
        {
            TEST_ASSERT_TRUE(iec104_emit_feeder_temporary_faults(0U,
                PHASE_ALL, COT_INTERROGATED_GROUP3, &state));
        }
        else
        {
            TEST_ASSERT_TRUE(iec104_emit_feeder_permanent_faults(0U,
                PHASE_ALL, COT_INTERROGATED_GROUP4, &state));
        }
        bool seen[180U] = {false};
        const uint32_t base = (0U == kind) ? 100000U : 200000U;
        for (size_t packet_index = 0U; tx_count > packet_index; packet_index++)
        {
            const uint8_t *packet_data = tx_log[packet_index];
            TEST_ASSERT_EQUAL_UINT8((0U == kind) ? 23U : 24U,
                frame_asdu_cot(packet_data));
            const bool measured = (M_ME_TF_1 == packet_data[6]);
            TEST_ASSERT_TRUE(measured || (M_SP_TB_1 == packet_data[6]));
            const size_t object_size = measured ? sizeof(m_me_tf_1_t) :
                                                 sizeof(m_sp_tb_1_t);
            for (size_t index = 0U; (packet_data[7] & 0x7FU) > index; index++)
            {
                const uint8_t *data = &packet_data[DATA_START_IDX +
                                                   index * object_size];
                const uint32_t ioa = (uint32_t)data[0] |
                    ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
                TEST_ASSERT_TRUE((base <= ioa) && (base + 180U > ioa));
                const uint32_t offset = ioa - base;
                TEST_ASSERT_FALSE(seen[offset]);
                seen[offset] = true;
                const uint32_t record = (offset % 60U) / 4U;
                const uint32_t field = offset % 4U;
                TEST_ASSERT_EQUAL_UINT8(2U > field, measured);
                if (measured)
                {
                    m_me_tf_1_t object;
                    (void)memcpy(&object, data, sizeof(object));
                    const float expected = (0U == field) ?
                        (float)record / 10.0F : (float)(record + 1U);
                    TEST_ASSERT_FLOAT_WITHIN(0.001F, expected, object.value);
                }
                else
                {
                    TEST_ASSERT_EQUAL_UINT8((2U == field) ?
                        (record + 1U) % 2U : record % 2U, data[3] & 1U);
                }
            }
        }
        for (size_t index = 0U; sizeof(seen) / sizeof(seen[0]) > index; index++)
        {
            TEST_ASSERT_TRUE(seen[index]);
        }
    }
}

void test_fault_emission_resumes_without_gap_or_repeat(void)
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

void test_split_apdu_is_reassembled(void)
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

void test_interrogation_is_confirmed_before_its_data(void)
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

void test_common_address_mismatch_is_rejected(void)
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

void test_i_frame_before_startdt_is_dropped(void)
{
    uint8_t frame[16];

    setup(64U, 32U);

    build_interrogation(frame, 0U, 0U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_CHECK(0U == tx_count, "I-frames before STARTDT cannot execute");
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_interrogation_transport_rejection_resumes_without_losing_data(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    enable_all_lines();

    /* Accept ACT_CON and one data APDU, then hold the transport full. */
    transport_budget = 2;

    build_interrogation(frame, 0U, iec104_get_send_sn(), TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    TEST_ASSERT_EQUAL_INT(-1, find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM));
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    const int term = find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM);

    TEST_CHECK(term >= 0, "the resumed interrogation terminates");
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[term]));
    size_t objects = 0U;
    for (size_t index = 0U; index < tx_count; index++)
    {
        if (frame_is_i(tx_log[index]) &&
            (COT_INTERROGATED_STATION == frame_asdu_cot(tx_log[index])))
        {
            objects += tx_log[index][7] & 0x7FU;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(MAX_POWER_LINE_COUNT * 12U, objects);
}

void test_station_interrogation_sends_untimed_values_and_quality(void)
{
    setup_mode(64U, 32U, true);
    test_invalid_quality = true;
    start_link();
    for (uint8_t feeder = 0U; MAX_POWER_LINE_COUNT > feeder; feeder++)
    {
        iec104_line_config_t line = {.in_use = 1U};
        for (size_t phase = 0U; 3U > phase; phase++)
        {
            const uint32_t base = 1000U + (uint32_t)feeder * 100U +
                                  (uint32_t)phase;
            line.anlik_akim[phase] = iec104_make_ioa_3byte(base + 30U);
            line.enerji_varyok[phase] = iec104_make_ioa_3byte(base + 40U);
            line.yuk_akimi_varyok[phase] = iec104_make_ioa_3byte(base + 50U);
            line.rf_haberlesme_varyok[phase] =
                iec104_make_ioa_3byte(base + 60U);
            line.trip_failed[phase] = iec104_make_ioa_3byte(base + 70U);
        }
        TEST_ASSERT_TRUE(iec104_set_line_config(feeder, &line));
    }
    uint8_t frame[16];
    build_interrogation(frame, 0U, iec104_get_send_sn(),
                        TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    size_t counts[5] = {0U};
    for (size_t index = 0U; tx_count > index; index++)
    {
        if (!frame_is_i(tx_log[index]) ||
            (COT_INTERROGATED_STATION != frame_asdu_cot(tx_log[index])))
        {
            continue;
        }
        const uint8_t type = frame_asdu_type(tx_log[index]);
        TEST_ASSERT_TRUE((M_ME_NC_1 == type) || (M_SP_NA_1 == type));
        const size_t object_size = (M_ME_NC_1 == type) ? 8U : 4U;
        const size_t count = tx_log[index][7] & 0x7FU;
        TEST_ASSERT_EQUAL_UINT8(0U, tx_log[index][7] & 0x80U);
        TEST_ASSERT_EQUAL_UINT32(12U + count * object_size, tx_log_len[index]);
        TEST_ASSERT_EQUAL_UINT16(tx_log_len[index] - 2U, tx_log[index][1]);
        for (size_t object = 0U; count > object; object++)
        {
            const uint8_t *data = &tx_log[index][12U + object * object_size];
            const uint32_t ioa = (uint32_t)data[0] |
                ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
            const uint32_t feeder = (ioa - 1000U) / 100U;
            const uint32_t field = (ioa - 1000U) % 100U;
            const uint32_t phase = field % 10U;
            const size_t category = (size_t)(field / 10U - 3U);
            TEST_ASSERT_LESS_THAN_UINT32(MAX_POWER_LINE_COUNT, feeder);
            TEST_ASSERT_LESS_THAN_UINT32(3U, phase);
            TEST_ASSERT_LESS_THAN_UINT32(5U, category);
            counts[category]++;
            if (0U == category)
            {
                float value;
                TEST_ASSERT_EQUAL_UINT8(M_ME_NC_1, type);
                memcpy(&value, &data[3], sizeof(value));
                TEST_ASSERT_EQUAL_FLOAT((float)(feeder * 10U + phase), value);
                TEST_ASSERT_EQUAL_UINT8((2U == phase) ? 0x80U : 0U, data[7]);
            }
            else
            {
                TEST_ASSERT_EQUAL_UINT8(M_SP_NA_1, type);
                TEST_ASSERT_EQUAL_UINT8(((feeder + phase) & 1U) |
                    ((2U == phase) ? 0x80U : 0U), data[3]);
            }
        }
    }
    for (size_t category = 0U; 5U > category; category++)
    {
        TEST_ASSERT_EQUAL_UINT32(MAX_POWER_LINE_COUNT * 3U, counts[category]);
    }
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_TERM,
                          frame_asdu_cot(tx_log[tx_count - 1U]));
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[tx_count - 1U]));
}

void test_group_two_keeps_timestamps_after_station_format_change(void)
{
    setup(64U, 32U);
    start_link();
    enable_all_lines();
    uint8_t frame[16];
    build_interrogation(frame, 0U, iec104_get_send_sn(),
                        TEST_COMMON_ADDRESS, QOI_GROUP_2);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    size_t objects = 0U;
    for (size_t index = 0U; tx_count > index; index++)
    {
        if (!frame_is_i(tx_log[index]) ||
            (COT_INTERROGATED_GROUP2 != frame_asdu_cot(tx_log[index])))
        {
            continue;
        }
        const size_t count = tx_log[index][7] & 0x7FU;
        TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, frame_asdu_type(tx_log[index]));
        TEST_ASSERT_EQUAL_UINT32(12U + count * 11U, tx_log_len[index]);
        objects += count;
    }
    TEST_ASSERT_EQUAL_UINT32(MAX_POWER_LINE_COUNT * 9U, objects);
}

void test_station_interrogation_without_active_lines_has_only_confirmations(void)
{
    setup(64U, 32U);
    start_link();
    uint8_t frame[16];
    build_interrogation(frame, 0U, iec104_get_send_sn(),
                        TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(C_IC_NA_1, frame_asdu_type(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(C_IC_NA_1, frame_asdu_type(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_TERM, frame_asdu_cot(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[1]));
}

void test_complete_interrogation_terminates_positively(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    enable_all_lines();

    build_interrogation(frame, 0U, iec104_get_send_sn(), TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int term = find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM);

    TEST_CHECK((term >= 0) && (0U == frame_asdu_pn(tx_log[term])),
               "a complete interrogation terminates positively");
}

void test_interrogation_with_unsupported_cot_is_rejected(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    enable_all_lines();
    build_interrogation(frame, 0U, iec104_get_send_sn(), TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    frame[8] = COT_DEACTIVATION;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int nack = find_asdu(C_IC_NA_1, UkCauseTx);

    TEST_CHECK(nack >= 0, "an unsupported cause of transmission is answered with COT 45");
    TEST_CHECK((nack >= 0) && (1U == frame_asdu_pn(tx_log[nack])),
               "the unknown-cause answer is negative");
    TEST_CHECK(find_asdu(C_IC_NA_1, COT_ACTIVATION_CON) < 0,
               "a deactivation is not confirmed as an activation");
    TEST_CHECK(find_asdu(M_ME_TF_1, COT_INTERROGATED_STATION) < 0,
               "a deactivation emits no interrogated data");
}

void test_interrogation_response_echoes_originator_address(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    enable_all_lines();

    build_interrogation(frame, 0U, iec104_get_send_sn(), TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    frame[9] = 7U;                      /* originator address of the master */
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();

    const int con  = find_asdu(C_IC_NA_1, COT_ACTIVATION_CON);
    const int term = find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM);
    const int data = find_asdu(M_ME_NC_1, COT_INTERROGATED_STATION);

    TEST_CHECK((con >= 0) && (7U == frame_asdu_oa(tx_log[con])),
               "ACT_CON echoes the originator address of the command");
    TEST_CHECK((term >= 0) && (7U == frame_asdu_oa(tx_log[term])),
               "ACT_TERM echoes the originator address of the command");
    TEST_CHECK((data >= 0) && (7U == frame_asdu_oa(tx_log[data])),
               "interrogated data echoes the originator address of the command");
}

void test_reset_process_general_reset_is_confirmed(void)
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

void test_reset_process_with_unknown_qrp_is_rejected(void)
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

void test_reset_pending_events_is_rejected_without_reboot(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    const uint32_t events_before = event_count;
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, 2U);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(C_RP_NA_1, frame_asdu_type(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[12], &tx_log[0][12], 4U);
    TEST_ASSERT_EQUAL_UINT32(events_before, event_count);
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_reset_process_with_foreign_ioa_is_rejected(void)
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

void test_interrogation_reassembles_at_every_tcp_split_boundary(void)
{
    uint8_t frame[16];

    for (uint16_t split = 1U; split < (uint16_t)sizeof(frame); split++)
    {
        setup(64U, 32U);
        start_link();
        build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS,
                            QOI_STATION_REQ);

        iec104_data_received(frame, split);
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0U, tx_count,
                                         "partial APDU must not respond");

        iec104_data_received(&frame[split],
                            (uint16_t)(sizeof(frame) - split));
        libiec104_poll();
        TEST_ASSERT_TRUE(find_asdu(C_IC_NA_1, COT_ACTIVATION_CON) >= 0);
        TEST_ASSERT_TRUE(find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM) >= 0);
    }
}

void test_interrogation_reassembles_one_byte_at_a_time(void)
{
    uint8_t frame[16];

    setup(64U, 32U);
    start_link();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);

    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        iec104_data_received(&frame[index], 1U);
        libiec104_poll();
        if ((index + 1U) < sizeof(frame))
        {
            TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
        }
    }

    TEST_ASSERT_TRUE(find_asdu(C_IC_NA_1, COT_ACTIVATION_CON) >= 0);
    TEST_ASSERT_TRUE(find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM) >= 0);
}

void test_startdt_reassembles_one_byte_at_a_time(void)
{
    const uint8_t frame[6] = {0x68U, 0x04U, 0x07U, 0U, 0U, 0U};

    setup(64U, 32U);
    for (size_t index = 0U; index < sizeof(frame); index++)
    {
        iec104_data_received(&frame[index], 1U);
        libiec104_poll();
        if ((index + 1U) < sizeof(frame))
        {
            TEST_ASSERT_FALSE(iec104_is_link_active());
            TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
        }
    }

    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_TRUE(tx_count >= 2U);
    TEST_ASSERT_TRUE(frame_is_u(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(IEC104_STARTDT_CON, frame_u_func(tx_log[0]));
}

static uint16_t frame_nr(const uint8_t *frame)
{
    return (uint16_t)(((uint32_t)frame[5] << 7U) |
                      ((uint32_t)frame[4] >> 1U));
}

static void advance_ticks(uint16_t ticks)
{
    for (uint16_t index = 0U; index < ticks; index++)
    {
        iec104_tick();
    }
    libiec104_poll();
}

static void send_pending_reset(uint16_t ns, uint16_t nr)
{
    uint8_t frame[16];
    build_reset_process(frame, ns, nr, TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_RESET_PENDING_EVENTS);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
}

void test_duplicate_ack_keeps_outstanding_window_unchanged(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
    feed_s_frame(1U);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_acksn());
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_partial_ack_releases_only_confirmed_frames(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    send_pending_reset(1U, 1U);
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_get_k());
    feed_s_frame(2U);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(2U, iec104_get_acksn());
    feed_s_frame(3U);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(3U, iec104_get_acksn());
}

void test_old_ack_outside_sent_window_closes_connection(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    feed_s_frame(2U);
    send_pending_reset(1U, 2U);
    feed_s_frame(1U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_ack_for_unsent_frame_closes_and_resets_link(void)
{
    setup(64U, 32U);
    start_link();
    feed_s_frame(2U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
}

void test_out_of_order_i_frame_closes_without_executing_command(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_reset_process(frame, 1U, 1U, TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_invalid_piggyback_ack_prevents_reset_command_execution(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_reset_process(frame, 0U, 2U, TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_receive_sequence_wraps_from_32767_to_zero(void)
{
    setup(64U, 32U);
    start_link();
    iec_set_receive_sn(32766U);
    send_pending_reset(32766U, 1U);
    TEST_ASSERT_EQUAL_UINT16(32767U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(32767U, frame_nr(tx_log[0]));
    send_pending_reset(32767U, 1U);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, frame_nr(tx_log[1]));
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_t1_closes_at_oldest_frame_deadline(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    advance_ticks(14U);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    advance_ticks(1U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_partial_ack_preserves_remaining_frame_original_deadline(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    advance_ticks(5U);
    send_pending_reset(1U, 1U);
    advance_ticks(9U);
    feed_s_frame(2U);
    advance_ticks(5U);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
    advance_ticks(1U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
}

void test_testfr_confirmation_cancels_pending_timeout(void)
{
    setup(64U, 32U);
    start_link();
    advance_ticks(21U);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    advance_ticks(1U);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_ACT, frame_u_func(tx_log[0]));
    feed_u_frame(IEC104_TESTFR_CON);
    advance_ticks(16U);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
}

void test_testfr_act_before_startdt_confirms_without_opening_link(void)
{
    const uint8_t expected[6] = {0x68U, 4U, 0x83U, 0U, 0U, 0U};
    setup(64U, 32U);
    feed_u_frame(IEC104_TESTFR_ACT);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, tx_log[0], sizeof(expected));
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
}

void test_unknown_u_function_has_no_state_or_transport_effect(void)
{
    setup(64U, 32U);
    feed_u_frame(0U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT(0U, event_count);
}

void test_invalid_apdu_lengths_close_without_scanning_for_embedded_frame(void)
{
    const uint8_t lengths[] = {0U, 1U, 2U, 3U, 254U, 255U};
    for (size_t index = 0U; index < sizeof(lengths); index++)
    {
        uint8_t data[12] = {0x68U, 0U, 0U, 0U, 0U, 0U, 0x68U, 4U, 0x43U, 0U, 0U, 0U};
        setup(64U, 32U);
        data[1] = lengths[index];
        iec104_data_received(data, sizeof(data));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
        TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    }
}

void test_noise_prefix_closes_without_processing_following_frames(void)
{
    const uint8_t data[] = {0x00U, 0xFFU, 0x67U, 0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    iec104_data_received(data, sizeof(data));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_partial_tail_survives_after_complete_frame(void)
{
    const uint8_t first[9] = {0x68U, 4U, 0x43U, 0U, 0U, 0U,
                             0x68U, 4U, 0x43U};
    const uint8_t tail[3] = {0U, 0U, 0U};
    setup(64U, 32U);
    iec104_data_received(first, (uint16_t)sizeof(first));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    iec104_data_received(tail, (uint16_t)sizeof(tail));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_CON, frame_u_func(tx_log[1]));
}

void test_null_and_empty_input_preserve_pending_partial_frame(void)
{
    const uint8_t frame[6] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    iec104_data_received(frame, 3U);
    libiec104_poll();
    iec104_data_received(NULL, 3U);
    iec104_data_received(frame, 0U);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    iec104_data_received(&frame[3], 3U);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
}

void test_unknown_asdu_returns_negative_type_response_with_own_sequences(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    frame[6] = 0xFEU;
    frame[9] = 0x37U;
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(16U, tx_log_len[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFEU, frame_asdu_type(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(0x37U, frame_asdu_oa(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_ns(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_nr(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[10], &tx_log[0][10], 6U);
}

static void assert_control_command_returns_unsupported_type(uint8_t type_id)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0x1234U, 1U);
    frame[6] = type_id;
    frame[9] = 0x37U;

    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();

    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_TRUE(frame_is_i(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(sizeof(frame), tx_log_len[0]);
    TEST_ASSERT_EQUAL_UINT8(type_id, frame_asdu_type(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(0x37U, frame_asdu_oa(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_ns(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_nr(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[10], &tx_log[0][10], 6U);
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_single_command_returns_negative_unsupported_type_response(void)
{
    assert_control_command_returns_unsupported_type(C_SC_NA_1);
}

void test_double_command_returns_negative_unsupported_type_response(void)
{
    assert_control_command_returns_unsupported_type(C_DC_NA_1);
}

void test_setpoints_are_rejected_without_reboot_or_state_change(void)
{
    static const uint8_t types[] = {C_SE_NA_1, C_SE_NB_1, C_SE_NC_1};
    for (size_t index = 0U; sizeof(types) > index; index++)
    {
        uint8_t frame[20] = {0};
        setup(64U, 32U);
        start_link();
        build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0x1234U, 1U);
        const uint16_t length = (C_SE_NC_1 == types[index]) ? 20U : 18U;
        frame[1] = (uint8_t)(length - 2U);
        frame[6] = types[index];
        iec104_data_received(frame, length);
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT16(length, tx_log_len[0]);
        TEST_ASSERT_EQUAL_UINT8(types[index], frame_asdu_type(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
        TEST_ASSERT_TRUE(iec104_is_link_active());
    }
}

void test_truncated_reset_command_is_rejected_without_reboot(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U,
                        IEC104_QRP_GENERAL_RESET);
    frame[1] = 13U;
    iec104_data_received(frame, 15U);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

/* Receive-only data does not generate an I-frame response or piggyback ACK. */
static void send_initialization(uint16_t send_seq)
{
    uint8_t frame[16];
    build_reset_process(frame, send_seq, 1U, TEST_COMMON_ADDRESS, 0U, 0U);
    frame[6] = M_EI_NA_1;
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
}

void test_receive_window_sends_exact_s_ack_at_threshold(void)
{
    const uint8_t expected[6] = {0x68U, 4U, 1U, 0U, 4U, 0U};
    setup(64U, 2U);
    start_link();
    send_initialization(0U);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_w());
    send_initialization(1U);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, tx_log[0], sizeof(expected));
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_w());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_send_sn());
}

void test_t2_sends_ack_at_deadline_and_clears_pending_count(void)
{
    const uint8_t expected[6] = {0x68U, 4U, 1U, 0U, 2U, 0U};
    setup(64U, 32U);
    start_link();
    send_initialization(0U);
    advance_ticks(9U);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_w());
    advance_ticks(1U);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, tx_log[0], sizeof(expected));
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_w());
    advance_ticks(1U);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
}

void test_rejected_s_ack_keeps_pending_count_until_transport_recovers(void)
{
    setup(64U, 1U);
    start_link();
    transport_budget = 0;
    send_initialization(0U);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_w());
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(1U, frame_nr(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, tx_log[0][2]);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_w());
}

/* Wire fixture: 2026-10-01 23:57:58.987, Thursday, valid CP56Time2a. */
static void build_clock_command(uint8_t *frame)
{
    const uint8_t timestamp[7] = {0x6BU, 0xE6U, 57U, 23U,
                                 0x81U, 10U, 26U};
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, 0U);
    frame[1] = 20U;
    frame[6] = C_CS_NA_1;
    frame[9] = 0x37U;
    (void)memcpy(&frame[15], timestamp, sizeof(timestamp));
}

void test_clock_command_confirms_wire_fields_and_updates_all_rtc_fields(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(22U, tx_log_len[0]);
    TEST_ASSERT_EQUAL_UINT8(C_CS_NA_1, frame_asdu_type(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(0x37U, frame_asdu_oa(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_ns(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT16(1U, frame_nr(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[10], &tx_log[0][10], 12U);
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    const rtc_t actual = mock_rtc_last_sync();
    TEST_ASSERT_EQUAL_UINT16(987U, actual.millisec);
    TEST_ASSERT_EQUAL_UINT8(58U, actual.second);
    TEST_ASSERT_EQUAL_UINT8(57U, actual.minute);
    TEST_ASSERT_EQUAL_UINT8(23U, actual.hour);
    TEST_ASSERT_EQUAL_UINT8(1U, actual.day);
    TEST_ASSERT_EQUAL_UINT8(10U, actual.month);
    TEST_ASSERT_EQUAL_UINT8(26U, actual.year);
    const cp56time2a_t saved = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&frame[15], &saved, 7U);
}

void test_clock_command_invalid_bit_rejects_without_changing_rtc(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    frame[17] |= 0x80U;
    const cp56time2a_t previous = iec104_get_last_clock_sync_time();
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
    const cp56time2a_t actual = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&previous, &actual, sizeof(actual));
}

void test_clock_command_missing_timestamp_byte_is_rejected(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    frame[1] = 19U;
    iec104_data_received(frame, 21U);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
}

void test_clock_command_unsupported_cot_does_not_update_rtc(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    frame[8] = COT_DEACTIVATION;
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(UkCauseTx, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
}

void test_clock_command_reassembles_at_every_tcp_split_boundary(void)
{
    uint8_t frame[22];
    for (uint16_t split = 1U; split < sizeof(frame); split++)
    {
        setup(64U, 32U);
        start_link();
        build_clock_command(frame);
        iec104_data_received(frame, split);
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
        TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
        iec104_data_received(&frame[split],
                             (uint16_t)(sizeof(frame) - split));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    }
}

void test_send_and_ack_sequences_complete_full_15_bit_cycle(void)
{
    setup(64U, 32U);
    start_link();
    for (uint32_t index = 0U; index < 32768U; index++)
    {
        const uint16_t receive_seq = (uint16_t)(index & 0x7FFFU);
        const uint16_t send_seq = (uint16_t)((index + 1U) & 0x7FFFU);
        const uint16_t next_seq = (uint16_t)((index + 2U) & 0x7FFFU);
        /* Only occupied entries are read; avoid clearing the whole log. */
        tx_count = 0U;
        send_pending_reset(receive_seq, send_seq);
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT16(send_seq, frame_ns(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT16(next_seq, iec104_get_send_sn());
        TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
        feed_s_frame(next_seq);
        TEST_ASSERT_EQUAL_UINT16(next_seq, iec104_get_acksn());
        TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
        TEST_ASSERT_TRUE(iec104_is_link_active());
    }
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_receive_sn());
}

void test_maximum_apdu_and_following_frame_are_processed_separately(void)
{
    uint8_t data[261] = {0U};
    const uint8_t test_frame[6] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    start_link();
    build_interrogation(data, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION_REQ);
    data[1] = 253U;
    data[6] = 0xFEU;
    (void)memcpy(&data[255], test_frame, sizeof(test_frame));
    iec104_data_received(data, (uint16_t)sizeof(data));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(255U, tx_log_len[0]);
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&data[10], &tx_log[0][10], 245U);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_CON, frame_u_func(tx_log[1]));
}

void test_oversized_tcp_chunk_is_dropped_and_next_frame_recovers(void)
{
    /* Current receive capacity is 1280 bytes; exercise one byte beyond it. */
    const uint8_t oversized[1281] = {0U};
    const uint8_t frame[6] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    iec104_data_received(frame, 3U);
    libiec104_poll();
    iec104_data_received(oversized, (uint16_t)sizeof(oversized));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_CON, frame_u_func(tx_log[0]));
}

void test_buffer_overflow_closes_without_processing_new_chunk(void)
{
    uint8_t chunk[1280] = {0U};
    const uint8_t frame[6] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    iec104_data_received(frame, 3U);
    libiec104_poll();
    (void)memcpy(chunk, frame, sizeof(frame));
    iec104_data_received(chunk, sizeof(chunk));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_reset_discards_partial_apdu_and_allows_fresh_startdt(void)
{
    const uint8_t frame[6] = {0x68U, 4U, 0x43U, 0U, 0U, 0U};
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    iec104_data_received(frame, 3U);
    libiec104_poll();
    iec104_reset();
    tx_clear();
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_w());
    start_link();
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_clock_command_spontaneous_cot_preserves_time_and_next_activation_succeeds(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    frame[8] = COT_SPONTANEOUS;
    const cp56time2a_t previous = iec104_get_last_clock_sync_time();
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(UkCauseTx, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
    const cp56time2a_t unchanged = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&previous, &unchanged, sizeof(unchanged));

    build_clock_command(frame);
    frame[2] = 2U; /* Next master N(S) = 1, encoded as N(S) << 1. */
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    const cp56time2a_t applied = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&frame[15], &applied, sizeof(applied));
}



void test_fault_ioa_getters_match_reserved_window_boundaries(void)
{
    for (uint32_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
    {
        iec104_line_config_t line = {0};
        line.temporary_fault = iec104_make_ioa_3byte(100000U + feeder * 1000U);
        line.permanent_fault = iec104_make_ioa_3byte(200000U + feeder * 1000U);
        TEST_ASSERT_TRUE(iec104_set_line_config(feeder, &line));
        TEST_ASSERT_EQUAL_UINT32(100000U + feeder * 1180U,
            iec104_ioa_3byte_to_uint32(
                iec104_get_feeder_temporary_fault_ariza_akimi_ioa(
                    feeder, 0U, 0U)));
        TEST_ASSERT_EQUAL_UINT32(100179U + feeder * 1180U,
            iec104_ioa_3byte_to_uint32(
                iec104_get_feeder_temporary_fault_yuk_akimi_varyok_ioa(
                    feeder, 2U, 14U)));
        TEST_ASSERT_EQUAL_UINT32(200179U + feeder * 1180U,
            iec104_ioa_3byte_to_uint32(
                iec104_get_feeder_permanent_fault_yuk_akimi_varyok_ioa(
                    feeder, 2U, 14U)));
    }
}

void test_event_replay_preserves_load_present_bit_on_wire(void)
{
    for (uint8_t load = 0U; 2U > load; load++)
    {
        setup(12U, 8U);
        start_link();
        fault_log_t record = {0};
        iec104_line_config_t line = {0};
        line.temporary_fault = iec104_make_ioa_3byte(100000U);
        line.permanent_fault = iec104_make_ioa_3byte(200000U);
        TEST_ASSERT_TRUE(iec104_set_line_config(0U, &line));
        record.info.nominal_current_status = (0U != load);
        record.info.power_status = 1U;
        record.info.type = (0U != load);
        TEST_ASSERT_TRUE(iec104_emit_evtlog_record(&record));
        TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[1][6]);
        TEST_ASSERT_EQUAL_UINT8(2U, tx_log[1][7] & 0x7FU);
        const size_t ioa_offset = DATA_START_IDX + sizeof(m_sp_tb_1_t);
        const uint32_t ioa = (uint32_t)tx_log[1][ioa_offset] |
            ((uint32_t)tx_log[1][ioa_offset + 1U] << 8U) |
            ((uint32_t)tx_log[1][ioa_offset + 2U] << 16U);
        TEST_ASSERT_EQUAL_UINT32((0U == load) ? 100003U : 200003U, ioa);
        const size_t load_offset = DATA_START_IDX +
            sizeof(m_sp_tb_1_t) + offsetof(m_sp_tb_1_t, siq);
        TEST_ASSERT_EQUAL_UINT8(load, tx_log[1][load_offset] & 1U);
    }
    TEST_ASSERT_FALSE(iec104_emit_evtlog_record(NULL));
}

void test_group_one_emits_only_phase_currents_without_fault_summary(void)
{
    setup(12U, 8U);
    start_link();
    mock_breaker_set_line_in_use(0U, true);
    uint8_t frame[16];
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_GROUP_1);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    uint8_t objects = 0U;
    for (size_t index = 0U; index < tx_count; index++)
    {
        if (M_ME_TF_1 == tx_log[index][6])
        {
            objects = (uint8_t)(objects + (tx_log[index][7] & 0x7FU));
        }
    }
    TEST_ASSERT_EQUAL_UINT8(3U, objects);
}

void test_alarm_event_uses_configured_ioa_value_quality_and_original_time(void)
{
    setup(2U, 2U);
    iec104_line_config_t line = {0};
    line.trip_failed[PHASE_L2] = iec104_make_ioa_3byte(123456U);
    TEST_ASSERT_TRUE(iec104_set_line_config(0U, &line));
    iec104_event_record_t record = {.kind = IEC104_EVENT_TRIP_FAILURE,
        .payload.alarm = {.feeder = 0U, .phase = PHASE_L2, .active = 1U}};
    record.payload.alarm.time =
        cp56time2a_make(12345U, 0U, 12U, 7U, 3U, 10U, 26U);
    TEST_ASSERT_FALSE(iec104_emit_event_record(&record));
    start_link();
    TEST_ASSERT_TRUE(iec104_emit_event_record(&record));
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[0][6]);
    TEST_ASSERT_EQUAL_UINT8(1U, tx_log[0][7] & 0x7FU);
    const m_sp_tb_1_t *object =
        (const m_sp_tb_1_t *)&tx_log[0][DATA_START_IDX];
    TEST_ASSERT_EQUAL_UINT32(123456U,
        iec104_ioa_3byte_to_uint32(object->ioa));
    TEST_ASSERT_EQUAL_UINT8(1U, object->siq.spi);
    TEST_ASSERT_EQUAL_UINT8(0U, object->siq.invalid);
    TEST_ASSERT_EQUAL_MEMORY(&record.payload.alarm.time,
                             &object->timestamp, sizeof(object->timestamp));
    record.payload.alarm.active = 0U;
    record.payload.alarm.time.iv_bit = 1U;
    TEST_ASSERT_TRUE(iec104_emit_event_record(&record));
    object = (const m_sp_tb_1_t *)&tx_log[1][DATA_START_IDX];
    TEST_ASSERT_EQUAL_UINT8(0U, object->siq.spi);
    TEST_ASSERT_EQUAL_UINT8(1U, object->siq.invalid);
    TEST_ASSERT_FALSE(iec104_emit_event_record(&record)); /* k window full. */
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
}

void test_alarm_event_transport_failure_and_missing_ioa_are_not_sent(void)
{
    setup(12U, 8U);
    start_link();
    iec104_event_record_t record = {.kind = IEC104_EVENT_TRIP_FAILURE,
        .payload.alarm = {.feeder = 0U, .phase = PHASE_L1, .active = 1U}};
    TEST_ASSERT_FALSE(iec104_emit_event_record(&record));
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    iec104_line_config_t line = {0};
    line.trip_failed[PHASE_L1] = iec104_make_ioa_3byte(1070U);
    TEST_ASSERT_TRUE(iec104_set_line_config(0U, &line));
    transport_budget = 0;
    TEST_ASSERT_FALSE(iec104_emit_event_record(&record));
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
    TEST_ASSERT_FALSE(iec104_emit_event_record(NULL));
}

void test_current_rf_alarm_snapshot_resumes_with_window_one(void)
{
    setup_mode(1U, 1U, true);
    start_link();
    enable_all_lines();
    TEST_ASSERT_FALSE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(22U, tx_log[0][7] & 0x7FU);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_TRUE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(20U, tx_log[1][7] & 0x7FU);
    setup_mode(2U, 2U, true);
    start_link();
    for (uint8_t feeder = 0U; MAX_POWER_LINE_COUNT > feeder; feeder++)
    {
        iec104_line_config_t line = {.in_use = 1U};
        for (size_t phase = 0U; PHASE_MAX > phase; phase++)
        {
            line.rf_haberlesme_varyok[phase] =
                iec104_make_ioa_3byte(1060U + feeder * 100U + (uint32_t)phase);
            line.trip_failed[phase] =
                iec104_make_ioa_3byte(1070U + feeder * 100U + (uint32_t)phase);
        }
        TEST_ASSERT_TRUE(iec104_set_line_config(feeder, &line));
    }
    TEST_ASSERT_TRUE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(22U, tx_log[0][7] & 0x7FU);
    TEST_ASSERT_EQUAL_UINT8(20U, tx_log[1][7] & 0x7FU);
    const m_sp_tb_1_t *object =
        (const m_sp_tb_1_t *)&tx_log[0][DATA_START_IDX + sizeof(m_sp_tb_1_t)];
    TEST_ASSERT_EQUAL_UINT32(1070U, iec104_ioa_3byte_to_uint32(object->ioa));
}

void test_rtc_service_rejection_returns_negative_confirmation_and_keeps_time(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    const cp56time2a_t previous = iec104_get_last_clock_sync_time();
    mock_rtc_sync_failure(true);
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    const cp56time2a_t actual = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&previous, &actual, sizeof(actual));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
    mock_rtc_sync_failure(false);
    build_clock_command(frame);
    frame[2] = 2U;
    frame[4] = 4U;
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
}

static uint32_t event_ack_count;
static uint16_t acknowledged_record;

static void capture_event_ack(uint16_t record_id)
{
    event_ack_count++;
    acknowledged_record = record_id;
}

static iec104_event_record_t tracked_fault(void)
{
    setup(12U, 8U);
    start_link();
    event_ack_count = 0U;
    acknowledged_record = 0U;
    iec104_line_config_t line = {0};
    line.temporary_fault = iec104_make_ioa_3byte(100000U);
    TEST_ASSERT_TRUE(iec104_set_line_config(0U, &line));
    return (iec104_event_record_t){.kind = IEC104_EVENT_FAULT};
}

void test_tracked_fault_waits_for_both_asdus_and_ignores_duplicate_ack(void)
{
    const iec104_event_record_t record = tracked_fault();
    const uint16_t before = iec104_get_send_sn();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    TEST_ASSERT_TRUE(iec104_event_ack_pending());
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    feed_s_frame((uint16_t)(before + 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_FALSE(iec104_emit_event_record_tracked(&record, 43U,
                                                     capture_event_ack));
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
    TEST_ASSERT_EQUAL_UINT16(42U, acknowledged_record);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
}

void test_tracked_fault_invalid_future_ack_resets_without_consumption(void)
{
    const iec104_event_record_t record = tracked_fault();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    feed_s_frame((uint16_t)(iec104_get_send_sn() + 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    TEST_ASSERT_FALSE(iec104_is_link_active());
}

void test_tracked_fault_reset_drops_old_ack_callback_without_consumption(void)
{
    const iec104_event_record_t record = tracked_fault();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    iec104_reset();
    start_link();
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 43U,
                                                    capture_event_ack));
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(43U, acknowledged_record);
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
}

void test_tracked_fault_partial_transport_failure_resumes_only_second_frame(void)
{
    iec104_event_record_t record = tracked_fault();
    record.payload.fault.info.power_status = 1U;
    transport_budget = 1;
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    TEST_ASSERT_TRUE(iec104_event_ack_pending());
    (void)memset(&record, 0, sizeof(record));
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[1][6]);
    TEST_ASSERT_EQUAL_UINT8(1U, tx_log[1][15]);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
}

void test_tracked_fault_window_one_requires_ack_of_each_frame(void)
{
    const iec104_event_record_t record = tracked_fault();
    setup(1U, 1U);
    start_link();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_ME_TF_1, tx_log[0][6]);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[1][6]);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
}

void test_reset_discards_queued_second_frame_without_consuming_record(void)
{
    const iec104_event_record_t record = tracked_fault();
    transport_budget = 1;
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    iec104_reset();
    transport_budget = -1;
    start_link();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
}

void test_tracked_event_null_arguments_do_not_send_or_arm_callback(void)
{
    const iec104_event_record_t record = tracked_fault();
    TEST_ASSERT_FALSE(iec104_emit_event_record_tracked(NULL, 42U,
                                                     capture_event_ack));
    TEST_ASSERT_FALSE(iec104_emit_event_record_tracked(&record, 42U, NULL));
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
}

void test_tracked_fault_ack_boundary_wraps_from_32767_to_zero(void)
{
    const iec104_event_record_t record = tracked_fault();
    for (uint32_t index = 0U; 32765U > index; index++)
    {
        iec104_send_M_SP_TB_1_spontan(iec104_make_ioa_3byte(1U), 1U, 0U);
        feed_s_frame(iec104_get_send_sn());
        tx_clear();
    }
    TEST_ASSERT_EQUAL_UINT16(32766U, iec104_get_send_sn());
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
    feed_s_frame(32767U);
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
    feed_s_frame(0U);
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
}

void test_i_frame_piggyback_ack_completes_tracked_event(void)
{
    const iec104_event_record_t record = tracked_fault();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    uint8_t frame[16];
    build_interrogation(frame, 0U, iec104_get_send_sn(),
                        TEST_COMMON_ADDRESS, QOI_STATION);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
    TEST_ASSERT_EQUAL_UINT16(42U, acknowledged_record);
}

void test_old_ack_closes_without_completing_tracked_record(void)
{
    const iec104_event_record_t record = tracked_fault();
    const uint16_t before = iec104_get_acksn();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U, capture_event_ack));
    feed_s_frame(before);
    TEST_ASSERT_TRUE(iec104_event_ack_pending());
    feed_s_frame((uint16_t)((before - 1U) & 0x7FFFU));
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_FALSE(iec104_event_ack_pending());
    TEST_ASSERT_EQUAL_UINT32(0U, event_ack_count);
}



void test_gi_confirmation_pn_retains_only_the_low_bit(void)
{
    static const uint8_t values[] = {0U, 1U, 2U, 3U, 255U};
    static const uint8_t expected[] = {0U, 1U, 0U, 1U, 1U};
    for (size_t i = 0U; i < sizeof(values); i++)
    {
        setup(12U, 8U);
        start_link();
        iec104_send_general_interrogation_con(20U, values[i]);
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(expected[i], frame_asdu_pn(tx_log[0]));
    }
}

void test_point_quality_flags_and_value_bits_preserve_wire_encoding(void)
{
    const ioa_3byte_t ioa = {0};
    static const uint8_t quality[] =
        {0x00U, 0x10U, 0x20U, 0x40U, 0x80U, 0xF0U};
    static const uint8_t single[] =
        {0x01U, 0x11U, 0x21U, 0x41U, 0x81U, 0xF1U};
    static const uint8_t double_point[] =
        {0x03U, 0x13U, 0x23U, 0x43U, 0x83U, 0xF3U};
    for (size_t i = 0U; i < sizeof(quality); i++)
    {
        setup(12U, 8U);
        start_link();
        iec104_send_M_SP_TB_1_spontan(ioa, 255U, quality[i]);
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(M_SP_TB_1, tx_log[0][6]);
        TEST_ASSERT_EQUAL_HEX8(single[i], tx_log[0][15]);
        tx_clear();
        iec104_send_M_DP_TB_1_spontan(ioa, 255U, quality[i]);
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(M_DP_TB_1, tx_log[0][6]);
        TEST_ASSERT_EQUAL_HEX8(double_point[i], tx_log[0][15]);
    }
}


void test_invalid_clock_fields_do_not_publish_time_and_next_command_recovers(void)
{
    static const uint8_t offsets[] = {16U, 17U, 18U, 19U, 20U, 20U, 21U};
    static const uint8_t values[] = {0xFFU, 60U, 24U, 0U, 0U, 13U, 100U};
    for (size_t index = 0U; sizeof(offsets) > index; index++)
    {
        uint8_t frame[22];
        setup(64U, 32U);
        start_link();
        build_clock_command(frame);
        frame[offsets[index]] = values[index];
        const cp56time2a_t previous = iec104_get_last_clock_sync_time();
        iec104_data_received(frame, (uint16_t)sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
        const cp56time2a_t actual = iec104_get_last_clock_sync_time();
        TEST_ASSERT_EQUAL_MEMORY(&previous, &actual, sizeof(actual));
        build_clock_command(frame);
        frame[2] = 2U; /* Next receive sequence, 1. */
        frame[4] = 4U; /* Acknowledge the negative response, sequence 2. */
        iec104_data_received(frame, (uint16_t)sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[1]));
        TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    }
}

void test_nonexistent_calendar_day_is_rejected_without_clock_update(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    frame[19] = 30U;
    frame[20] = 2U;
    iec104_data_received(frame, (uint16_t)sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
}



static void setup_idle_timeout(uint16_t t3)
{
    setup(64U, 32U);
    const iec104_config_t cfg = {
        .t0_max = 30U, .t1_max = 15U, .t2_max = 10U, .t3_max = t3,
        .k_max = 64U, .w_max = 32U, .common_address = TEST_COMMON_ADDRESS
    };
    iec104_init(&test_io, &cfg);
    start_link();
}

void test_idle_probe_fires_exactly_at_t3_plus_two_and_is_not_duplicated(void)
{
    const uint16_t values[] = {20U, 60U, 65525U};
    for (size_t index = 0U; index < 3U; index++)
    {
        setup_idle_timeout(values[index]);
        for (uint32_t tick = 0U; tick < (uint32_t)values[index] + 1U; tick++)
        {
            iec104_tick();
        }
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
        iec104_tick();
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_ACT, frame_u_func(tx_log[0]));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        feed_u_frame(IEC104_TESTFR_CON);
        TEST_ASSERT_TRUE(iec104_is_link_active());
    }
}

void test_received_frame_restarts_idle_window_but_partial_bytes_do_not(void)
{
    setup_idle_timeout(20U);
    for (uint32_t tick = 0U; tick < 21U; tick++)
    {
        iec104_tick();
    }
    feed_u_frame(IEC104_TESTFR_ACT);
    tx_clear();
    for (uint32_t tick = 0U; tick < 21U; tick++)
    {
        iec104_tick();
    }
    const uint8_t partial[] = {0x68U, 0x04U};
    iec104_data_received(partial, sizeof(partial));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    iec104_tick();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_ACT, frame_u_func(tx_log[0]));
}

void test_active_t3_remains_unchanged_until_reinitialization(void)
{
    setup_idle_timeout(60U);
    iec104_config_set_t3_max(120U);
    TEST_ASSERT_EQUAL_UINT16(120U, iec104_config_get_t3_max());
    TEST_ASSERT_EQUAL_UINT16(60U, iec104_get_t3_timeout());
    setup_idle_timeout(120U);
    TEST_ASSERT_EQUAL_UINT16(120U, iec104_get_t3_timeout());
    setup_idle_timeout(19U);
    TEST_ASSERT_EQUAL_UINT16(60U, iec104_get_t3_timeout());
    setup_idle_timeout(65535U);
    TEST_ASSERT_EQUAL_UINT16(60U, iec104_get_t3_timeout());
}

void test_startdt_preserves_coalesced_following_frame(void)
{
    uint8_t frames[22] = {0x68U, 4U, 7U, 0U, 0U, 0U};
    setup(64U, 32U);
    build_interrogation(&frames[6], 0U, 0U, TEST_COMMON_ADDRESS,
                        QOI_STATION_REQ);
    iec104_data_received(frames, sizeof(frames));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_receive_sn());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0,
        find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM));
}

void test_duplicate_startdt_preserves_sequences_and_pending_frames(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    const uint16_t next_sn = iec104_get_send_sn();
    const uint16_t pending = iec104_get_k();
    tx_clear();
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_EQUAL_UINT16(next_sn, iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(pending, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
}

void test_stop_start_preserves_connection_sequence_numbers(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    feed_s_frame(iec104_get_send_sn());
    const uint16_t next_sn = iec104_get_send_sn();
    feed_u_frame(IEC104_STOPDT_ACT);
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_receive_sn());
    TEST_ASSERT_EQUAL_UINT16(next_sn, iec104_get_send_sn());
}

void test_rejected_startdt_confirmation_does_not_activate_link(void)
{
    setup(64U, 32U);
    transport_budget = 0;
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, event_count);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_send_sn());
    transport_budget = -1;
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_TRUE(iec104_is_link_active());
}

void test_rejected_reset_confirmation_does_not_request_reboot(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    transport_budget = 0;
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, 1U);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_short_i_frame_closes_without_using_previous_asdu(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    tx_clear();
    const uint8_t frame[] = {0x68U, 4U, 2U, 0U, 4U, 0U};
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_command_vsq_cannot_claim_missing_objects_or_zero_objects(void)
{
    const uint8_t counts[] = {0U, 2U, 0x82U};
    for (size_t index = 0U; index < sizeof(counts); index++)
    {
        uint8_t frame[16];
        setup(64U, 32U);
        start_link();
        build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, 1U);
        frame[7] = counts[index];
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
        TEST_ASSERT_EQUAL(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    }
}

static void configure_live_ioas(void)
{
    for (uint8_t feeder = 0U; feeder < MAX_POWER_LINE_COUNT; feeder++)
    {
        iec104_line_config_t line = {.in_use = 1U};
        ioa_3byte_t *fields[] = {line.anlik_akim, line.enerji_varyok,
            line.yuk_akimi_varyok, line.rf_haberlesme_varyok,
            line.trip_failed};
        for (size_t family = 0U; family < 5U; family++)
        {
            for (size_t phase = 0U; phase < 3U; phase++)
            {
                fields[family][phase] = iec104_make_ioa_3byte((uint32_t)
                    (1U + family * MAX_POWER_LINE_COUNT * 3U +
                     feeder * 3U + phase));
            }
        }
        TEST_ASSERT_TRUE(iec104_set_line_config(feeder, &line));
    }
}

static void assert_unique_interrogated_points(uint8_t qoi)
{
    bool seen[MAX_POWER_LINE_COUNT * 3U * 5U] = {false};
    size_t total = 0U;
    for (size_t index = 0U; index < tx_count; index++)
    {
        if (!frame_is_i(tx_log[index]) ||
            (qoi != frame_asdu_cot(tx_log[index])))
        {
            continue;
        }
        const size_t count = tx_log[index][7] & 0x7FU;
        const uint8_t type = frame_asdu_type(tx_log[index]);
        const size_t object_size = (20U == qoi) ?
            ((M_ME_NC_1 == type) ? 8U : 4U) :
            ((M_ME_TF_1 == type) ? 15U : 11U);
        for (size_t object = 0U; object < count; object++)
        {
            const uint8_t *data = &tx_log[index][12U + object * object_size];
            const uint32_t ioa = (uint32_t)data[0] |
                ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U);
            TEST_ASSERT_TRUE((ioa > 0U) && (ioa <= sizeof(seen)));
            TEST_ASSERT_FALSE(seen[ioa - 1U]);
            seen[ioa - 1U] = true;
            total++;
        }
    }
    const size_t families = (20U == qoi) ? 5U : ((21U == qoi) ? 1U : 4U);
    TEST_ASSERT_EQUAL_UINT32(MAX_POWER_LINE_COUNT * 3U * families, total);
}

void test_interrogation_window_one_resumes_confirm_data_and_termination(void)
{
    const uint8_t queries[] = {20U, 21U, 22U};
    for (size_t query = 0U; query < sizeof(queries); query++)
    {
        uint8_t frame[16];
        setup_mode(1U, 1U, true);
        start_link();
        configure_live_ioas();
        build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS,
                            queries[query]);
        frame[9] = 17U;
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
        for (size_t attempt = 0U; attempt < 64U; attempt++)
        {
            if (find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM) >= 0)
            {
                break;
            }
            feed_s_frame(iec104_get_send_sn());
        }
        const int term = find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, term);
        TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[term]));
        size_t objects = 0U;
        for (size_t index = 0U; index < tx_count; index++)
        {
            if (frame_is_i(tx_log[index]))
            {
                TEST_ASSERT_EQUAL_UINT8(17U, tx_log[index][9]);
                if (C_IC_NA_1 != frame_asdu_type(tx_log[index]))
                {
                    objects += tx_log[index][7] & 0x7FU;
                }
            }
        }
        const size_t families = (20U == queries[query]) ? 5U :
                                ((21U == queries[query]) ? 1U : 4U);
        TEST_ASSERT_EQUAL_UINT32(MAX_POWER_LINE_COUNT * 3U * families,
                                 objects);
        assert_unique_interrogated_points(queries[query]);
    }
}

void test_stopdt_accepts_pending_ack_before_confirming_stop(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    send_initialization(1U);
    const uint16_t next_sn = iec104_get_send_sn();
    tx_clear();
    feed_u_frame(IEC104_STOPDT_ACT);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count); /* S ACK for received I-frame. */
    TEST_ASSERT_FALSE(frame_is_u(tx_log[0]));
    feed_s_frame(next_sn);
    TEST_ASSERT_EQUAL_UINT16(0U, iec104_get_k());
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_STOPDT_CON, frame_u_func(tx_log[1]));
}

void test_stopdt_outstanding_ack_keeps_t1_running(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    feed_u_frame(IEC104_STOPDT_ACT);
    for (size_t second = 0U; second < 16U; second++)
    {
        iec104_tick();
        libiec104_poll();
    }
    TEST_ASSERT_EQUAL(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_stopdt_retries_rejected_confirmation(void)
{
    setup(64U, 32U);
    start_link();
    transport_budget = 0;
    feed_u_frame(IEC104_STOPDT_ACT);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_STOPDT_CON, frame_u_func(tx_log[0]));
}

void test_duplicate_startdt_preserves_tracked_delivery_callback(void)
{
    const iec104_event_record_t record = tracked_fault();
    TEST_ASSERT_TRUE(iec104_emit_event_record_tracked(&record, 42U,
                                                    capture_event_ack));
    const uint16_t next_sn = iec104_get_send_sn();
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_TRUE(iec104_event_ack_pending());
    TEST_ASSERT_EQUAL_UINT16(next_sn, iec104_get_send_sn());
    feed_s_frame(next_sn);
    TEST_ASSERT_EQUAL_UINT32(1U, event_ack_count);
    TEST_ASSERT_EQUAL_UINT16(42U, acknowledged_record);
}

void test_single_object_sequence_vsq_remains_accepted(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, 20U);
    frame[7] = 0x81U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0,
        find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM));
}

void test_oversized_control_frame_closes_without_activating_link(void)
{
    setup(64U, 32U);
    const uint8_t frame[] = {0x68U, 5U, 7U, 0U, 0U, 0U, 0U};
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_interrogation_retries_confirmation_and_termination(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, 20U);
    transport_budget = 0;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    transport_budget = 1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_TERM, frame_asdu_cot(tx_log[1]));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
}

void test_reset_cancels_pending_live_interrogation(void)
{
    uint8_t frame[16];
    setup(1U, 1U);
    start_link();
    configure_live_ioas();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, 20U);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, count_i_frames());
    iec104_reset();
    start_link();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_other_command_does_not_replace_pending_interrogation_originator(void)
{
    uint8_t frame[16];
    setup(1U, 1U);
    start_link();
    configure_live_ioas();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, 20U);
    frame[9] = 17U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    /* A second command updates the reply address while GI is blocked. */
    build_reset_process(frame, 1U, 1U, TEST_COMMON_ADDRESS, 0U, 0U);
    frame[9] = 99U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    for (size_t attempt = 0U; attempt < 64U; attempt++)
    {
        feed_s_frame(iec104_get_send_sn());
        if (find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM) >= 0)
        {
            break;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0,
        find_asdu(C_IC_NA_1, COT_ACTIVATION_TERM));
    for (size_t index = 0U; index < tx_count; index++)
    {
        if (frame_is_i(tx_log[index]))
        {
            TEST_ASSERT_EQUAL_UINT8((C_RP_NA_1 == frame_asdu_type(tx_log[index])) ? 99U : 17U,
                                    frame_asdu_oa(tx_log[index]));
        }
    }
}

void test_foreign_ioa_rejects_all_queries_without_starting_producer(void)
{
    const uint8_t queries[] = {20U, 21U, 22U, 23U, 24U};
    for (size_t index = 0U; index < sizeof(queries); index++)
    {
        uint8_t frame[16];
        setup(64U, 32U);
        start_link();
        build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, queries[index]);
        frame[12] = 1U;
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(UkIOA, frame_asdu_cot(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_NOT_EQUAL(IEC104_EVT_SEND_TEMP_FAULTS, last_event);
        TEST_ASSERT_NOT_EQUAL(IEC104_EVT_SEND_PERM_FAULTS, last_event);
    }
}

void test_clock_foreign_ioa_preserves_rtc_and_last_clock_sync(void)
{
    uint8_t frame[22] = {0x68U, 20U, 0U, 0U, 2U, 0U,
        103U, 1U, 6U, 0U, 0U, 0U, 1U, 0U, 0U,
        0U, 0U, 23U, 12U, 11U, 3U, 26U};
    setup(64U, 32U);
    start_link();
    frame[10] = (uint8_t)TEST_COMMON_ADDRESS;
    frame[11] = (uint8_t)(TEST_COMMON_ADDRESS >> 8U);
    const cp56time2a_t before = iec104_get_last_clock_sync_time();
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
    const cp56time2a_t after = iec104_get_last_clock_sync_time();
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL_UINT8(UkIOA, frame_asdu_cot(tx_log[0]));
}

static siq_t battery_quality;

static int battery_warning(siq_t *value)
{
    *value = battery_quality;
    return 0;
}

static void setup_battery(void)
{
    setup(64U, 32U);
    iec104_io_t io = test_io;
    io.get_battery_warning = battery_warning;
    const iec104_config_t cfg = {.common_address = TEST_COMMON_ADDRESS,
        .k_max = 1U, .w_max = 32U, .t1_max = 15U, .t2_max = 10U,
        .t3_max = 60U};
    iec104_init(&io, &cfg);
    iec104_config_set_ioa_aku_uyarisi(iec104_make_ioa_3byte(10000U));
    battery_quality = (siq_t){.invalid = 1U};
    start_link();
}

void test_battery_warning_retries_changes_and_invalid_quality(void)
{
    setup_battery();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_k());
    battery_quality = (siq_t){.spi = 1U};
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_SP_NA_1, tx_log[0][6]);
    TEST_ASSERT_EQUAL_UINT8(1U, tx_log[0][15]);
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    battery_quality = (siq_t){0};
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT8(0U, tx_log[1][15]);
    battery_quality = (siq_t){.invalid = 1U};
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_HEX8(0x80U, tx_log[2][15]);
}

void test_battery_warning_is_in_station_query_before_termination(void)
{
    setup_battery();
    uint8_t frame[16];
    build_interrogation(frame, 0U, iec104_get_send_sn(),
                        TEST_COMMON_ADDRESS, 20U);
    frame[9] = 0x3CU;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    feed_s_frame(iec104_get_send_sn());
    feed_s_frame(iec104_get_send_sn());
    TEST_ASSERT_EQUAL_UINT16(3U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_SP_NA_1, tx_log[1][6]);
    TEST_ASSERT_EQUAL_UINT8(20U, frame_asdu_cot(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT8(0x3CU, tx_log[1][9]);
    TEST_ASSERT_EQUAL_HEX8(0x10U, tx_log[1][12]);
    TEST_ASSERT_EQUAL_HEX8(0x27U, tx_log[1][13]);
    TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_TERM, frame_asdu_cot(tx_log[2]));
}

void test_empty_rf_snapshot_finishes_cursor_before_lines_are_enabled_again(void)
{
    setup_mode(1U, 1U, true);
    start_link();
    enable_all_lines();
    TEST_ASSERT_FALSE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    for (uint8_t line = 0U; line < MAX_POWER_LINE_COUNT; line++)
    {
        mock_breaker_set_line_in_use(line, false);
    }
    TEST_ASSERT_TRUE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    feed_s_frame(iec104_get_send_sn());
    enable_all_lines();
    TEST_ASSERT_FALSE(iec104_send_rf_communication_states(COT_SPONTANEOUS));
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(22U, tx_log[1][7] & 0x7FU);
}




void test_flagged_clock_commands_preserve_rtc_and_wire_test_flag(void)
{
    const uint8_t flags[] = {0x80U, 0x40U, 0xC0U};
    for (size_t index = 0U; index < sizeof(flags); index++)
    {
        uint8_t frame[22];
        setup(64U, 32U);
        start_link();
        build_clock_command(frame);
        frame[8] |= flags[index];
        const cp56time2a_t previous = iec104_get_last_clock_sync_time();
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
        const cp56time2a_t actual = iec104_get_last_clock_sync_time();
        TEST_ASSERT_EQUAL_MEMORY(&previous, &actual, sizeof(actual));
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
        TEST_ASSERT_EQUAL_UINT8(COT_ACTIVATION_CON, frame_asdu_cot(tx_log[0]));
        TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_EQUAL_HEX8(flags[index] & 0x80U, tx_log[0][8] & 0x80U);
        TEST_ASSERT_EQUAL_MEMORY(&frame[9], &tx_log[0][9], 13U);
    }
}

void test_flagged_reset_never_reboots_and_preserves_test_flag(void)
{
    const uint8_t flags[] = {0x80U, 0x40U, 0xC0U};
    for (size_t index = 0U; index < sizeof(flags); index++)
    {
        uint8_t frame[16];
        setup(64U, 32U);
        start_link();
        build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, IEC104_QRP_GENERAL_RESET);
        frame[8] |= flags[index];
        iec104_data_received(frame, sizeof(frame));
        libiec104_poll();
        TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
        TEST_ASSERT_EQUAL_UINT8(1U, frame_asdu_pn(tx_log[0]));
        TEST_ASSERT_EQUAL_HEX8(flags[index] & 0x80U, tx_log[0][8] & 0x80U);
        TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    }
}

void test_test_interrogation_is_rejected_without_sending_process_values(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    enable_all_lines();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, QOI_STATION);
    frame[8] |= 0x80U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_HEX8(0xC7U, tx_log[0][8]);
    TEST_ASSERT_EQUAL_UINT8(C_IC_NA_1, tx_log[0][6]);
}

void test_clock_confirmation_survives_full_window_without_reapplying_time(void)
{
    uint8_t frame[22];
    setup(1U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    build_clock_command(frame);
    frame[2] = 2U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    feed_s_frame(2U);
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(C_CS_NA_1, tx_log[1][6]);
    TEST_ASSERT_EQUAL_UINT16(2U, frame_ns(tx_log[1]));
    TEST_ASSERT_EQUAL_UINT16(2U, frame_nr(tx_log[1]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[9], &tx_log[1][9], 13U);
    feed_s_frame(3U);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
}

void test_clock_confirmation_retries_transport_rejection_once(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    transport_budget = 0;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    transport_budget = -1;
    libiec104_poll();
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    TEST_ASSERT_EQUAL_MEMORY(&frame[9], &tx_log[0][9], 13U);
}

void test_reset_reboots_only_after_deferred_confirmation_is_accepted(void)
{
    uint8_t frame[16];
    setup(1U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    build_reset_process(frame, 1U, 1U, TEST_COMMON_ADDRESS, 0U, IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
    feed_s_frame(2U);
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(C_RP_NA_1, tx_log[1][6]);
    TEST_ASSERT_EQUAL_UINT8(0U, frame_asdu_pn(tx_log[1]));
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REBOOT_REQUESTED, last_event);
    const unsigned int events = event_count;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT(events, event_count);
}

void test_pending_reply_overload_closes_before_second_clock_side_effect(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    transport_budget = 0;
    build_clock_command(frame);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    frame[2] = 2U;
    frame[17] = 58U;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_session_reset_discards_deferred_reboot_confirmation(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    transport_budget = 0;
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    iec104_reset();
    transport_budget = -1;
    start_link();
    TEST_ASSERT_NOT_EQUAL(IEC104_EVT_REBOOT_REQUESTED, last_event);
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
}

void test_deferred_reply_timeout_closes_without_reboot(void)
{
    uint8_t frame[16];
    setup(64U, 32U);
    start_link();
    transport_budget = 0;
    build_reset_process(frame, 0U, 1U, TEST_COMMON_ADDRESS, 0U, IEC104_QRP_GENERAL_RESET);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    advance_ticks(15U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_partial_ack_of_same_age_frames_does_not_extend_t1(void)
{
    setup(64U, 32U);
    start_link();
    send_pending_reset(0U, 1U);
    send_pending_reset(1U, 1U);
    advance_ticks(14U);
    feed_s_frame(2U);
    advance_ticks(1U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
}

void test_stopdt_keeps_idle_probe_and_test_confirmation_timeout_running(void)
{
    setup(64U, 32U);
    start_link();
    feed_u_frame(IEC104_STOPDT_ACT);
    tx_clear();
    advance_ticks(22U);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(IEC104_TESTFR_ACT, frame_u_func(tx_log[0]));
    advance_ticks(16U);
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

void test_stopped_connection_responds_to_test_and_resumes_without_new_initialization(void)
{
    setup(64U, 32U);
    start_link();
    const uint16_t ns = iec104_get_send_sn();
    feed_u_frame(IEC104_STOPDT_ACT);
    tx_clear();
    advance_ticks(22U);
    feed_u_frame(IEC104_TESTFR_CON);
    advance_ticks(14U);
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(ns, iec104_get_send_sn());
}

void test_initialization_retries_after_transport_failure_and_is_sent_once_per_session(void)
{
    setup(64U, 32U);
    transport_budget = 1;
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(M_EI_NA_1, tx_log[1][6]);
    feed_s_frame(iec104_get_send_sn());
    feed_u_frame(IEC104_STARTDT_ACT);
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, iec104_get_send_sn());
}

void test_bad_prefix_cannot_execute_following_clock_command(void)
{
    uint8_t frame[23] = {0x55U};
    setup(64U, 32U);
    start_link();
    build_clock_command(&frame[1]);
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT32(0U, mock_rtc_sync_count());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}



void test_t1_timestamp_slots_are_reused_after_ack_without_stale_deadlines(void)
{
    setup(1U, 32U);
    start_link();
    for (uint16_t index = 0U; index < 300U; index++)
    {
        tx_clear();
        send_pending_reset(index, iec104_get_send_sn());
        advance_ticks(1U);
        feed_s_frame(iec104_get_send_sn());
        TEST_ASSERT_TRUE(iec104_is_link_active());
    }
    tx_clear();
    send_pending_reset(300U, iec104_get_send_sn());
    advance_ticks(14U);
    TEST_ASSERT_TRUE(iec104_is_link_active());
    advance_ticks(1U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
}

void test_deferred_maximum_negative_reply_preserves_full_asdu(void)
{
    uint8_t frame[255] = {0};
    setup(64U, 32U);
    start_link();
    build_interrogation(frame, 0U, 1U, TEST_COMMON_ADDRESS, 20U);
    frame[1] = 253U;
    frame[6] = 0xFEU;
    frame[9] = 0x3CU;
    for (size_t index = 12U; index < sizeof(frame); index++)
    {
        frame[index] = (uint8_t)index;
    }
    transport_budget = 0;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(0U, tx_count);
    transport_budget = -1;
    libiec104_poll();
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
    TEST_ASSERT_EQUAL_UINT16(sizeof(frame), tx_log_len[0]);
    TEST_ASSERT_EQUAL_UINT8(UkTypeId, frame_asdu_cot(tx_log[0]));
    TEST_ASSERT_EQUAL_MEMORY(&frame[9], &tx_log[0][9], sizeof(frame) - 9U);
}

void test_stop_start_retains_deferred_reply_without_reapplying_clock(void)
{
    uint8_t frame[22];
    setup(64U, 32U);
    start_link();
    build_clock_command(frame);
    transport_budget = 0;
    iec104_data_received(frame, sizeof(frame));
    libiec104_poll();
    transport_budget = -1;
    feed_u_frame(IEC104_STOPDT_ACT);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_UINT16(2U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(1U, tx_log[0][2]);
    TEST_ASSERT_EQUAL_UINT8(IEC104_STOPDT_CON, frame_u_func(tx_log[1]));
    feed_u_frame(IEC104_STARTDT_ACT);
    TEST_ASSERT_EQUAL_UINT16(4U, tx_count);
    TEST_ASSERT_EQUAL_UINT8(C_CS_NA_1, tx_log[3][6]);
    TEST_ASSERT_EQUAL_UINT16(1U, frame_ns(tx_log[3]));
    TEST_ASSERT_EQUAL_UINT32(1U, mock_rtc_sync_count());
}

void test_initialization_transport_stall_closes_at_t1(void)
{
    setup(64U, 32U);
    transport_budget = 1;
    feed_u_frame(IEC104_STARTDT_ACT);
    advance_ticks(15U);
    TEST_ASSERT_FALSE(iec104_is_link_active());
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    TEST_ASSERT_EQUAL_UINT16(1U, tx_count);
}

void test_bad_prefix_and_invalid_length_close_before_complete_control_field(void)
{
    const uint8_t bad_prefix[] = {0x55U};
    const uint8_t bad_length[] = {0x68U, 3U};
    setup(64U, 32U);
    iec104_data_received(bad_prefix, sizeof(bad_prefix));
    libiec104_poll();
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
    setup(64U, 32U);
    iec104_data_received(bad_length, sizeof(bad_length));
    libiec104_poll();
    TEST_ASSERT_EQUAL_INT(IEC104_EVT_REQUEST_SOCKET_CLOSE, last_event);
}

/*** end of file ***/
