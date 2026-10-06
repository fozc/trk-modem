/*
 * rf.h
 *
 *  Created on: Nov 9, 2025
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#ifndef RF_H_
#define RF_H_

#include <stdint.h>
#include <stdbool.h>
#include "types.h"
#include "rf_scp_codec.h"
#include <stddef.h>





/** R1 values retain their original units and signed/float representation.
 * is_online describes fresh communication, not breaker position.
 * trip.current_amps is the instantaneous trip value, not fault magnitude;
 * trip.timestamp_ms is reserved and must not be used as event time.
 */
typedef struct
{
    uint8_t eui64[8];
    rf_scp_live_t live;
    /* RTU acceptance time, not an AY measurement timestamp. */
    cp56time2a_t received_time;
    rf_scp_trip_t trip;
    uint32_t last_live_ms;
    uint32_t last_trip_ms;
    bool has_live;
    bool has_trip;
    bool is_online;
    bool current_valid;
    bool trip_voltage_valid;
    bool harvest_voltage_valid;
    bool trip_failed;
    bool trip_failure_latched;
    bool uptime_stalled;
    uint8_t source;
} rf_phase_data_t;

typedef struct
{
    uint16_t window;
    uint32_t total;
    bool is_active;
    bool has_report;
} rf_anomaly_data_t;

/** Volatile state initialization at RTU startup. */
void rf_init(void);
void rf_hub_restarted(void);
/* NULL received_time records an unknown clock (IV=1). */
bool rf_handle_live(const rf_scp_live_t *live, uint32_t now_ms,
                     const cp56time2a_t *received_time);
bool rf_handle_trip(const rf_scp_trip_t *trip, uint32_t now_ms);
bool rf_handle_anomaly(const rf_scp_anomaly_t *report);

/** Outputs remain untouched on invalid/unassigned identity. */
bool rf_get_source_data(uint8_t source, uint32_t now_ms, rf_phase_data_t *out);
bool rf_get_phase_data(size_t line_index, phase_id_t phase, uint32_t now_ms,
                        rf_phase_data_t *out);
bool rf_get_anomaly(uint8_t source, uint8_t path, rf_anomaly_data_t *out);

/** Operator acknowledgement cannot clear an asserted live Trip_Failed. */
bool rf_ack_trip_failure(uint8_t source);

#endif /* RF_H_ */

/*** end of file ***/
