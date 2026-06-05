/*
 * iec104_test_comprehensive.c
 *
 * Comprehensive edge case and functional testing for IEC104 Event Queue
 * Tests: boundary conditions, error handling, circular buffer logic
 */

#include "iec104_event_queue.h"
#include "mock_flash.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test configuration */
#define TEST_SUPER_BLOCK_ADDR        0x08100000
#define TEST_SUPER_BLOCK_BACKUP_ADDR 0x08101000
#define TEST_DATA_BLOCK_ADDR         0x08102000
#define TEST_LOG_SIZE                (512 * 1024)

/* ANSI color codes for better visibility */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;
static int test_number = 0;

/* Helper macros */
#define TEST_START(name) \
	do { \
		test_number++; \
		printf("\n[TEST %02d] %s\n", test_number, name); \
	} while(0)

#define TEST_ASSERT(condition, message) \
	do { \
		if (condition) { \
			printf("  " COLOR_GREEN "[PASS]" COLOR_RESET " %s\n", message); \
			tests_passed++; \
		} else { \
			printf("  " COLOR_RED "[FAIL]" COLOR_RESET " %s\n", message); \
			tests_failed++; \
		} \
	} while(0)

#define TEST_SECTION(name) \
	printf("\n" COLOR_CYAN "======== %s ========" COLOR_RESET "\n", name)

/* Global queue instance */
static iec104_event_queue_cfg_t test_cfg;

/* Helper function to create a test event */
static iec104_fault_event_t create_test_event(uint32_t seed)
{
	iec104_fault_event_t event;
	memset(&event, 0, sizeof(event));
	
	/* Fill with test data based on seed */
	for (uint32_t i = 0; i < sizeof(event.power_states.data); i++) {
		event.power_states.data[i] = (uint8_t)((seed + i) & 0xFF);
	}
	event.power_states.length = 10 + (seed % 10);
	event.nominal_current.length = 15;
	event.fault_type.length = 8;
	event.fault_current.length = 12;
	event.fault_duration.length = 20;
	
	return event;
}

/* Helper function to initialize test environment */
static bool setup_test_queue(void)
{
	mock_flash_init();
	mock_flash_reset();
	
	test_cfg.io_if.read = mock_flash_read;
	test_cfg.io_if.write = mock_flash_write;
	test_cfg.super_block_addr = TEST_SUPER_BLOCK_ADDR;
	test_cfg.super_block_backup_addr = TEST_SUPER_BLOCK_BACKUP_ADDR;
	test_cfg.data_block_addr = TEST_DATA_BLOCK_ADDR;
	test_cfg.size = TEST_LOG_SIZE;
	
	return iec104_event_queue_init(&test_cfg);
}

/* ========== Test Cases ========== */

void test_init_with_null_config(void)
{
	TEST_START("Initialization with NULL config");
	
	mock_flash_reset();
	bool result = iec104_event_queue_init(NULL);
	
	TEST_ASSERT(result == false, "Should reject NULL config");
}

void test_init_with_invalid_io_interface(void)
{
	TEST_START("Initialization with invalid I/O interface");
	
	iec104_event_queue_cfg_t bad_cfg = {0};
	bad_cfg.io_if.read = NULL;
	bad_cfg.io_if.write = mock_flash_write;
	
	bool result = iec104_event_queue_init(&bad_cfg);
	TEST_ASSERT(result == false, "Should reject NULL read function");
	
	bad_cfg.io_if.read = mock_flash_read;
	bad_cfg.io_if.write = NULL;
	
	result = iec104_event_queue_init(&bad_cfg);
	TEST_ASSERT(result == false, "Should reject NULL write function");
}

void test_empty_queue_operations(void)
{
	TEST_START("Empty queue operations");
	
	setup_test_queue();
	
	TEST_ASSERT(iec104_event_queue_is_empty() == true, "New queue should be empty");
	TEST_ASSERT(iec104_event_queue_is_full() == false, "New queue should not be full");
	TEST_ASSERT(iec104_event_queue_get_count() == 0, "Count should be 0");
	
	iec104_fault_event_t event;
	iec104_queue_result_t result = iec104_event_queue_read(&event);
	TEST_ASSERT(result == IEC104_QUEUE_EMPTY, "Reading from empty queue should return EMPTY");
}

void test_add_single_event(void)
{
	TEST_START("Add single event to queue");
	
	setup_test_queue();
	
	iec104_fault_event_t event = create_test_event(42);
	iec104_queue_result_t result = iec104_event_queue_edd(&event);
	
	TEST_ASSERT(result == IEC104_QUEUE_OK, "Should add event successfully");
	TEST_ASSERT(iec104_event_queue_is_empty() == false, "Queue should not be empty");
	TEST_ASSERT(iec104_event_queue_get_count() == 1, "Count should be 1");
}

