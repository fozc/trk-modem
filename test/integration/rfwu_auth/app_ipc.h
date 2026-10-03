/*
 * app_ipc.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only RFWU dependency double; no hardware access.
 */
#ifndef TEST_RFWU_APP_IPC_H
#define TEST_RFWU_APP_IPC_H
#include <stdbool.h>
#include <stdint.h>
#define APP_IPC_OK 0
int app_ipc_read_download_image_id(uint32_t *crc, uint32_t *size);
int app_ipc_request_update(uint32_t crc, uint32_t size, bool force);
#endif
/*** end of file ***/
