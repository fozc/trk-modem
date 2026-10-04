/*
 * bms_reader.h
 *
 *  Created on: Aug 1, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef BMS_BMS_READER_H_
#define BMS_BMS_READER_H_

#include "bms.h"

void bms_reader_init(void);
void bms_rx_interrupt_handler(uint8_t data);

/**
 * @brief  Copy the latest decoded BMS snapshot maintained by the reader.
 * @param[out] p_out Destination structure; untouched if NULL.
 * @note   The full-map and SOH responses update disjoint fields of a single
 *         persistent record, so a snapshot always carries the most recent of
 *         each. Thread-safe: No (call from the Contiki cooperative context).
 *         Full-map and SOH validity expire independently after 5000 ms
 *         without a successfully parsed response of that type.
 */
void bms_reader_get_data(bms_data_t *p_out);

#endif /* BMS_BMS_READER_H_ */

/*** end of file ***/