void test_add_and_read_event(void)
{
	TEST_START("Add and read event");
	
	setup_test_queue();
	
	iec104_fault_event_t event_out, event_in = create_test_event(123);
	
	iec104_event_queue_edd(&event_in);
	iec104_queue_result_t result = iec104_event_queue_read(&event_out);
	
	TEST_ASSERT(result == IEC104_QUEUE_OK, "Should read event successfully");
	TEST_ASSERT(iec104_event_queue_is_empty() == true, "Queue should be empty after read");
	TEST_ASSERT(event_out.power_states.length == event_in.power_states.length, 
	            "Event data should match");
}

void test_add_null_event(void)
{
	TEST_START("Add NULL event");
	
	setup_test_queue();
	
	iec104_queue_result_t result = iec104_event_queue_edd(NULL);
	TEST_ASSERT(result == IEC104_QUEUE_INVALID_PARAM, "Should reject NULL event");
}

void test_read_to_null_buffer(void)
{
	TEST_START("Read event to NULL buffer");
	
	setup_test_queue();
	
	iec104_fault_event_t event = create_test_event(1);
	iec104_event_queue_edd(&event);
	
	iec104_queue_result_t result = iec104_event_queue_read(NULL);
	TEST_ASSERT(result == IEC104_QUEUE_INVALID_PARAM, "Should reject NULL buffer");
}

void test_fill_queue_to_capacity(void)
{
	TEST_START("Fill queue to maximum capacity (100 events)");
	
	setup_test_queue();
	
	/* Add 99 events (capacity - 1 because of circular buffer) */
	for (int i = 0; i < 99; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_queue_result_t result = iec104_event_queue_edd(&event);
		
		if (result != IEC104_QUEUE_OK) {
			printf("  Failed at event %d\n", i);
			break;
		}
	}
	
	TEST_ASSERT(iec104_event_queue_get_count() == 99, "Should have 99 events");
	TEST_ASSERT(iec104_event_queue_is_full() == true, "Queue should be full");
}

