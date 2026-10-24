/*
 * test_gsm_receive_scenario.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "mock_bsp.h"
#include "mock_gsm_log.h"
#include "mock_uart.h"
#include "mock_gsm_socket.h"
#include "mock_gsm_info.h"
#include "mock_iec104_elog.h"
#include "mock_modem_config.h"
#include "mock_iec104_config.h"
#include "mock_rtc.h"
#include "mock_led_driver.h"
#include "mock_iec104_process.h"
#include "mock_gsm_http_server.h"
#include "mock_gsm_elog.h"
#include "xscanf.h"
#include "xprintf.h"
TEST_SOURCE_FILE("gsm_signal_led.c")
TEST_SOURCE_FILE("datetime.c")
TEST_SOURCE_FILE("gsm_sms.c")
#include "ring_buff.h"
#include <string.h>

#undef CCSLOG
#define CCSLOG(color, ...) ((void)(color))
#include "../../Application/gsm/at_engine2.c"
/* Legacy GSM helpers retain their existing host warning exceptions. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "../../Application/gsm/utils.c"
#include "../../Application/gsm/gsm_engine.c"
#pragma GCC diagnostic pop

UART_HandleTypeDef huart1;
static uint32_t now;
static uint32_t urc_calls;

static uint32_t get_tick(int call_count)
{
    (void)call_count;
    return now;
}

HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *handle,
    const uint8_t *data, uint16_t length)
{
    TEST_ASSERT_EQUAL_PTR(&huart1, handle);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_TRUE(length > 0U);
    return HAL_OK;
}

void gsm_URC_callback(uint8_t *message, uint16_t length)
{
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_TRUE(length > 0U);
    urc_calls++;
}

void gsm_wtd_liveness_ping(void)
{
}

void setUp(void)
{
    memset(&at_engine, 0, sizeof(at_engine));
    s_tx_done_flag = 0U;
    now = 100U;
    urc_calls = 0U;
    uart_abort_tx_IgnoreAndReturn(true);
    bsp_get_tick_Stub(get_tick);
    gsm_log_get_level_IgnoreAndReturn(GSM_LOG_OFF);
    at_engine_init();
    memset(&gsm, 0, sizeof(gsm));
    memset(&gsm_info, 0, sizeof(gsm_info));
    gsm_socket_set_state_Ignore();
}

void tearDown(void)
{
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

uint16_t iec104_get_t3_timeout(void)
{
    return 60U;
}
uint16_t iec104_get_listen_port(void)
{
    return 2404U;
}

static const uint8_t *expected_payload;
static uint16_t expected_length;
static uint32_t delivered;

static void receive_web(const uint8_t *data, uint16_t length, int count)
{
    (void)count;
    TEST_ASSERT_EQUAL_UINT16(expected_length, length);
    TEST_ASSERT_EQUAL_MEMORY(expected_payload, data, length);
    delivered++;
}

static void receive_iec(const uint8_t *data, uint16_t length,
                        void *user, int count)
{
    TEST_ASSERT_NULL(user);
    receive_web(data, length, count);
}

static void start_read(uint32_t query)
{
    TEST_ASSERT_EQUAL_UINT32(1U, gsm_engine_send_query(query));
    (void)at_engine_process();
    at_engine_dma_tx_complete_callback();
}

static void verify_read(uint32_t query, const char *header,
                        const uint8_t *payload, uint16_t length)
{
    expected_payload = payload;
    expected_length = length;
    delivered = 0U;
    gsm_http_server_client_data_received_Stub(receive_web);
    iec104_on_data_Stub(receive_iec);
    start_read(query);
    receive_bytes(header, strlen(header));
    receive_bytes(payload, length);
    receive_bytes("\r\n\r\nOK\r\n", 8U);
    gsm_info_add_rx_bytes_Expect(length);
    gsm_socket_touch_activity_Ignore();
    TEST_ASSERT_EQUAL_UINT32(GSM_RESPONSE_OK, gsm_engine_get_query_res());
    TEST_ASSERT_EQUAL_UINT32(1U, delivered);
    TEST_ASSERT_EQUAL_UINT32(length, gsm_info.rx_counter);
    TEST_ASSERT_NULL(at_engine_get_payload(NULL));
    gsm_set_free();
}

void test_web_and_iec_and_dialer_preserve_binary_control_text(void)
{
    static const uint8_t payload[] =
        "NO CARRIER\r\nOK\r\n\0\r\nERROR\r\n#QSS: 0\r\nSRING: 1,4\r\n";
    const uint32_t queries[] = {ATQUERY_GET_LISTENER_SOCKET_DATA,
        ATQUERY_GET_IEC104_LISTENER_SOCKET_DATA, ATQUERY_GET_DIALER_SOCKET_DATA};
    const char *headers[] = {"\r\n#SRECV: 1,47\r\n",
        "\r\n#SRECV: 3,47\r\n", "\r\n#SRECV: 2,47\r\n"};
    for (size_t index = 0U; index < 3U; index++)
    {
        memset(&gsm_info, 0, sizeof(gsm_info));
        verify_read(queries[index], headers[index], payload,
                    (uint16_t)(sizeof(payload) - 1U));
        TEST_ASSERT_EQUAL_UINT32(0U, urc_calls);
    }
}

void test_web_receive_1024_bytes_includes_all_byte_values(void)
{
    uint8_t payload[1024];
    for (size_t index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t)(index & 0xFFU);
    }
    verify_read(ATQUERY_GET_LISTENER_SOCKET_DATA,
        "\r\n#SRECV: 1,1024\r\n", payload, sizeof(payload));
}

void test_error_and_timeout_do_not_deliver_socket_data(void)
{
    start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
    receive_bytes("\r\nERROR\r\n", 9U);
    TEST_ASSERT_EQUAL_UINT32(GSM_ERROR, gsm_engine_get_query_res());
    gsm_set_free();
    start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
    now += 10000U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(GSM_TIMEOUT, gsm_engine_get_query_res());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

void test_web_binary_receive_all_transport_split_points(void)
{
    static const uint8_t payload[] = "NO CARRIER\0\r\nOK\r\n";
    static const uint8_t response[] =
        "\r\n#SRECV: 1,17\r\nNO CARRIER\0\r\nOK\r\n\r\n\r\nOK\r\n";
    for (size_t split = 0U; split < sizeof(response) - 1U; split++)
    {
        expected_payload = payload;
        expected_length = (uint16_t)(sizeof(payload) - 1U);
        delivered = 0U;
        gsm_info.rx_counter = 0U;
        gsm_http_server_client_data_received_Stub(receive_web);
        start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
        receive_bytes(response, split);
        TEST_ASSERT_EQUAL_UINT32(0U, gsm_engine_get_query_res());
        receive_bytes(response + split, sizeof(response) - 1U - split);
        gsm_info_add_rx_bytes_Expect(expected_length);
        gsm_socket_touch_activity_Ignore();
        TEST_ASSERT_EQUAL_UINT32(GSM_RESPONSE_OK, gsm_engine_get_query_res());
        TEST_ASSERT_EQUAL_UINT32(1U, delivered);
        gsm_set_free();
    }
}

void test_truncated_payload_never_delivers_partial_data(void)
{
    start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
    static const char partial[] = "\r\n#SRECV: 1,100\r\nabc\r\nOK\r\n";
    receive_bytes(partial, sizeof(partial) - 1U);
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_engine_get_query_res());
    TEST_ASSERT_NULL(at_engine_get_payload(NULL));
    now += 10000U;
    (void)at_engine_process();
    (void)at_engine_process();
    TEST_ASSERT_EQUAL_UINT32(GSM_TIMEOUT, gsm_engine_get_query_res());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

void test_missing_or_oversized_srecv_header_does_not_deliver_data(void)
{
    const char *responses[] = {"\r\nOK\r\n",
        "\r\n#SRECV: 1,65536\r\nabc\r\n\r\nOK\r\n"};
    for (size_t index = 0U; index < 2U; index++)
    {
        start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
        receive_bytes(responses[index], strlen(responses[index]));
        TEST_ASSERT_EQUAL_UINT32(GSM_ERROR, gsm_engine_get_query_res());
        TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
        gsm_set_free();
    }
}

void test_empty_socket_payload_is_success_without_delivery(void)
{
    static const char response[] = "\r\n#SRECV: 1,0\r\n\r\n\r\nOK\r\n";
    start_read(ATQUERY_GET_LISTENER_SOCKET_DATA);
    receive_bytes(response, sizeof(response) - 1U);
    TEST_ASSERT_EQUAL_UINT32(GSM_RESPONSE_OK, gsm_engine_get_query_res());
    TEST_ASSERT_EQUAL_UINT32(0U, gsm_info.rx_counter);
}

void test_all_socket_receivers_preserve_boundary_lengths(void)
{
    uint8_t payload[1024];
    memset(payload, 0xA5, sizeof(payload));
    const uint16_t lengths[] = {1U, 2U, 6U, 1023U, 1024U};
    const uint32_t queries[] = {ATQUERY_GET_LISTENER_SOCKET_DATA,
        ATQUERY_GET_IEC104_LISTENER_SOCKET_DATA, ATQUERY_GET_DIALER_SOCKET_DATA};
    const uint8_t sockets[] = {1U, 3U, 2U};
    for (size_t socket = 0U; socket < 3U; socket++)
    {
        for (size_t index = 0U; index < 5U; index++)
        {
            char header[32];
            (void)xsprintf(header, "\r\n#SRECV: %u,%u\r\n",
                           (unsigned int)sockets[socket],
                           (unsigned int)lengths[index]);
            gsm_info.rx_counter = 0U;
            verify_read(queries[socket], header, payload, lengths[index]);
        }
    }
}

/*** end of file ***/
