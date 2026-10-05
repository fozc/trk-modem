/*
 * test_at_engine_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_gsm_log.h"
#include "ring_buff.h"
#include <string.h>

#undef CCSLOG
#define CCSLOG(color, ...) ((void)(color))
#include "../../Application/gsm/at_engine2.c"
/* Legacy helpers outside this scenario have existing conversion warnings.
 * Keep strict diagnostics for the AT motor and all new test code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "../../Application/gsm/utils.c"
#pragma GCC diagnostic pop

UART_HandleTypeDef huart1;
static uint32_t now;
static uint32_t dma_calls;
static uint32_t abort_calls;
static uint32_t urc_calls;
static uint32_t liveness_calls;
static HAL_StatusTypeDef dma_result;
static bool complete_during_start;
static uint8_t transmitted[AT_ENGINE_RX_BUFFER_SIZE];
static uint16_t transmitted_len;
static uint8_t last_urc[AT_ENGINE_URC_BUFFER_SIZE];

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return now;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_EQUAL_PTR(&huart1, handle);
    TEST_ASSERT_TRUE(length <= sizeof(transmitted));
    memcpy(transmitted, data, length);
    transmitted_len = length;
    dma_calls++;
    if (complete_during_start)
    {
        at_engine_dma_tx_complete_callback();
    }
    return dma_result;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *handle)
{
    TEST_ASSERT_EQUAL_PTR(&huart1, handle);
    abort_calls++;
    return HAL_OK;
}
void gsm_URC_callback(uint8_t *message, uint16_t length)
{
    TEST_ASSERT_TRUE(length < sizeof(last_urc));
    memcpy(last_urc, message, length);
    last_urc[length] = 0U;
    urc_calls++;
}
void gsm_wtd_liveness_ping(void)
{
    liveness_calls++;
}
void setUp(void)
{
    memset(&at_engine, 0, sizeof(at_engine));
    s_tx_done_flag = 0U;
    now = 100U;
    dma_calls = 0U;
    abort_calls = 0U;
    urc_calls = 0U;
    liveness_calls = 0U;
    dma_result = HAL_OK;
    complete_during_start = false;
    memset(last_urc, 0, sizeof(last_urc));
    bsp_get_tick_Stub(get_tick);
    gsm_log_get_level_IgnoreAndReturn(GSM_LOG_OFF);
    at_engine_init();
}
void tearDown(void)
{
}
static void start_command(const char *command, uint8_t retries,
    uint32_t timeout)
{
    TEST_ASSERT_TRUE(at_engine_send_at_command(command,
        (uint16_t)strlen(command), "OK\r\n", 4U, retries, timeout));
    (void)at_engine_process();
    at_engine_dma_tx_complete_callback();
}
static void receive_bytes(const void *bytes, size_t length)
{
    const uint8_t *data = bytes;
    for (size_t i = 0U; i < length; i++)
    {
        at_engine_rx_byte(data[i]);
    }
    (void)at_engine_process();
}

void test_at_command_rejects_null_empty_and_oversized_input(void)
{
    uint8_t command[128];
    memset(command, 'A', sizeof(command));
    TEST_ASSERT_FALSE(at_engine_send_at_command(NULL, 1U,
        "OK\r\n", 4U, 1U, 1000U));
    TEST_ASSERT_FALSE(at_engine_send_at_command(command, 0U,
        "OK\r\n", 4U, 1U, 1000U));
    TEST_ASSERT_FALSE(at_engine_send_at_command(command, 128U,
        "OK\r\n", 4U, 1U, 1000U));
    TEST_ASSERT_FALSE(at_engine_is_busy());
    TEST_ASSERT_EQUAL_UINT32(0U, dma_calls);
}
void test_at_maximum_command_fits_and_busy_rejection_preserves_it(void)
{
    uint8_t command[127];
    memset(command, 'A', sizeof(command));
    TEST_ASSERT_TRUE(at_engine_send_at_command(command, sizeof(command),
        "OK\r\n", 4U, 1U, 1000U));
    TEST_ASSERT_FALSE(at_engine_send_at_command("AT\r", 3U,
        "OK\r\n", 4U, 1U, 1000U));
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT16(127U, transmitted_len);
    TEST_ASSERT_EQUAL_MEMORY(command, transmitted, sizeof(command));
}
void test_at_zero_retries_finish_without_transmission(void)
{
    start_command("AT\r", 0U, 1000U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
    TEST_ASSERT_FALSE(at_engine_is_busy());
    TEST_ASSERT_EQUAL_UINT32(0U, dma_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, liveness_calls);
}
void test_at_ok_response_and_get_response_null_length(void)
{
    static const char response[] = "\r\nOK\r\n";
    start_command("AT\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_FALSE(at_engine_is_busy());
    TEST_ASSERT_EQUAL_MEMORY(response, at_engine_get_response(NULL),
        sizeof(response) - 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, liveness_calls);
}
void test_at_error_and_no_sim_responses_are_distinct(void)
{
    start_command("AT+CSQ\r", 1U, 1000U);
    receive_bytes("\r\nERROR\r\n", 9U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_ERROR, at_engine_get_result());
    at_engine_reset();
    start_command("AT+CSQ\r", 1U, 1000U);
    receive_bytes("\r\n+CME ERROR: 10\r\n", 18U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NO_SIM, at_engine_get_result());
}
void test_at_prompt_without_final_newline_completes(void)
{
    TEST_ASSERT_TRUE(at_engine_send_at_command("AT#SSEND=1\r", 11U,
        "\r\n> ", 4U, 1U, 1000U));
    (void)at_engine_process();
    at_engine_dma_tx_complete_callback();
    receive_bytes("\r\n> ", 4U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}
void test_at_timeout_boundary_and_retry_exhaustion(void)
{
    start_command("AT\r", 1U, 1000U);
    now = 1099U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_WAIT_RESPONSE, at_engine.state);
    now = 1100U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_SEND_CMD, at_engine.state);
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
}
void test_at_timeout_is_correct_across_tick_wrap(void)
{
    now = UINT32_MAX - 400U;
    start_command("AT\r", 1U, 1000U);
    now = 598U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_WAIT_RESPONSE, at_engine.state);
    now = 599U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
}
void test_at_retry_discards_only_previous_command_response(void)
{
    start_command("AT\r", 2U, 1000U);
    receive_bytes("partial", 7U);
    now = 1100U;
    (void)at_engine_process();
    (void)at_engine_process();
    at_engine_dma_tx_complete_callback();
    TEST_ASSERT_EQUAL_UINT32(2U, dma_calls);
    TEST_ASSERT_EQUAL_UINT16(0U, at_engine.response_buffer_len);
    receive_bytes("\r\nOK\r\n", 6U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}
void test_at_dma_completion_during_start_is_not_overwritten(void)
{
    complete_during_start = true;
    TEST_ASSERT_TRUE(at_engine_send_at_command("AT\r", 3U,
        "OK\r\n", 4U, 1U, 1000U));
    (void)at_engine_process();
    TEST_ASSERT_TRUE(at_engine_is_tx_done());
}
void test_at_reset_aborts_inflight_dma_and_returns_to_idle(void)
{
    TEST_ASSERT_TRUE(at_engine_send_at_command("AT\r", 3U,
        "OK\r\n", 4U, 1U, 1000U));
    (void)at_engine_process();
    at_engine_reset();
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    TEST_ASSERT_TRUE(at_engine_is_tx_done());
    TEST_ASSERT_FALSE(at_engine_is_busy());
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
}
void test_at_missing_dma_completion_aborts_only_after_tx_deadline(void)
{
    TEST_ASSERT_TRUE(at_engine_send_at_command("AT\r", 3U,
        "OK\r\n", 4U, 2U, 1000U));
    (void)at_engine_process();
    now = 1100U;
    (void)at_engine_process();
    now = 3099U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(0U, abort_calls);
    now = 3100U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(1U, abort_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, dma_calls);
}
void test_at_dma_start_failure_does_not_leave_completion_flag_stuck(void)
{
    dma_result = HAL_BUSY;
    TEST_ASSERT_TRUE(at_engine_send_at_command("AT\r", 3U,
        "OK\r\n", 4U, 1U, 1000U));
    (void)at_engine_process();
    TEST_ASSERT_TRUE(at_engine_is_tx_done());
    now = 1100U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
}
void test_at_data_mode_preserves_binary_bytes(void)
{
    const uint8_t payload[] = {0U, 0xFFU, '\r', '\n', 'O', 'K'};
    TEST_ASSERT_TRUE(at_engine_send_data(payload, sizeof(payload), 1000U));
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_MEMORY(payload, transmitted, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(sizeof(payload), transmitted_len);
}
void test_at_srecv_embedded_ok_is_not_response_terminator(void)
{
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes("\r\n#SRECV: 3,6\r\n\r\nOK\r\n", 21U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    receive_bytes("\r\n\r\nOK\r\n", 8U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}
void test_at_srecv_payload_ending_ok_does_not_complete_on_separator(void)
{
    static const char response[] = "\r\n#SRECV: 3,4\r\n"
        "\x11\x00OK\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    receive_bytes("\r\nOK\r\n", 6U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}
void test_at_srecv_binary_error_and_urc_patterns_are_not_dispatched(void)
{
    static const char response[] = "\r\n#SRECV: 3,22\r\n"
        "\r\nERROR\r\nSRING: 3,6\r\n\0";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    receive_bytes("\r\n\r\nOK\r\n", 8U);
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    uint16_t length;
    const uint8_t *actual = at_engine_get_response(&length);
    TEST_ASSERT_EQUAL_UINT16(sizeof(response) - 1U + 8U, length);
    TEST_ASSERT_EQUAL_MEMORY(response, actual, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL_MEMORY("\r\n\r\nOK\r\n",
        actual + sizeof(response) - 1U, 8U);
    now += 2000U;
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(1U, dma_calls);
}
void test_at_srecv_all_transport_split_points_preserve_response(void)
{
    static const char response[] = "\r\n#SRECV: 3,6\r\n"
        "\x68\x04\x07\x00\x00\x00\r\n\r\nOK\r\n";
    for (size_t split = 0U; split < sizeof(response); split++)
    {
        at_engine_reset();
        at_engine_init();
        start_command("AT#SRECV=3,1024\r", 1U, 1000U);
        receive_bytes(response, split);
        receive_bytes(response + split, sizeof(response) - 1U - split);
        uint16_t length;
        const uint8_t *actual = at_engine_get_response(&length);
        TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
        TEST_ASSERT_EQUAL_UINT16(sizeof(response) - 1U, length);
        TEST_ASSERT_EQUAL_MEMORY(response, actual, length);
    }
}
void test_at_urc_queued_after_done_survives_command_reset(void)
{
    static const char response[] = "\r\n#SRECV: 3,2\r\n"
        "AB\r\n\r\nOK\r\n\r\nSRING: 3,6\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    at_engine_reset();
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(1U, urc_calls);
    TEST_ASSERT_EQUAL_STRING("SRING: 3,6", last_urc);
    TEST_ASSERT_FALSE(at_engine_is_busy());
}
void test_at_response_overflow_stays_terminated_and_drains_stale_bytes(void)
{
    uint8_t excess[AT_ENGINE_RESPONSE_BUFFER_SIZE + 12U];
    memset(excess, 'A', sizeof(excess));
    start_command("AT\r", 1U, 1000U);
    receive_bytes(excess, sizeof(excess));
    uint16_t length;
    const uint8_t *actual = at_engine_get_response(&length);
    TEST_ASSERT_EQUAL_UINT16(AT_ENGINE_RESPONSE_BUFFER_SIZE - 1U, length);
    TEST_ASSERT_EQUAL_HEX8(0U, actual[length]);
    TEST_ASSERT_EQUAL_UINT32(0U, rbuff_available(&rx_ringbuf));
}
static void assert_srecv_payload(const char *header, const void *payload,
    size_t payload_len)
{
    static const char trailer[] = "\r\n\r\nOK\r\n";
    const size_t header_len = strlen(header);
    at_engine_reset();
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(header, header_len);
    receive_bytes(payload, payload_len);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    receive_bytes(trailer, sizeof(trailer) - 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    uint16_t length;
    const uint8_t *actual = at_engine_get_response(&length);
    TEST_ASSERT_EQUAL_UINT16(header_len + payload_len
        + sizeof(trailer) - 1U, length);
    TEST_ASSERT_EQUAL_MEMORY(header, actual, header_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, actual + header_len, payload_len);
    TEST_ASSERT_EQUAL_MEMORY(trailer, actual + header_len + payload_len,
        sizeof(trailer) - 1U);
}

void test_at_srecv_all_urc_and_cme_patterns_remain_payload(void)
{
    static const struct
    {
        const char *header;
        const char *payload;
    } cases[] = {
        {"\r\n#SRECV: 3,12\r\n", "SRING: 3,6\r\n"},
        {"\r\n#SRECV: 3,15\r\n", "NO CARRIER: 3\r\n"},
        {"\r\n#SRECV: 3,20\r\n", "#HTTPRING: 1,200,6\r\n"},
        {"\r\n#SRECV: 3,14\r\n", "#SL: ABORTED\r\n"},
        {"\r\n#SRECV: 3,15\r\n", "+CGEV: detach\r\n"},
        {"\r\n#SRECV: 3,17\r\n", "#NITZ: 26/10/05\r\n"},
        {"\r\n#SRECV: 3,15\r\n", "+CUSD: 0,test\r\n"},
        {"\r\n#SRECV: 3,16\r\n", "+CME ERROR: 10\r\n"},
        {"\r\n#SRECV: 3,16\r\n", "+CME ERROR: 42\r\n"}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        assert_srecv_payload(cases[i].header, cases[i].payload,
            strlen(cases[i].payload));
    }
}

void test_at_srecv_binary_urc_response_at_every_split_and_bytewise(void)
{
    static const char response[] = "\r\n#SRECV: 3,30\r\n"
        "\0SRING: 3,6\r\nNO CARRIER: 3\r\n\0\xFF"
        "\r\n\r\nOK\r\n";
    for (size_t split = 0U; split < sizeof(response); split++)
    {
        at_engine_reset();
        start_command("AT#SRECV=3,1024\r", 1U, 1000U);
        receive_bytes(response, split);
        receive_bytes(response + split, sizeof(response) - 1U - split);
        TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
        TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
        uint16_t length;
        const uint8_t *actual = at_engine_get_response(&length);
        TEST_ASSERT_EQUAL_UINT16(sizeof(response) - 1U, length);
        TEST_ASSERT_EQUAL_MEMORY(response, actual, length);
    }
    at_engine_reset();
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    for (size_t i = 0U; i < sizeof(response) - 1U; i++)
    {
        receive_bytes(response + i, 1U);
        TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
        if (i < sizeof(response) - 2U)
        {
            TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
        }
    }
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}

void test_at_srecv_payload_header_pattern_does_not_rearm_binary_mode(void)
{
    assert_srecv_payload("\r\n#SRECV: 3,11\r\n", "#SRECV: 3,6", 11U);
}

void test_at_srecv_partial_urc_keyword_cannot_cross_payload_boundary(void)
{
    static const char response[] = "\r\n#SRECV: 3,1\r\nS"
        "RING: 3,6\r\n\r\nOK\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_MEMORY(response, at_engine_get_response(NULL),
        sizeof(response) - 1U);
}

void test_at_real_urc_before_srecv_header_preserves_payload(void)
{
    static const char urc[] = "\r\nSRING: 1,2\r\n";
    static const char response[] = "\r\n#SRECV: 3,2\r\nAB\r\n\r\nOK\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(urc, sizeof(urc) - 1U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, urc_calls);
    TEST_ASSERT_EQUAL_STRING("SRING: 1,2", last_urc);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_MEMORY(response, at_engine_get_response(NULL),
        sizeof(response) - 1U);
}

void test_at_real_urc_after_payload_keeps_payload_crlf_and_nul(void)
{
    static const char prefix[] = "\r\n#SRECV: 3,3\r\n\0\r\n";
    static const char urcs[] = "SRING: 1,2\r\nNO CARRIER: 3\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(prefix, sizeof(prefix) - 1U);
    receive_bytes(urcs, sizeof(urcs) - 1U);
    TEST_ASSERT_EQUAL_UINT32(2U, urc_calls);
    TEST_ASSERT_EQUAL_STRING("NO CARRIER: 3", last_urc);
    receive_bytes("\r\n\r\nOK\r\n", 8U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    uint16_t length;
    const uint8_t *actual = at_engine_get_response(&length);
    TEST_ASSERT_EQUAL_UINT16(sizeof(prefix) - 1U + 8U, length);
    TEST_ASSERT_EQUAL_MEMORY(prefix, actual, sizeof(prefix) - 1U);
}

void test_at_incomplete_real_urc_after_payload_waits_for_line_end(void)
{
    static const char prefix[] = "\r\n#SRECV: 3,1\r\n\0";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(prefix, sizeof(prefix) - 1U);
    receive_bytes("\r\nSRING: 3,6\r", 13U);
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    receive_bytes("\n", 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, urc_calls);
    receive_bytes("\r\n\r\nOK\r\n", 8U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_MEMORY(prefix, at_engine_get_response(NULL),
        sizeof(prefix) - 1U);
}

static void receive_partial_srecv(void)
{
    static const char response[] = "\r\n#SRECV: 3,1024\r\nAB";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
}

static void queue_real_urc(void)
{
    static const char urc[] = "\r\nSRING: 3,6\r\n";
    for (size_t i = 0U; i < sizeof(urc) - 1U; i++)
    {
        at_engine_rx_byte((uint8_t)urc[i]);
    }
}

static void assert_pending_urc_processed(void)
{
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(1U, urc_calls);
    TEST_ASSERT_EQUAL_STRING("SRING: 3,6", last_urc);
    TEST_ASSERT_FALSE(at_engine_is_busy());
}

void test_at_reset_after_incomplete_srecv_preserves_pending_real_urc(void)
{
    receive_partial_srecv();
    queue_real_urc();
    at_engine_reset();
    assert_pending_urc_processed();
}

void test_at_reset_after_srecv_timeout_accepts_real_urc(void)
{
    receive_partial_srecv();
    now += 1000U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
    queue_real_urc();
    at_engine_reset();
    assert_pending_urc_processed();
}

void test_at_clear_buffer_ends_binary_window_but_preserves_rx_ring(void)
{
    receive_partial_srecv();
    queue_real_urc();
    at_engine_clear_buff();
    uint16_t length;
    TEST_ASSERT_EQUAL_HEX8(0U, at_engine_get_response(&length)[0]);
    TEST_ASSERT_EQUAL_UINT16(0U, length);
    receive_bytes("\r\nOK\r\n", 6U);
    TEST_ASSERT_EQUAL_UINT32(1U, urc_calls);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
}

void test_at_cancel_drops_pending_bytes_and_accepts_next_real_urc(void)
{
    receive_partial_srecv();
    queue_real_urc();
    at_engine_cancel();
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    TEST_ASSERT_FALSE(at_engine_is_busy());
    queue_real_urc();
    assert_pending_urc_processed();
}

void test_at_srecv_reset_and_retry_allow_next_normal_response(void)
{
    receive_partial_srecv();
    at_engine_reset();
    start_command("AT+CSQ\r", 1U, 1000U);
    receive_bytes("\r\nOK\r\n", 6U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    at_engine_reset();
    start_command("AT#SRECV=3,1024\r", 2U, 1000U);
    receive_bytes("\r\n#SRECV: 3,1024\r\n", 18U);
    now += 1000U;
    (void)at_engine_process();
    (void)at_engine_process();
    at_engine_dma_tx_complete_callback();
    receive_bytes("\r\n#SRECV: 3,1\r\nA\r\n\r\nOK\r\n", 24U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
}

void test_at_srecv_zero_and_maximum_requested_payload(void)
{
    static const char empty[] = "\r\n#SRECV: 3,0\r\n\r\n\r\nOK\r\n";
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(empty, sizeof(empty) - 1U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    uint8_t payload[1024];
    memset(payload, 0xFF, sizeof(payload));
    memcpy(payload, "NO CARRIER: 3\r\n", 15U);
    payload[100U] = 0U;
    memcpy(payload + sizeof(payload) - 12U, "SRING: 3,6\r\n", 12U);
    assert_srecv_payload("\r\n#SRECV: 3,1024\r\n", payload, sizeof(payload));
}

void test_at_srecv_real_trailer_errors_after_binary_nul_are_detected(void)
{
    static const char prefix[] = "\r\n#SRECV: 3,3\r\n\0OK";
    static const struct
    {
        const char *trailer;
        at_engine_result_t result;
    } cases[] = {
        {"\r\nERROR\r\n", AT_ENGINE_RESULT_ERROR},
        {"\r\n+CME ERROR: 10\r\n", AT_ENGINE_RESULT_NO_SIM},
        {"\r\n+CME ERROR: 42\r\n", AT_ENGINE_RESULT_ERROR}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        at_engine_reset();
        start_command("AT#SRECV=3,1024\r", 1U, 1000U);
        receive_bytes(prefix, sizeof(prefix) - 1U);
        receive_bytes(cases[i].trailer, strlen(cases[i].trailer));
        TEST_ASSERT_EQUAL(cases[i].result, at_engine_get_result());
        TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
        TEST_ASSERT_EQUAL_MEMORY(prefix, at_engine_get_response(NULL),
            sizeof(prefix) - 1U);
    }
}

void test_at_srecv_overflow_mid_payload_degrades_to_timeout_and_recovers(void)
{
    /* A late header plus a 1024-byte payload no longer fits the response
     * buffer once junk echo text has filled most of it. The command must
     * degrade to a clean timeout with the binary window still closed for
     * URC dispatch, and the engine must serve the next command normally. */
    uint8_t filler[600];
    uint8_t payload[1024];
    memset(filler, 'A', sizeof(filler));
    memset(payload, 0xFF, sizeof(payload));
    start_command("AT#SRECV=3,1024\r", 1U, 1000U);
    receive_bytes(filler, sizeof(filler));
    receive_bytes("\r\n#SRECV: 3,1024\r\n", 18U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    receive_bytes(payload, sizeof(payload));
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_NONE, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    uint16_t length;
    (void)at_engine_get_response(&length);
    TEST_ASSERT_EQUAL_UINT16(AT_ENGINE_RESPONSE_BUFFER_SIZE - 1U, length);
    now += 1000U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_TIMEOUT, at_engine_get_result());
    at_engine_reset();
    start_command("AT\r", 1U, 1000U);
    receive_bytes("\r\nOK\r\n", 6U);
    TEST_ASSERT_EQUAL(AT_ENGINE_RESULT_OK, at_engine_get_result());
    TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
}

/*** end of file ***/
