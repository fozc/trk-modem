/*
 * test_rf_discovery.c
 *
 *  Created on: Oct 11, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Verify discovery capacity, identity order and report refresh boundaries.
 */
#include "unity.h"
#include "rf_discovery.h"
#include <string.h>

static uint8_t identity[8];

void setUp(void)
{
    rf_discovery_reset();
    (void)memset(identity, 0, sizeof(identity));
    identity[7] = 1U;
}

void tearDown(void)
{
}

void test_discovery_add_and_get_preserve_all_identity_bytes(void)
{
    const uint8_t eui[8] = {0U, 0x12U, 0x4BU, 0U, 0x38U,
                            0xC9U, 0xF1U, 0xC3U};
    TEST_ASSERT_TRUE(rf_discovery_add(eui));
    uint8_t output[8] = {0};
    TEST_ASSERT_TRUE(rf_discovery_get_device(0U, output));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(eui, output, sizeof(output));
    rf_discovery_entry_t entry;
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_FALSE(entry.has_rssi);
    TEST_ASSERT_FALSE(rf_discovery_add(eui));
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
}

void test_full_queue_refreshes_existing_rssi_without_evicting_any_device(void)
{
    for (uint8_t index = 1U; RF_DISCOVERY_MAX >= index; index++)
    {
        identity[7] = index;
        TEST_ASSERT_TRUE(rf_discovery_add(identity));
    }
    identity[7] = RF_DISCOVERY_MAX + 1U;
    TEST_ASSERT_FALSE(rf_discovery_report(identity, -127));
    TEST_ASSERT_FALSE(rf_discovery_add(identity));
    identity[7] = 1U;
    TEST_ASSERT_TRUE(rf_discovery_report(identity, INT8_MIN));
    TEST_ASSERT_TRUE(rf_discovery_report(identity, INT8_MAX));
    rf_discovery_entry_t entry;
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_EQUAL_INT8(INT8_MAX, entry.rssi);
    TEST_ASSERT_TRUE(entry.has_rssi);
    TEST_ASSERT_EQUAL_UINT8(RF_DISCOVERY_MAX, rf_discovery_get_count());
}

void test_remove_first_middle_and_last_keeps_remaining_identity_order(void)
{
    for (uint8_t index = 1U; 5U >= index; index++)
    {
        identity[7] = index;
        TEST_ASSERT_TRUE(rf_discovery_report(identity, -55));
    }
    const uint8_t removed[] = {1U, 3U, 5U};
    for (size_t index = 0U; sizeof(removed) > index; index++)
    {
        identity[7] = removed[index];
        TEST_ASSERT_TRUE(rf_discovery_remove(identity));
        TEST_ASSERT_FALSE(rf_discovery_remove(identity));
    }
    TEST_ASSERT_EQUAL_UINT8(2U, rf_discovery_get_count());
    rf_discovery_entry_t entry;
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_EQUAL_UINT8(2U, entry.eui64[7]);
    TEST_ASSERT_TRUE(rf_discovery_get(1U, &entry));
    TEST_ASSERT_EQUAL_UINT8(4U, entry.eui64[7]);
    TEST_ASSERT_EQUAL_INT8(-55, entry.rssi);
}

void test_invalid_discovery_inputs_preserve_existing_queue_and_output(void)
{
    TEST_ASSERT_TRUE(rf_discovery_report(identity, -55));
    TEST_ASSERT_FALSE(rf_discovery_add(NULL));
    TEST_ASSERT_FALSE(rf_discovery_report(NULL, -55));
    TEST_ASSERT_FALSE(rf_discovery_remove(NULL));
    uint8_t output[8];
    (void)memset(output, 0xA5, sizeof(output));
    TEST_ASSERT_FALSE(rf_discovery_get_device(1U, output));
    TEST_ASSERT_FALSE(rf_discovery_get_device(0U, NULL));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5U, output, sizeof(output));
    rf_discovery_entry_t entry;
    (void)memset(&entry, 0xA5, sizeof(entry));
    const rf_discovery_entry_t before = entry;
    TEST_ASSERT_FALSE(rf_discovery_get(SIZE_MAX, &entry));
    TEST_ASSERT_FALSE(rf_discovery_get(0U, NULL));
    TEST_ASSERT_EQUAL_MEMORY(&before, &entry, sizeof(entry));
    TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
}

void test_every_rssi_value_refreshes_one_existing_identity(void)
{
    for (int16_t rssi = INT8_MIN; INT8_MAX >= rssi; rssi++)
    {
        TEST_ASSERT_TRUE(rf_discovery_report(identity, (int8_t)rssi));
        rf_discovery_entry_t entry;
        TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
        TEST_ASSERT_EQUAL_INT8((int8_t)rssi, entry.rssi);
        TEST_ASSERT_EQUAL_UINT8(1U, rf_discovery_get_count());
    }
}

void test_reset_after_full_queue_clears_entries_and_accepts_new_identity(void)
{
    for (uint8_t index = 1U; RF_DISCOVERY_MAX >= index; index++)
    {
        identity[7] = index;
        TEST_ASSERT_TRUE(rf_discovery_report(identity, -55));
    }
    rf_discovery_reset();
    TEST_ASSERT_EQUAL_UINT8(0U, rf_discovery_get_count());
    rf_discovery_entry_t entry;
    TEST_ASSERT_FALSE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_TRUE(rf_discovery_add(identity));
    TEST_ASSERT_TRUE(rf_discovery_get(0U, &entry));
    TEST_ASSERT_FALSE(entry.has_rssi);
}

/*** end of file ***/
