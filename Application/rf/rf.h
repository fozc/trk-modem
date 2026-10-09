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





/** R2 values retain their original units and signed/float representation.
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

/** BQ-16: offline/event-only acknowledgement is allowed. A fresh asserted
 * live flag leaves the alarm acknowledged and ongoing (latch=false). */
bool rf_ack_trip_failure(uint8_t source);
/* Receipt service may finish storage while the MH inventory is absent.
 * Acknowledge only the exact saved device identity, never its replacement. */
bool rf_ack_stored_alarm(const uint8_t *eui64);

/** BOLATeX BQ-01: open the trip-failure alarm from a stored event record
 * (101 or 105) regardless of when the record arrives. The alarm stays
 * open until a same-opening live 1 -> 0 transition or acknowledgement.
 * Last LIVE zero alone is not evidence that the failure was resolved. */
bool rf_open_trip_failure_alarm(uint8_t zone, uint8_t feeder,
                                uint8_t phase);

/* Called after durable raw storage. Acknowledged duplicate events do not
 * reopen an alarm. True means durable acknowledgement is needed.
 * The last 128 identities are kept until RTU restart. */
bool rf_handle_alarm_event(const rf_scp_event_t *event);
/** True only for an R2 AY matching its accepted slot and unique hash. */
bool rf_event_source_matches(const rf_scp_event_t *event);

typedef struct
{
    uint32_t source_mismatches;
    uint32_t epoch_duration_ms;
    uint8_t store_reset_reason;
    uint8_t counter_status;
    uint8_t epoch_feeder;
    uint8_t epoch_phases;
} rf_service_status_t;

/** Session diagnostics from durably stored records, not persistent alarms. */
void rf_record_service_event(const rf_scp_event_t *event, bool source_matches);
void rf_get_service_status(rf_service_status_t *out);

#endif /* RF_H_ */

/*** end of file ***/
