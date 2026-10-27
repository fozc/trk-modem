/*
 * rf_alarm.h
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Durable trip-failure delivery and receipt acknowledgement.
 */
#ifndef RF_ALARM_H
#define RF_ALARM_H

#include "rf_scp_codec.h"
#include "iec104_types.h"
#include "iec104_event_log.h"

/**
 * @brief Console announcement decision for one delivered alarm record.
 *
 * Rising-edge only: an announcement is produced when a delivery reports
 * an active alarm for the first time, or when a previously reported
 * alarm changes its active state. Unchanged states stay silent so
 * retries never repeat the line.
 */
typedef enum
{
    RF_ALARM_ANNOUNCE_NONE = 0,   /**< No announcement. */
    RF_ALARM_ANNOUNCE_ACTIVE,     /**< Alarm became active (red line). */
    RF_ALARM_ANNOUNCE_CLEARED     /**< Previously active alarm cleared. */
} rf_alarm_announce_t;

/**
 * @brief Decide and format the console announcement for a delivery.
 *
 * @param[in,out] out              Buffer receiving the NUL-terminated line.
 * @param[in]     out_len          Size of out; 40 bytes suffice.
 * @param[in]     record           Delivered alarm record; feeder/phase are
 *                                 zero-based line indexes, printed as +1.
 * @param[in]     previous_reported Whether this slot already reported once.
 * @param[in]     previous_active  Active state of the previous report.
 *
 * @return Announcement kind; out is filled only when it is not NONE.
 */
rf_alarm_announce_t rf_alarm_announcement(char *out, unsigned int out_len,
                                          const iec104_alarm_record_t *record,
                                          bool previous_reported,
                                          bool previous_active);

void rf_alarm_init(void);
void rf_alarm_live(uint8_t source, const cp56time2a_t *time);
bool rf_alarm_record(const rf_scp_event_t *event);
void rf_alarm_process(uint32_t now_ms);
bool rf_alarm_is_idle(void);

typedef struct
{
    uint8_t line;
    uint8_t phase;
    uint8_t eui64[8];
    uint32_t revision;
    bool confirm_offline;
} rf_alarm_ack_request_t;

typedef enum
{
    RF_ALARM_ACK_OK,
    RF_ALARM_ACK_INVALID,
    RF_ALARM_ACK_STALE,
    RF_ALARM_ACK_OFFLINE,
    RF_ALARM_ACK_BUSY,
    RF_ALARM_ACK_IOA_CONFLICT,
    RF_ALARM_ACK_STORAGE
} rf_alarm_ack_result_t;

/* Admin web caller only. Persist identity/user/time before changing the latch. */
rf_alarm_ack_result_t rf_alarm_acknowledge(const rf_alarm_ack_request_t *request);

#endif /* RF_ALARM_H */

/*** end of file ***/
