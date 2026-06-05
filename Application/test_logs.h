/**
 * @file test_logs.h
 * @brief Test utilities for log system - dummy data generation and testing
 * 
 * Created on: Feb 21, 2026
 * Author: Fatih Özcan
 *         fatihozcan@gmail.com
 */

#ifndef TEST_LOGS_H_
#define TEST_LOGS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test version of handle_get_syslogs_json - generates dummy data
 * 
 * Returns dummy system logs in same JSON format for testing pagination
 * Generates 150 dummy entries without using elog system
 */
void test_handle_get_syslogs_json(void);

/**
 * @brief Populate logs with dummy fault records
 * 
 * @param count Number of fault records to generate (0 = clear all)
 */
void test_populate_logs(uint16_t count);

/**
 * @brief Populate logs with dummy system logs (text-based)
 * 
 * @param count Number of system logs to generate (0 = clear all)
 */
void test_populate_syslogs(uint16_t count);

/**
 * @brief Test edge cases for log system
 * 
 * Tests: empty strings, special chars, newlines, max length, non-printable bytes
 */
void test_log_edge_cases(void);

/**
 * @brief Clear all logs for testing
 */
void test_clear_logs(void);

/**
 * @brief Register test commands with shell
 * 
 * Adds "testlog" command with subcommands: fault, sys, edge, clear, scenario
 */
void test_logs_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_LOGS_H_ */
