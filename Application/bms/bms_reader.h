/*
 * bms_reader.h
 *
 *  Created on: 1 Ağu 2026
 *      Author: fatih
 */

#ifndef BMS_BMS_READER_H_
#define BMS_BMS_READER_H_

#include "bms.h"

void bms_reader_init(void);

/**
 * @brief  Copy the latest decoded BMS snapshot maintained by the reader.
 * @param[out] p_out Destination structure; untouched if NULL.
 * @note   The full-map and SOH responses update disjoint fields of a single
 *         persistent record, so a snapshot always carries the most recent of
 *         each. Thread-safe: No (call from the Contiki cooperative context).
 */
void bms_reader_get_data(bms_data_t *p_out);

#endif /* BMS_BMS_READER_H_ */
