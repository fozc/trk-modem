/*
 * test_spi_timeout.c
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "unity.h"
#include "main.h"
#include "../../Application/bsp/spi.c"

test_spi_t test_spi;
static uint32_t tick, tick_step, tx_busy, rx_busy, tx_polls, rx_polls;
static uint32_t writes, reads;
static uint8_t sent, received;
static uint8_t selected;

uint32_t HAL_GetTick(void)
{
    const uint32_t now = tick;
    tick += tick_step;
    return now;
}
uint32_t LL_SPI_IsActiveFlag_TXP(test_spi_t *spi)
{
    TEST_ASSERT_EQUAL_PTR(SPI2, spi);
    tx_polls++;
    if (0U != tx_busy)
    {
        tx_busy--;
        return 0U;
    }
    return 1U;
}
uint32_t LL_SPI_IsActiveFlag_RXP(test_spi_t *spi)
{
    TEST_ASSERT_EQUAL_PTR(SPI2, spi);
    rx_polls++;
    if (0U != rx_busy)
    {
        rx_busy--;
        return 0U;
    }
    return 1U;
}
void LL_SPI_TransmitData8(test_spi_t *spi, uint8_t data)
{
    TEST_ASSERT_EQUAL_PTR(SPI2, spi);
    TEST_ASSERT_EQUAL_UINT32(0U, tx_busy);
    writes++;
    sent = data;
}
uint8_t LL_SPI_ReceiveData8(test_spi_t *spi)
{
    TEST_ASSERT_EQUAL_PTR(SPI2, spi);
    TEST_ASSERT_EQUAL_UINT32(0U, rx_busy);
    reads++;
    return received;
}
void LL_GPIO_SetOutputPin(void *port, uint32_t pin)
{
    (void)port; (void)pin; selected = 0U;
}
void LL_GPIO_ResetOutputPin(void *port, uint32_t pin)
{
    (void)port; (void)pin; selected = 1U;
}
void setUp(void)
{
    tick = 0U; tick_step = 0U; tx_busy = 0U; rx_busy = 0U;
    tx_polls = 0U; rx_polls = 0U; writes = 0U; reads = 0U;
    received = 0xA5U; sent = 0U; selected = 0U;
    spi_clear_transfer_error();
    spi_cs_low();
}
void tearDown(void)
{
}

void test_ready_transfer_preserves_byte_and_status(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xA5U, spi_send_byte(0x5AU));
    TEST_ASSERT_EQUAL_HEX8(0x5AU, sent);
    TEST_ASSERT_EQUAL(SPI_TRANSFER_OK, spi_get_transfer_status());
    TEST_ASSERT_EQUAL_UINT8(1U, selected);
}
void test_delayed_flags_and_dummy_read_work(void)
{
    tx_busy = 3U; rx_busy = 4U;
    TEST_ASSERT_EQUAL_HEX8(0xA5U, spi_read_byte());
    TEST_ASSERT_EQUAL_HEX8(0xFFU, sent);
    TEST_ASSERT_EQUAL_UINT32(4U, tx_polls);
    TEST_ASSERT_EQUAL_UINT32(5U, rx_polls);
}
void test_tx_timeout_deselects_without_transmitting(void)
{
    tx_busy = UINT32_MAX; tick_step = 1U;
    TEST_ASSERT_EQUAL_HEX8(0xFFU, spi_send_byte(0x5AU));
    TEST_ASSERT_EQUAL(SPI_TRANSFER_TIMEOUT, spi_get_transfer_status());
    TEST_ASSERT_EQUAL_UINT32(0U, writes);
    TEST_ASSERT_EQUAL_UINT32(0U, reads);
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
    TEST_ASSERT_EQUAL_UINT32(10U, tx_polls);
}
void test_rx_timeout_does_not_read_stale_data(void)
{
    rx_busy = UINT32_MAX; tick_step = 1U;
    TEST_ASSERT_EQUAL_HEX8(0xFFU, spi_send_byte(0x5AU));
    TEST_ASSERT_EQUAL(SPI_TRANSFER_TIMEOUT, spi_get_transfer_status());
    TEST_ASSERT_EQUAL_UINT32(1U, writes);
    TEST_ASSERT_EQUAL_UINT32(0U, reads);
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
}
void test_tick_wrap_preserves_timeout(void)
{
    tick = UINT32_MAX - 4U; tick_step = 1U; tx_busy = UINT32_MAX;
    (void)spi_send_byte(0U);
    TEST_ASSERT_EQUAL(SPI_TRANSFER_TIMEOUT, spi_get_transfer_status());
    TEST_ASSERT_EQUAL_UINT32(10U, tx_polls);
}
void test_stopped_tick_still_bounds_polling(void)
{
    tx_busy = UINT32_MAX;
    (void)spi_send_byte(0U);
    TEST_ASSERT_EQUAL(SPI_TRANSFER_TIMEOUT, spi_get_transfer_status());
    TEST_ASSERT_EQUAL_UINT32(100000U, tx_polls);
}
void test_fault_remains_latched_across_transactions(void)
{
    tx_busy = UINT32_MAX; tick_step = 1U;
    (void)spi_send_byte(0U);
    const uint32_t previous_polls = tx_polls;
    tx_busy = 0U;
    spi_cs_high(); spi_cs_low();
    (void)spi_read_byte();
    TEST_ASSERT_EQUAL_UINT8(0U, selected);
    TEST_ASSERT_EQUAL_UINT32(previous_polls, tx_polls);
    TEST_ASSERT_EQUAL_UINT32(0U, writes);
    spi_clear_transfer_error(); spi_cs_low();
    TEST_ASSERT_EQUAL_HEX8(0xA5U, spi_read_byte());
    TEST_ASSERT_EQUAL(SPI_TRANSFER_OK, spi_get_transfer_status());
}
void test_valid_ff_payload_is_not_an_error(void)
{
    received = 0xFFU;
    TEST_ASSERT_EQUAL_HEX8(0xFFU, spi_read_byte());
    TEST_ASSERT_EQUAL(SPI_TRANSFER_OK, spi_get_transfer_status());
}
/*** end of file ***/
