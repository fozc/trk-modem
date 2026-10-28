/*
 * File: test_firmware_update_scenario.c
 * Created on: Oct 28, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Production HTTP/RFWU handlers and shared writer; only platform boundaries
 * are doubled. Failed NOR writes may have partially changed the sector.
 */
#include "unity.h"
#include "gsm_firmware_update.h"
#include "http_handlers.h"
#include "raw_tcp_fw_update.h"
#include "hmac_sha256.h"
#include "efw_crc.h"
#include "rfwu_product_key.h"
#include "mock_w25qxx.h"
#include "mock_nvram.h"
#include "mock_boot.h"
#include "mock_app_ipc.h"
#include "mock_reboot.h"
#include "mock_elog.h"
#include "mock_bsp.h"
#include "mock_bsp_random.h"
#include "mock_http_response.h"
#include <string.h>

TEST_SOURCE_FILE("json.c")
TEST_SOURCE_FILE("sha256.c")

static uint8_t image[12288];
static uint8_t response[64];
static char json_response[1024];
static char tx[2048];
static rfwu_nvram_t record;
static uint32_t write_failures;
static uint32_t write_count;
static bool persist_failure;

static int write_flash(uint32_t address, const void *data, uint32_t length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_TRUE(address >= FIRMWARE_A_ADDRESS);
    const uint32_t offset = address - FIRMWARE_A_ADDRESS;
    TEST_ASSERT_TRUE(offset + length <= sizeof(image));
    write_count++;
    if (write_failures != 0U)
    {
        write_failures--;
        memcpy(image + offset, data, length / 2U);
        return W25QXX_RES_WRITE_FAIL;
    }
    memcpy(image + offset, data, length);
    return W25QXX_RES_OK;
}

static const rfwu_nvram_t *get_record(int call_count)
{
    (void)call_count;
    return &record;
}

static bool save_record(const rfwu_nvram_t *value, int call_count)
{
    (void)call_count;
    if (persist_failure)
    {
        return false;
    }
    record = *value;
    return true;
}

static bool random_word(uint32_t *word, int call_count)
{
    *word = (uint32_t)call_count + 1U;
    return true;
}

static int capture(const void *data, int length)
{
    TEST_ASSERT_TRUE((length > 0) && ((size_t)length <= sizeof(response)));
    memcpy(response, data, (size_t)length);
    return length;
}

static void capture_json(const char *data, int length, int call_count)
{
    (void)call_count;
    TEST_ASSERT_TRUE((length >= 0) && ((size_t)length < sizeof(json_response)));
    memcpy(json_response, data, (size_t)length);
    json_response[length] = '\0';
}

static void put_u32(uint8_t *data, uint32_t value)
{
    for (size_t index = 0U; index < 4U; index++)
    {
        data[index] = (uint8_t)(value >> (index * 8U));
    }
}

static void send_packet(rfwu_cmd_t command, const uint8_t *payload, uint16_t length)
{
    uint8_t frame[RFWU_MAX_PACKET_SIZE] = {0};
    put_u32(frame, RFWU_MAGIC);
    frame[4] = (uint8_t)command;
    frame[6] = (uint8_t)length;
    frame[7] = (uint8_t)(length >> 8U);
    if (length != 0U)
    {
        memcpy(frame + RFWU_HEADER_SIZE, payload, length);
    }
    efw_crc_t crc = efw_crc_init();
    crc = efw_crc_update(crc, frame, RFWU_HEADER_SIZE + length);
    put_u32(frame + RFWU_HEADER_SIZE + length, (uint32_t)efw_crc_finalize(crc));
    rfwu_on_receive(frame, (int)(RFWU_HEADER_SIZE + length + RFWU_CRC_SIZE));
}

