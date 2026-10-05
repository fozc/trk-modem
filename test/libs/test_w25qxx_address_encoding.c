/*
 * test_w25qxx_address_encoding.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Check production flash driver SPI command and address bytes.
 */
#include "unity.h"
#include "mock_spi.h"
#include "mock_bsp.h"
#include <string.h>

#include "../../Application/libs/w25qxx.c"

static spi_transfer_status_t fake_transfer_status;

static spi_transfer_status_t transfer_status_callback(int call_count)
{
    (void)call_count;
    return fake_transfer_status;
}

static uint8_t fail_program_command(uint8_t data, int call_count)
{
    (void)call_count;
    if (0x02U == data)
    {
        fake_transfer_status = SPI_TRANSFER_TIMEOUT;
    }
    return 0xFFU;
}

void setUp(void)
{
    bsp_kick_wdt_Ignore();
    spi_get_transfer_status_IgnoreAndReturn(SPI_TRANSFER_OK);
    fake_transfer_status = SPI_TRANSFER_OK;
}

void tearDown(void)
{
}

static void expect_address(uint8_t command, uint8_t high,
                           uint8_t middle, uint8_t low)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(command, 0U);
    spi_send_byte_ExpectAndReturn(high, 0U);
    spi_send_byte_ExpectAndReturn(middle, 0U);
    spi_send_byte_ExpectAndReturn(low, 0U);
}

static void expect_status(uint8_t value)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x05U, 0U);
    spi_read_byte_ExpectAndReturn(value);
    spi_cs_high_Expect();
}

static void expect_write_enable(void)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x06U, 0U);
    spi_cs_high_Expect();
}

void test_read_byte_preserves_address_byte_order_and_boundaries(void)
{
    static const struct
    {
        uint32_t address;
        uint8_t high;
        uint8_t middle;
        uint8_t low;
    } cases[] =
    {
        {0x000000U, 0x00U, 0x00U, 0x00U},
        {0x0000FFU, 0x00U, 0x00U, 0xFFU},
        {0x000100U, 0x00U, 0x01U, 0x00U},
        {0x123456U, 0x12U, 0x34U, 0x56U},
        {0x3FFFFFU, 0x3FU, 0xFFU, 0xFFU}
    };

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        expect_address(0x03U, cases[i].high, cases[i].middle, cases[i].low);
        spi_read_byte_ExpectAndReturn(0xA5U);
        spi_cs_high_Expect();
        TEST_ASSERT_EQUAL_HEX8(0xA5U, w25qxx_read_byte(cases[i].address));
    }
}

void test_fast_read_sends_dummy_byte_and_returns_payload(void)
{
    uint8_t buffer[2] = {0U};
    expect_address(0x0BU, 0x12U, 0x34U, 0x56U);
    spi_send_byte_ExpectAndReturn(0x00U, 0U);
    spi_read_byte_ExpectAndReturn(0xA5U);
    spi_read_byte_ExpectAndReturn(0x5AU);
    spi_cs_high_Expect();
    w25qxx_read_buff(0x123456U, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_HEX8(0xA5U, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5AU, buffer[1]);
}

void test_write_byte_preserves_address_and_payload(void)
{
    expect_write_enable();
    expect_status(0x02U);
    expect_address(0x02U, 0x12U, 0x34U, 0x56U);
    spi_send_byte_ExpectAndReturn(0xA5U, 0U);
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_write_byte(0x123456U, 0xA5U));
}

void test_page_write_preserves_partial_page_offset(void)
{
    const uint8_t data = 0xA5U;
    expect_write_enable();
    expect_status(0x02U);
    expect_address(0x02U, 0x12U, 0x34U, 0x56U);
    spi_send_byte_ExpectAndReturn(data, 0U);
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_page_write(0x123456U, &data, 1U));
}

void test_full_page_write_sends_zero_low_address_byte(void)
{
    const uint8_t data[W25QXX_PAGE_SIZE] = {0U};
    expect_write_enable();
    expect_status(0x02U);
    expect_address(0x02U, 0x12U, 0x34U, 0x00U);
    for (size_t i = 0U; i < sizeof(data); i++)
    {
        spi_send_byte_ExpectAndReturn(0x00U, 0U);
    }
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_page_write(0x123400U, data, sizeof(data)));
}

void test_erase_commands_preserve_sector_and_block_addresses(void)
{
    expect_write_enable();
    expect_status(0x02U);
    expect_address(0x20U, 0x12U, 0x30U, 0x00U);
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_erase_sector(0x123000U));

    expect_write_enable();
    expect_status(0x02U);
    expect_address(0x52U, 0x12U, 0x80U, 0x00U);
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_erase_block32(0x128000U));

    expect_write_enable();
    expect_status(0x02U);
    expect_address(0xD8U, 0x12U, 0x00U, 0x00U);
    spi_cs_high_Expect();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(0, w25qxx_erase_block64(0x120000U));
}
void test_verify_uses_fast_read_address_and_compares_payload(void)
{
    const uint8_t data[2] = {0xA5U, 0x5AU};
    expect_address(0x0BU, 0x12U, 0x34U, 0x56U);
    spi_send_byte_ExpectAndReturn(0x00U, 0U);
    spi_read_byte_ExpectAndReturn(0xA5U);
    spi_read_byte_ExpectAndReturn(0x5AU);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_INT(0, w25qxx_verify(0x123456U, data, sizeof(data)));
}