void test_overflow_handling(void)
{
	TEST_START("Queue overflow handling (circular buffer wrap)");
	
	setup_test_queue();
	
	/* Fill queue to capacity */
	for (int i = 0; i < 99; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	/* Try to add one more - should overwrite oldest */
	iec104_fault_event_t overflow_event = create_test_event(999);
	iec104_queue_result_t result = iec104_event_queue_edd(&overflow_event);
	
	TEST_ASSERT(result == IEC104_QUEUE_OK, "Should handle overflow");
	TEST_ASSERT(iec104_event_queue_get_count() == 99, "Count should remain at max");
}

void test_circular_buffer_wraparound(void)
{
	TEST_START("Circular buffer wraparound behavior");
	
	setup_test_queue();
	
	/* Add 50 events */
	for (int i = 0; i < 50; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	/* Read 30 events */
	for (int i = 0; i < 30; i++) {
		iec104_fault_event_t event;
		iec104_event_queue_read(&event);
	}
	
	TEST_ASSERT(iec104_event_queue_get_count() == 20, "Should have 20 events remaining");
	
	/* Add 70 more events to cause wraparound */
	for (int i = 50; i < 120; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	TEST_ASSERT(iec104_event_queue_get_count() == 90, "Should have 90 events after wraparound");
}

void test_multiple_add_read_cycles(void)
{
	TEST_START("Multiple add/read cycles");
	
	setup_test_queue();
	
	for (int cycle = 0; cycle < 5; cycle++) {
		/* Add 20 events */
		for (int i = 0; i < 20; i++) {
			iec104_fault_event_t event = create_test_event(cycle * 100 + i);
			iec104_event_queue_edd(&event);
		}
		
		/* Read 20 events */
		for (int i = 0; i < 20; i++) {
			iec104_fault_event_t event;
			iec104_queue_result_t result = iec104_event_queue_read(&event);
			if (result != IEC104_QUEUE_OK) {
				printf("  Cycle %d failed at read %d\n", cycle, i);
			}
		}
	}
	
	TEST_ASSERT(iec104_event_queue_is_empty() == true, 
	            "Queue should be empty after balanced operations");
}

void test_flash_persistence(void)
{
	TEST_START("Flash persistence (save and load)");
	
	setup_test_queue();
	
	/* Add some events */
	for (int i = 0; i < 10; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	/* Save to flash */
	iec104_queue_result_t save_result = iec104_event_queue_save_to_flash();
	TEST_ASSERT(save_result == IEC104_QUEUE_OK, "Should save to flash successfully");
	
	/* Load from flash */
	iec104_queue_result_t load_result = iec104_event_queue_load_from_flash();
	TEST_ASSERT(load_result == IEC104_QUEUE_OK, "Should load from flash successfully");
	
	TEST_ASSERT(iec104_event_queue_get_count() == 10, 
	            "Should restore correct event count");
}

void test_sector_boundary_crossing(void)
{
	TEST_START("Event storage across sector boundaries");
	
	setup_test_queue();
	
	/* Calculate events per sector */
	uint32_t events_per_sector = IEC104_QUEUE_SECTOR_EVENT_COUNT;
	
	printf("  Events per sector: %u\n", events_per_sector);
	
	/* Add events up to boundary */
	for (uint32_t i = 0; i < events_per_sector + 2; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	TEST_ASSERT(iec104_event_queue_get_count() == events_per_sector + 2,
	            "Should handle sector boundary crossing");
}

void test_unsent_event_tracking(void)
{
	TEST_START("Unsent event tracking");
	
	setup_test_queue();
	
	/* Add events (all start as unsent) */
	for (int i = 0; i < 5; i++) {
		iec104_fault_event_t event = create_test_event(i);
		iec104_event_queue_edd(&event);
	}
	
	uint8_t unsent_count = iec104_event_queue_get_unsent_count();
	TEST_ASSERT(unsent_count == 5, "Should track 5 unsent events");
}

void test_structure_sizes(void)
{
	TEST_START("Structure size validation");
	
	uint32_t event_size = sizeof(iec104_fault_event_t);
	uint32_t sector_size = sizeof(iec104_event_sector_t);
	uint32_t queue_size = sizeof(iec104_event_queue_t);
	uint32_t superblock_size = sizeof(iec104_event_queue_superblock_t);
	
	printf("  iec104_fault_event_t: %u bytes\n", event_size);
	printf("  iec104_event_sector_t: %u bytes\n", sector_size);
	printf("  iec104_event_queue_t: %u bytes\n", queue_size);
	printf("  iec104_event_queue_superblock_t: %u bytes\n", superblock_size);
	
	TEST_ASSERT(sector_size == IEC104_QUEUE_FLASH_SECTOR_SIZE, 
	            "Sector must be exactly 4096 bytes");
	TEST_ASSERT(event_size < IEC104_QUEUE_SECTOR_SIZE, 
	            "Event must fit in sector");
}

/* ========== Main Test Runner ========== */

int main(void)
{
	printf("\n");
	printf("========================================================\n");
	printf("  IEC104 Event Queue - Comprehensive Test Suite\n");
	printf("========================================================\n");
	
	TEST_SECTION("STRUCTURE VALIDATION");
	test_structure_sizes();
	
	TEST_SECTION("INITIALIZATION TESTS");
	test_init_with_null_config();
	test_init_with_invalid_io_interface();
	
	TEST_SECTION("BASIC OPERATIONS");
	test_empty_queue_operations();
	test_add_single_event();
	test_add_and_read_event();
	
	TEST_SECTION("NULL POINTER HANDLING");
	test_add_null_event();
	test_read_to_null_buffer();
	
	TEST_SECTION("CAPACITY AND OVERFLOW");
	test_fill_queue_to_capacity();
	test_overflow_handling();
	
	TEST_SECTION("CIRCULAR BUFFER BEHAVIOR");
	test_circular_buffer_wraparound();
	test_multiple_add_read_cycles();
	
	TEST_SECTION("SECTOR BOUNDARY TESTS");
	test_sector_boundary_crossing();
	
	TEST_SECTION("PERSISTENCE");
	test_flash_persistence();
	
	TEST_SECTION("UNSENT EVENT TRACKING");
	test_unsent_event_tracking();
	
	/* Summary */
	printf("\n");
	printf("========================================================\n");
	printf("  TEST SUMMARY\n");
	printf("========================================================\n");
	printf("  Total tests: %d\n", tests_passed + tests_failed);
	printf("  " COLOR_GREEN "Passed: %d" COLOR_RESET "\n", tests_passed);
	printf("  " COLOR_RED "Failed: %d" COLOR_RESET "\n", tests_failed);
	printf("========================================================\n");
	
	if (tests_failed == 0) {
		printf(COLOR_GREEN "\n  ALL TESTS PASSED!\n\n" COLOR_RESET);
		return 0;
	} else {
		printf(COLOR_RED "\n  SOME TESTS FAILED!\n\n" COLOR_RESET);
		return 1;
	}
}