static void hello(uint32_t size)
{
    uint8_t payload[RFWU_HELLO_SIZE] = {0};
    uint8_t message[5U + RFWU_NONCE_SIZE + 8U];
    uint8_t tag[32];
    const uint8_t key[RFWU_KEY_SIZE] = RFWU_PRODUCT_KEY_BYTES;
    send_packet(RFWU_CMD_CHALLENGE, NULL, 0U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_CHALLENGE, response[4]);
    put_u32(payload, size);
    put_u32(payload + 4U, 1234U);
    memcpy(message, "RFWU2", 5U);
    memcpy(message + 5U, response + RFWU_HEADER_SIZE, RFWU_NONCE_SIZE);
    memcpy(message + 5U + RFWU_NONCE_SIZE, payload, 8U);
    hmac_sha256(tag, key, sizeof(key), message, sizeof(message));
    memcpy(payload + 8U, tag, RFWU_TAG_SIZE);
    send_packet(RFWU_CMD_HELLO, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
}

static void send_data(uint32_t offset, uint8_t value, uint16_t length)
{
    uint8_t payload[RFWU_MAX_DATA_LEN];
    put_u32(payload, offset);
    memset(payload + 4U, value, length);
    send_packet(RFWU_CMD_DATA, payload, (uint16_t)(length + 4U));
}

void setUp(void)
{
    memset(image, 0xFF, sizeof(image));
    memset(&record, 0, sizeof(record));
    write_failures = 0U;
    write_count = 0U;
    persist_failure = false;
    boot_get_download_address_IgnoreAndReturn(FIRMWARE_A_ADDRESS);
    w25qxx_write_buff_StubWithCallback(write_flash);
    nvram_get_rfwu_StubWithCallback(get_record);
    nvram_set_rfwu_StubWithCallback(save_record);
    bsp_random_secure_word_StubWithCallback(random_word);
    bsp_get_tick_IgnoreAndReturn(0U);
    http_send_json_StubWithCallback(capture_json);
    http_send_error_Ignore();
    http_send_bad_request_Ignore();
    elog_log_fw_update_Ignore();
    app_ipc_read_download_image_id_IgnoreAndReturn(-1);
    http_handlers_init(tx, sizeof(tx));
    fw_update_reset();
    rfwu_init(capture);
    gsm_firmware_update_init();
    gsm_firmware_update_rfwu_init();
}

void tearDown(void)
{
}

void test_rfwu_retry_after_partial_nor_failure_preserves_exact_image(void)
{
    hello(8192U);
    for (uint32_t offset = 0U; offset < 3072U; offset += 1024U)
    {
        send_data(offset, 0xA1U, 1024U);
        TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    }
    write_failures = 1U;
    send_data(3072U, 0xA1U, 1024U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_NACK, response[4]);
    send_data(3072U, 0xA1U, 1024U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    for (uint32_t offset = 4096U; offset < 8192U; offset += 1024U)
    {
        send_data(offset, 0xB2U, 1024U);
        TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    }
    uint8_t size[4];
    put_u32(size, 8192U);
    send_packet(RFWU_CMD_FINISH, size, sizeof(size));
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    for (size_t index = 0U; index < sizeof(image); index++)
    {
        const uint8_t expected = index < 4096U ? 0xA1U : index < 8192U ? 0xB2U : 0xFFU;
        TEST_ASSERT_EQUAL_UINT8(expected, image[index]);
    }
}

void test_http_apply_before_finish_does_not_arm_update_or_reset(void)
{
    uint8_t data[16] = {1U};
    handle_fw_start("{\"s\":16,\"c\":1,\"h\":1}");
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    handle_fw_apply();
    TEST_ASSERT_EQUAL_UINT32(0U, write_count);
    TEST_ASSERT_NOT_NULL(strstr(json_response, "no firmware"));
}

void test_http_failed_finish_cannot_apply_and_fresh_transfer_recovers(void)
{
    uint8_t data[16] = {1U};
    handle_fw_start("{\"s\":16,\"c\":1,\"h\":1}");
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    write_failures = 1U;
    handle_fw_finish(NULL);
    TEST_ASSERT_NOT_NULL(strstr(json_response, "verify fail"));
    handle_fw_apply();
    TEST_ASSERT_NOT_NULL(strstr(json_response, "no firmware"));
    handle_fw_start("{\"s\":16,\"c\":1,\"h\":1}");
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    handle_fw_finish(NULL);
    app_ipc_request_update_ExpectAndReturn(0U, 0U, false, APP_IPC_OK);
    reboot_system_delayed_Expect(10000U);
    handle_fw_apply();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, image, sizeof(data));
}

void test_rfwu_hello_invalidates_completed_http_image(void)
{
    uint8_t data[16] = {1U};
    handle_fw_start("{\"s\":16,\"c\":1,\"h\":1}");
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    handle_fw_finish(NULL);
    hello(8192U);
    handle_fw_apply();
    TEST_ASSERT_NOT_NULL(strstr(json_response, "no firmware"));
}

void test_transport_switch_invalidates_http_resume_and_rfwu_session(void)
{
    uint8_t data[4096];
    memset(data, 0xA1, sizeof(data));
    handle_fw_start("{\"s\":8192,\"c\":2,\"h\":1}");
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    hello(8192U);
    handle_fw_chunk(4096U, data, sizeof(data), false, 0U);
    TEST_ASSERT_NOT_NULL(strstr(json_response, "no update"));
    handle_fw_start("{\"s\":8192,\"c\":2,\"h\":1}");
    TEST_ASSERT_NOT_NULL(strstr(json_response, "\"received\":0"));
    send_data(0U, 0xB2U, 1024U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_NACK, response[4]);
    TEST_ASSERT_EQUAL_UINT8(RFWU_ERR_NO_SESSION, response[RFWU_HEADER_SIZE]);
    TEST_ASSERT_EQUAL_UINT32(0U, record.magic);
    handle_fw_chunk(0U, data, sizeof(data), false, 0U);
    memset(data, 0xB2, sizeof(data));
    handle_fw_chunk(4096U, data, sizeof(data), false, 0U);
    handle_fw_finish(NULL);
    TEST_ASSERT_EQUAL_UINT8(0xA1U, image[0]);
    TEST_ASSERT_EQUAL_UINT8(0xB2U, image[4096]);
}

void test_http_start_does_not_take_writer_when_rfwu_cancel_cannot_be_persisted(void)
{
    hello(8192U);
    persist_failure = true;
    handle_fw_start("{\"s\":16,\"c\":1,\"h\":1}");
    TEST_ASSERT_NOT_NULL(strstr(json_response, "flash init"));
    persist_failure = false;
    send_data(0U, 0xA1U, 1024U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
}

void test_http_non_aligned_chunks_cross_two_sectors_without_changing_the_image(void)
{
    uint8_t data[4608];
    handle_fw_start("{\"s\":9000,\"c\":3,\"h\":1}");
    memset(data, 0xA1, sizeof(data));
    handle_fw_chunk(0U, data, 4090U, false, 0U);
    memset(data, 0xB2, sizeof(data));
    handle_fw_chunk(4090U, data, sizeof(data), false, 0U);
    memset(data, 0xC3, sizeof(data));
    handle_fw_chunk(8698U, data, 302U, false, 0U);
    handle_fw_finish(NULL);
    for (size_t index = 0U; index < sizeof(image); index++)
    {
        const uint8_t expected = index < 4090U ? 0xA1U : index < 8698U ? 0xB2U :
                                 index < 9000U ? 0xC3U : 0xFFU;
        TEST_ASSERT_EQUAL_UINT8(expected, image[index]);
    }
}

void test_rfwu_partial_sector_finish_can_retry_after_a_partial_nor_failure(void)
{
    hello(1023U);
    send_data(0U, 0xA1U, 1023U);
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    uint8_t size[4];
    put_u32(size, 1023U);
    write_failures = 1U;
    send_packet(RFWU_CMD_FINISH, size, sizeof(size));
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_NACK, response[4]);
    TEST_ASSERT_EQUAL_UINT32(0U, record.received_bytes);
    send_packet(RFWU_CMD_FINISH, size, sizeof(size));
    TEST_ASSERT_EQUAL_UINT8(RFWU_RESP_ACK, response[4]);
    TEST_ASSERT_EQUAL_UINT32(1023U, record.received_bytes);
    for (size_t index = 0U; index < 1023U; index++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xA1U, image[index]);
    }
    TEST_ASSERT_EQUAL_UINT8(0xFFU, image[1023]);
}

/*** end of file ***/