void test_device_id_retains_both_bytes(void)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x90U, 0U);
    spi_send_byte_ExpectAndReturn(0x00U, 0U);
    spi_send_byte_ExpectAndReturn(0x00U, 0U);
    spi_send_byte_ExpectAndReturn(0x00U, 0U);
    spi_read_byte_ExpectAndReturn(0xEFU);
    spi_read_byte_ExpectAndReturn(0xFFU);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_HEX16(0xFFEFU, w25qxx_read_manu_deviceid());
}

void test_jedec_id_reads_three_bytes_and_zeros_upper_byte(void)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x9FU, 0U);
    spi_read_byte_ExpectAndReturn(0xEFU);
    spi_read_byte_ExpectAndReturn(0x40U);
    spi_read_byte_ExpectAndReturn(0x16U);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_HEX32(0x001640EFU, w25qxx_read_jedecid());
}

void test_jedec_id_preserves_board_part_byte_order(void)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x9FU, 0U);
    spi_read_byte_ExpectAndReturn(0x1FU);
    spi_read_byte_ExpectAndReturn(0x87U);
    spi_read_byte_ExpectAndReturn(0x01U);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_HEX32(0x0001871FU, w25qxx_read_jedecid());
}

void test_jedec_id_preserves_high_bits_in_last_identity_byte(void)
{
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0x9FU, 0U);
    spi_read_byte_ExpectAndReturn(0xEFU);
    spi_read_byte_ExpectAndReturn(0x40U);
    spi_read_byte_ExpectAndReturn(0xF0U);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_HEX32(0x00F040EFU, w25qxx_read_jedecid());
}

void test_capacity_lookup_unknown_is_stable_and_empty(void)
{
    const flash_capacity_t *unknown = w25q_lookup_capacity(0xFFU);
    TEST_ASSERT_NOT_NULL(unknown);
    TEST_ASSERT_EQUAL_UINT32(0U, unknown->size_bytes);
    TEST_ASSERT_EQUAL_STRING("Unknown", unknown->size_str);
    TEST_ASSERT_EQUAL_PTR(unknown, w25q_lookup_capacity(0x00U));
    TEST_ASSERT_EQUAL_UINT32(1UL << 22, w25q_lookup_capacity(0x16U)->size_bytes);
}
void test_latched_spi_fault_rejects_flash_wait_without_polling(void)
{
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_wait_for_write_or_erase());
}

void test_status_read_timeout_cannot_be_accepted_as_idle(void)
{
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_OK);
    expect_status(0x00U);
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_wait_for_write_or_erase());
}

void test_write_enable_status_timeout_cannot_approve_programming(void)
{
    expect_write_enable();
    expect_status(0xFFU);
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_write_byte(0U, 0xFFU));
}

void test_program_transfer_timeout_is_returned_to_caller(void)
{
    const uint8_t data = 0xA5U;
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_Stub(transfer_status_callback);
    spi_cs_low_Ignore();
    spi_cs_high_Ignore();
    spi_read_byte_IgnoreAndReturn(0x02U);
    spi_send_byte_Stub(fail_program_command);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_page_write(0U, &data, 1U));
}

void test_verify_ff_payload_cannot_hide_transport_failure(void)
{
    const uint8_t data = 0xFFU;
    expect_address(0x0BU, 0U, 0U, 0U);
    spi_send_byte_ExpectAndReturn(0U, 0U);
    spi_read_byte_ExpectAndReturn(0xFFU);
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    spi_cs_high_Expect();
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_verify(0U, &data, 1U));
}
void test_sector_erase_without_wel_does_not_send_erase_command(void)
{
    expect_write_enable();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_WEL_NOT_SET,
        w25qxx_erase_sector(0x123000U));
}

void test_block32_erase_without_wel_does_not_send_erase_command(void)
{
    expect_write_enable();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_WEL_NOT_SET,
        w25qxx_erase_block32(0x128000U));
}

void test_block64_erase_without_wel_does_not_send_erase_command(void)
{
    expect_write_enable();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_WEL_NOT_SET,
        w25qxx_erase_block64(0x120000U));
}

void test_chip_erase_without_wel_does_not_send_erase_command(void)
{
    expect_write_enable();
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_WEL_NOT_SET, w25qxx_erase_chip());
}

void test_erase_status_timeout_takes_priority_over_wel_bit(void)
{
    expect_write_enable();
    expect_status(0xFFU);
    spi_get_transfer_status_StopIgnore();
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT,
        w25qxx_erase_sector(0x123000U));

    expect_write_enable();
    expect_status(0x00U);
    spi_get_transfer_status_ExpectAndReturn(SPI_TRANSFER_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_TIMEOUT, w25qxx_erase_chip());
}

void test_chip_erase_with_wel_preserves_command_and_busy_polling(void)
{
    expect_write_enable();
    expect_status(0x02U);
    spi_cs_low_Expect();
    spi_send_byte_ExpectAndReturn(0xC7U, 0U);
    spi_cs_high_Expect();
    expect_status(0x01U);
    expect_status(0x00U);
    TEST_ASSERT_EQUAL_INT(W25QXX_RES_OK, w25qxx_erase_chip());
}
/*** end of file ***/
