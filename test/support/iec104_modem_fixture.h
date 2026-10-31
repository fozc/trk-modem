/**
 * @file iec104_modem_fixture.h
 * @brief Host-only RF/storage/transport boundary fixture for the real modem.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_MODEM_FIXTURE_H
#define IEC104_MODEM_FIXTURE_H

#include "iec104_modem.h"
#include "rf.h"
#include "power_board_scp.h"
#include "rtc.h"

typedef struct
{
    iec104_modem_t modem;
    iec104_config_t config;
    breaker_t breaker;
    rf_phase_data_t phase;
    power_board_snapshot_t power;
    iec104_event_record_t event;
    fault_log_t fault;
    bool has_event;
    bool has_fault;
    bool has_live;
    bool writable;
    bool storage_failure;
    uint32_t marked;
    uint32_t sync_count;
    uint32_t reset_count;
    uint32_t close_count;
    uint32_t clock_count;
    uint32_t fault_reads;
    rtc_t clock;
    uint8_t frames[256][255];
    size_t lengths[256];
    size_t head;
    size_t count;
} iec104_modem_fixture_t;

extern iec104_modem_fixture_t iec104_modem_fixture;
bool iec104_fixture_start(uint8_t window);
bool iec104_fixture_configure_sbo(uint8_t active, uint16_t timeout_seconds);
void iec104_fixture_set_period_ms(uint32_t milliseconds);
size_t iec104_fixture_feed(const uint8_t *data, size_t length, uint32_t time);
void iec104_fixture_poll(uint32_t time);
void iec104_fixture_close(void);
size_t iec104_fixture_pop(uint8_t *data, size_t capacity);
void iec104_fixture_event(uint8_t kind);
void iec104_fixture_notify_event(void);
uint32_t iec104_fixture_marked(void);
uint32_t iec104_fixture_reset_count(void);
uint32_t iec104_fixture_clock_count(void);
uint32_t iec104_fixture_state(void);

#endif
/*** end of file ***/
