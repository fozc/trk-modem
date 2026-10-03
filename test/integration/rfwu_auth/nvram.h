/*
 * nvram.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Host-only RFWU dependency double; no hardware access.
 */
#ifndef TEST_RFWU_NVRAM_H
#define TEST_RFWU_NVRAM_H
#include <stdbool.h>
#include <stdint.h>
typedef struct
{
    uint32_t magic;
    uint32_t file_hash;
    uint32_t total_size;
    uint32_t received_bytes;
    uint32_t shared_key;
    uint32_t fw_crc;
    uint32_t fw_size;
} rfwu_nvram_t;
const rfwu_nvram_t *nvram_get_rfwu(void);
void nvram_set_rfwu(const rfwu_nvram_t *record);
#endif
/*** end of file ***/
