/*
 * rf_scp_engine.h
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * SCP v1.0 single-outstanding master engine: one job slot, SEQ management,
 * timeout, retry, and link-state tracking. The core is Contiki- and
 * HAL-independent (time and packets are injected) so it is host-testable.
 */

#ifndef RF_RF_SCP_ENGINE_H_
#define RF_RF_SCP_ENGINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "scp.h"                 /* scp_packet_t, SCP_TYPE_* */

/** Logical request kinds. Grow phase by phase (S1: GET_STATUS only). */
typedef enum
{
    RF_JOB_NONE = 0,
    RF_JOB_GET_STATUS
} rf_job_kind_t;

/** Outcome delivered to the completion callback. */
typedef enum
{
    RF_RESULT_OK = 0,       /* matching ACK received                  */
    RF_RESULT_ERR,          /* peer returned ERROR (link still alive) */
    RF_RESULT_TIMEOUT       /* no response after all retries          */
} rf_result_t;

/** Coarse link health derived from consecutive job outcomes. */
typedef enum
{
    RF_LINK_UNKNOWN = 0,
    RF_LINK_UP,
    RF_LINK_DOWN
} rf_link_state_t;

/** Emit one encoded request onto the physical link (injected dependency). */
typedef void (*rf_send_fn_t)(const scp_packet_t *req);

/** Job completion notification (injected dependency). @p rsp is NULL on timeout. */
typedef void (*rf_done_fn_t)(rf_job_kind_t kind,
                             rf_result_t result,
                             const scp_packet_t *rsp);

typedef struct
{
    /* --- configuration --- */
    rf_send_fn_t    send;
    rf_done_fn_t    on_done;
    uint16_t        timeout_ms;
    uint8_t         max_retry;
    uint8_t         link_fail_n;

    /* --- runtime (single job slot) --- */
    bool            busy;
    rf_job_kind_t   job;
    uint8_t         cmd;
    uint8_t         seq;            /* SEQ of the outstanding request */
    uint8_t         next_seq;       /* +1 per new request; 0xFF -> 0x00 wrap */
    uint8_t         retries_left;
    uint32_t        deadline_ms;
    uint8_t         fail_streak;
    rf_link_state_t link;
} rf_engine_t;

/**
 * @brief Initialise the engine.
 *
 * @param[out] engine       Engine context.
 * @param[in]  send         Request emit callback (must not be NULL).
 * @param[in]  on_done      Job completion callback (must not be NULL).
 * @param[in]  timeout_ms   Per-attempt response timeout in ms.
 * @param[in]  max_retry    Retransmit attempts after the first send.
 * @param[in]  link_fail_n  Consecutive timeouts before link is marked DOWN.
 */
void rf_engine_init(rf_engine_t *engine,
                    rf_send_fn_t send,
                    rf_done_fn_t on_done,
                    uint16_t timeout_ms,
                    uint8_t max_retry,
                    uint8_t link_fail_n);

/**
 * @brief Start a new job.
 *
 * @param[in,out] engine  Engine context.
 * @param[in]     kind    Job to run.
 * @param[in]     now_ms  Current monotonic time in ms.
 *
 * @return true if the job was accepted and sent; false if the engine is
 *         busy (single-outstanding rejection) or the job is unsupported.
 */
bool rf_engine_start(rf_engine_t *engine, rf_job_kind_t kind, uint32_t now_ms);

/**
 * @brief Drive timeout and retransmission. Call every poll cycle.
 *
 * @param[in,out] engine  Engine context.
 * @param[in]     now_ms  Current monotonic time in ms.
 */
void rf_engine_tick(rf_engine_t *engine, uint32_t now_ms);

/**
 * @brief Feed a solicited ACK/ERROR to the engine (request-response match).
 *
 * Proactive SET packets must NOT be routed here. Non-matching or unexpected
 * responses are silently ignored.
 *
 * @param[in,out] engine  Engine context.
 * @param[in]     rsp     Received ACK or ERROR packet.
 */
void rf_engine_on_response(rf_engine_t *engine, const scp_packet_t *rsp);

/** @return true while a job is outstanding. */
bool rf_engine_is_busy(const rf_engine_t *engine);

/** @return current coarse link state. */
rf_link_state_t rf_engine_link_state(const rf_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_SCP_ENGINE_H_ */

/*** end of file ***/
