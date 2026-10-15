/**
 * @file modbus_line_data.h
 * @brief Feeder block reader for the fixed Modbus register map
 * @author Fatih Ozcan
 *         fatihozcan@gmail.com
 * @date 2026-10-15
 *
 * Serves the four feeder blocks (40000 + n * 1000, see modbus_map.h): the
 * IEC104 information mirror, the SCADA-meaningful RF live data and the
 * fault-log lists, qualified by per-feeder and per-phase quality registers.
 */

#ifndef MODBUS_LINE_DATA_H
#define MODBUS_LINE_DATA_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Read one register of the feeder block space.
 *
 * Every address inside 40000..43999 answers true so a master can bulk-read
 * a whole feeder block in one FC03 window: live fields resolve to their
 * measurement, reserved gaps and never-delivered fields read back as 0.
 * Validity is carried by the quality registers - an undefined feeder (no RF
 * line provisioned for Fider_ID n) reports zero quality everywhere, and a
 * provisioned feeder that has not yet delivered a live sample reports zero
 * values with only the defined bit set.
 *
 * @param[in]  reg_addr Logical holding-register address.
 * @param[in]  now_ms   FC03 snapshot time (ms tick), freshness reference.
 * @param[out] value    Destination for the 16-bit register value.
 * @return true inside the feeder space, false for every other address.
 */
bool modbus_line_data_read(uint16_t reg_addr, uint32_t now_ms,
                           uint16_t *value);

#endif /* MODBUS_LINE_DATA_H */

/*** end of file ***/
