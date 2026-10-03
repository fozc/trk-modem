/*
 * elog.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only RFWU dependency double; no hardware access.
 */
#ifndef TEST_RFWU_ELOG_H
#define TEST_RFWU_ELOG_H
#define ELOG_FW_SRC_RFWU 0U
#define ELOG_FW_RESULT_FAIL 0U
#define ELOG_FW_RESULT_OK 1U
#define ELOG_FW_RESULT_START 2U
#define ELOG_FW_RESULT_AUTH_FAIL 3U
#define elog_log_fw_update(...) do { } while (0)
#endif
/*** end of file ***/
