/**
 * @file iec104_modem.h
 * @brief Modem mapping to existing NVRAM, RF, RTC and durable event services.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_MODEM_H
#define IEC104_MODEM_H

#include "iec104_server.h"
#include "iec104_event_log.h"

typedef struct
{
    void *context;
    bool (*write)(void *context, const uint8_t *frame, size_t length);
    /** Schedule the existing GSM listener close operation. */
    void (*request_close)(void *context);
    /** Schedule existing 3 s flush + 2 s close + reboot process. */
    void (*request_reset)(void *context);
    void (*event)(void *context, iec104_core_event_t event);
} iec104_modem_io_t;

typedef struct
{
    iec104_server_t server;
    iec104_modem_io_t io;
    uint32_t now_ms;
    uint32_t started_ms;
    uint32_t periodic_since;
    uint32_t period_ms;
    size_t snapshot_index;
    size_t periodic_index;
    bool snapshot_done;
    bool periodic_pending;
    bool battery_sent;
    uint8_t battery_value;
    uint32_t battery_ioa;
    iec104_event_record_t replay_record;
    uint16_t replay_sequence;
    uint8_t replay_stage;
    bool replay_waiting;
    bool replay_loaded;
    bool replay_dirty;
    bool replay_ready;
} iec104_modem_t;

/** @brief Bind existing product services to a new IEC104 modem instance.
 * @param[out] modem Caller-owned storage, normally static in the modem process.
 * @param[in] io Port callbacks/context; write, request_close/reset are required.
 * @return True if NVRAM settings and callbacks initialize a usable server.
 * @pre NVRAM, event log, RF and RTC services have already been initialized.
 * @note Copies global settings from the NVRAM RAM image. Does not open TCP,
 * enable BMS/dummy producers or change persistent settings/data.
 * Reinitialize changed configuration only while disconnected.
 */
bool iec104_modem_init(iec104_modem_t *modem, const iec104_modem_io_t *io);
/** @brief Record TCP open and restart initial status/battery publication.
 * @param[in,out] modem Initialized modem instance.
 * @param[in] now_ms Current monotonic millisecond tick, e.g. HAL_GetTick().
 * @note Sequence numbers reset here, not on a duplicate STARTDT.
 */
void iec104_modem_open(iec104_modem_t *modem, uint32_t now_ms);
/** @brief Record physical socket closure and preserve unacknowledged events.
 * @param[in,out] modem Instance to close; NULL is harmless.
 * @note Drops partial publication state and attempts existing journal sync.
 */
void iec104_modem_close(iec104_modem_t *modem);
/** @brief Receive a GSM/TCP payload and route it through server/core processing.
 * @param[in,out] modem Open modem instance owned by the main/process context.
 * @param[in] data Borrowed payload bytes; no whole-frame alignment is required.
 * @param[in] length Available bytes in data.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @return Consumed prefix length; check session state and retain/retry a suffix
 * if the session remains open. Zero is also returned for NULL input/instance.
 * @note This is the production RX entry point; core.io.receive is a later
 * parsed-ASDU callback, not the UART/GSM byte receiver.
 */
size_t iec104_modem_on_data_received(iec104_modem_t *modem, const uint8_t *data, size_t length, uint32_t now_ms);
/** @brief Run protocol timers/GI and schedule product publications/replay.
 * @param[in,out] modem Initialized instance in the cooperative main context.
 * @param[in] now_ms Current monotonic millisecond tick.
 * @note Reads existing RF/PowerBoard/fault services. GI/replies take priority;
 * spontaneous work handles initial status, battery changes, replay and periodic
 * data. ACK delivery alone consumes durable records. Call regularly, not in ISR.
 */
void iec104_modem_poll(iec104_modem_t *modem, uint32_t now_ms);
/** @brief Request replay after a producer has durably stored a new live event.
 * @param[in,out] modem Instance whose next eligible poll should service replay.
 * @pre The producer has appended/synchronized the event using existing storage.
 * @note Sets a scheduling flag only; does not store/send/acknowledge the event
 * in this call. Uses the existing journal, without another event queue.
 */
void iec104_modem_notify_event(iec104_modem_t *modem);

#endif
/*** end of file ***/
