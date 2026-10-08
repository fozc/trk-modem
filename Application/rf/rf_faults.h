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
} rf_fault_stats_t;

/* Runtime counts since RTU initialization. Phase list sizes are separate.
 * Uncertain-time records are listed, but not counted as exact openings. */
void rf_faults_init(void);
void rf_faults_reset_sequences(void);
void rf_faults_reset_phase(uint8_t feeder, uint8_t phase);
rf_fault_class_t rf_faults_classify(const rf_scp_event_t *event);
bool rf_faults_get_stats(uint8_t zone, uint8_t feeder, rf_fault_stats_t *out);

#endif /* RF_FAULTS_H */

/*** end of file ***/
