/*
 * runtime_stats.h
 *
 * Created on: Oct 24, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 *
 * Persistent operating counters, separate from configuration storage.
 */
#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H

#include <stdbool.h>
#include <stdint.h>

#define RUNTIME_STATS_INTERVAL_SECONDS 900U
#define RUNTIME_STATS_CAPACITY 128U

typedef enum
{
    RUNTIME_LIFETIME,
    RUNTIME_GSM_RX_BYTES,
    RUNTIME_GSM_TX_BYTES,
    RUNTIME_WEB_SESSIONS,
    RUNTIME_IEC_SERVER_SESSIONS,
    RUNTIME_IEC_CLIENT_SESSIONS,
    RUNTIME_IEC_ACTIVE_SECONDS,
    RUNTIME_GSM_RECOVERIES,
    RUNTIME_MODBUS_REQUESTS,
    RUNTIME_MODBUS_CRC_ERRORS,
    RUNTIME_MODBUS_UART_ERRORS,
    RUNTIME_MODBUS_TX_ERRORS,
    RUNTIME_BOOTS,
    RUNTIME_COUNTER_COUNT
} runtime_counter_t;

typedef struct
{
    uint32_t timestamp;
    uint32_t counters[RUNTIME_COUNTER_COUNT];
    bool clock_valid;
} runtime_stats_t;

/* Main/cooperative context only. No function in this API is ISR-safe.
 * An empty log starts at zero; the first checkpoint is after 15 minutes.
 * An I/O error or unknown record version never authorizes erasing the log.
 */
bool runtime_stats_init(void);
void runtime_stats_add(runtime_counter_t counter, uint32_t amount);
uint32_t runtime_stats_get(runtime_counter_t counter);
void runtime_stats_set(runtime_counter_t counter, uint32_t value);
/* Called from the existing one-second main-process timer. */
void runtime_stats_tick(bool iec_active);
bool runtime_stats_save(void);
void runtime_stats_snapshot(runtime_stats_t *out);
/* skip=0 is the newest committed record. */
bool runtime_stats_read(uint32_t skip, runtime_stats_t *out);
void runtime_stats_shell_init(void);

#endif
/*** end of file ***/
