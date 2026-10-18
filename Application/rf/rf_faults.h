/*
 * rf_faults.h
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * RF fault sequence classification and feeder opening counts.
 */
#ifndef RF_FAULTS_H
#define RF_FAULTS_H

#include "rf_scp_codec.h"

typedef enum
{
    RF_FAULT_NONE,
    RF_FAULT_TEMPORARY,
    RF_FAULT_PERMANENT
} rf_fault_class_t;

typedef struct
{
    uint32_t permanent;
    uint32_t temporary;
    uint32_t uncertain;
    uint32_t pending;
    bool is_final;
} rf_fault_stats_t;

/* Runtime counts since RTU initialization. Phase list sizes are separate.
 * Uncertain-time records are listed, but not counted as exact openings.
 * Forced closure retains a known class and also increments uncertainty;
 * uncertain is an evidence warning, not a disjoint fault class. */
void rf_faults_init(void);
void rf_faults_reset_sequences(void);
void rf_faults_reset_phase(uint8_t feeder, uint8_t phase);
rf_fault_class_t rf_faults_classify(const rf_scp_event_t *event);
/* Call only after both the phase log and replay journal are synchronized. */
void rf_faults_record_stored(const rf_scp_event_t *event);
bool rf_faults_get_stats(uint8_t zone, uint8_t feeder, rf_fault_stats_t *out);

/* BQ-22/B.3: each of the three assigned AYs must provide idle LIVE,
 * then a later pending=0 LIVE, then a fresh HEAD and an empty ring.
 * A known opening waits 600+2 AY uptime seconds on the same boot.
 * Counts remain provisional until a fresh empty-ring observation.
 * Missing LIVE, clock quality or identity evidence is never guessed. */
void rf_faults_live(const rf_scp_live_t *live, uint32_t now_ms);
uint64_t rf_faults_head_started(void);
bool rf_faults_needs_head(uint32_t now_ms);
void rf_faults_ring_empty(uint64_t head_version, uint32_t now_ms);
void rf_faults_hub_restarted(void);

#endif /* RF_FAULTS_H */

/*** end of file ***/
