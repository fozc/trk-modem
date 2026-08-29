/*
 * elog.h
 *
 *  Created on: 14 Eyl 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef ELOG_H_
#define ELOG_H_

#include <stdint.h>
#include <stddef.h>
#include "elog_codes.h"

typedef enum
{
    ELOG_LEVEL_DEBUG,
    ELOG_LEVEL_INFO,
    ELOG_LEVEL_WARN,
    ELOG_LEVEL_ERROR,
    ELOG_LEVEL_FATAL
} elog_level_t;

/* One log record. Stored as the payload of the spi_flash_log ring; the
 * library adds its own sequence number and CRC around it on flash, so the
 * entry_id field is informational only (low 16 bits of the ring seq). */
typedef struct
{
    uint32_t timestamp;
    uint16_t entry_id;
    uint8_t level;
    uint8_t code;
    uint8_t info[16];

}__attribute__((packed)) elog_entry_t;

/* Storage is fixed (W25QXX SPI flash, layout in spi_flash_organization.h). */
void elog_init(void);
void elog_add(elog_code_t _code, elog_level_t level, const void *data, size_t data_len);
void elog_add_entry(const elog_entry_t *entry);

/* Copy up to count entries into entries[], skipping the skip_newest newest
 * ones. Entries are returned newest first; the number of entries actually
 * copied is reported through *out_count. Returns 0 on success, non-zero on
 * error. Single cursor pass: cost is O(skip_newest + count) slot reads. */
int elog_read_recent(uint32_t skip_newest, uint32_t count,
                     elog_entry_t *entries, uint32_t *out_count);

void elog_print(elog_level_t level, const char *message);
void elog_clear(void);
uint16_t elog_get_entry_count(void);
uint16_t elog_get_max_entries(void);
void elog_shell_init(void);

/**
 * @brief Source of a configuration change.
 */
typedef enum
{
    ELOG_SOURCE_WEB    = 0,
    ELOG_SOURCE_SERIAL = 1
} elog_config_source_t;

/**
 * @brief Log a configuration change event.
 *
 * info payload (16 bytes):
 *   [0..3]  client IP (big-endian, 0 when source is serial)
 *   [4]     source  (elog_config_source_t)
 *   [5..15] area label (null-terminated, truncated if needed)
 *
 * @param[in] code    Elog code identifying the config area.
 * @param[in] source  ELOG_SOURCE_WEB or ELOG_SOURCE_SERIAL.
 * @param[in] ip      Remote client IP (uint32_t, big-endian). Pass 0 for serial.
 * @param[in] p_area  Short area label (e.g. "device", "rf").
 */
void elog_log_config_change(elog_code_t code,
                            elog_config_source_t source,
                            uint32_t ip,
                            const char *p_area);

#endif /* ELOG_H_ */
