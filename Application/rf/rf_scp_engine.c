/*
 * rf_scp_engine.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * SCP v1.0 single-outstanding master engine. Contiki- and HAL-independent:
 * time is passed in and requests leave through an injected send callback,
 * so the whole state machine is deterministic and host-testable.
 */

#include "rf_scp_engine.h"
#include "rf_scp_cmd.h"
#include <string.h>

/**
 * @brief Build the request packet for a job.
 *
 * @param[in]  kind  Job kind.
 * @param[in]  seq   Sequence number to stamp.
 * @param[out] req   Destination packet.
 *
 * @return true if the job maps to a request; false if unsupported.
 */
static bool build_request(rf_job_kind_t kind, uint8_t seq, scp_packet_t *req)
{
    req->dst = RF_SCP_ADDR_HUB;
    req->src = RF_SCP_ADDR_RTU;
    req->seq = seq;

    switch (kind)
    {
        case RF_JOB_GET_STATUS:
            req->type     = SCP_TYPE_GET;
            req->cmd      = RF_SCP_CMD_GET_STATUS;
            req->data_len = 0U;
            return true;

        default:                /* MISRA 16.4 */
            return false;
    }
}

/** Emit the current job's request and (re)arm the timeout deadline. */
static void arm_request(rf_engine_t *engine, uint32_t now_ms)
{
    scp_packet_t req;

    (void)build_request(engine->job, engine->seq, &req);
    engine->send(&req);
    engine->deadline_ms = now_ms + engine->timeout_ms;
}

/** Finish the current job: clear the slot and notify the caller. */
static void finish_job(rf_engine_t *engine, rf_result_t result,
                       const scp_packet_t *rsp)
{
    rf_job_kind_t kind = engine->job;

    engine->busy = false;
    engine->job  = RF_JOB_NONE;
    engine->on_done(kind, result, rsp);
}

void rf_engine_init(rf_engine_t *engine,
                    rf_send_fn_t send,
                    rf_done_fn_t on_done,
                    uint16_t timeout_ms,
                    uint8_t max_retry,
                    uint8_t link_fail_n)
{
    if (NULL == engine)
    {
        return;
    }

    (void)memset(engine, 0, sizeof(*engine));
    engine->send        = send;
    engine->on_done     = on_done;
    engine->timeout_ms  = timeout_ms;
    engine->max_retry   = max_retry;
    engine->link_fail_n = link_fail_n;
    engine->link        = RF_LINK_UNKNOWN;
}

bool rf_engine_start(rf_engine_t *engine, rf_job_kind_t kind, uint32_t now_ms)
{
    scp_packet_t probe;

    if ((NULL == engine) || (NULL == engine->send))
    {
        return false;
    }

    if (engine->busy)
    {
        return false;   /* single-outstanding: reject the second job */
    }

    if (!build_request(kind, engine->next_seq, &probe))
    {
        return false;   /* unsupported job */
    }

    engine->busy         = true;
    engine->job          = kind;
    engine->cmd          = probe.cmd;
    engine->seq          = engine->next_seq;
    engine->retries_left = engine->max_retry;
    engine->next_seq     = (uint8_t)(engine->next_seq + 1U); /* 0xFF -> 0x00 */

    arm_request(engine, now_ms);
    return true;
}

void rf_engine_tick(rf_engine_t *engine, uint32_t now_ms)
{
    if ((NULL == engine) || (!engine->busy))
    {
        return;
    }

    /* Signed delta handles uint32_t tick wraparound safely. */
    if ((int32_t)(now_ms - engine->deadline_ms) < 0)
    {
        return;   /* not expired yet */
    }

    if (0U < engine->retries_left)
    {
        engine->retries_left = (uint8_t)(engine->retries_left - 1U);
        arm_request(engine, now_ms);   /* same SEQ, no other request between */
        return;
    }

    /* All attempts exhausted -> job dropped. */
    engine->fail_streak = (uint8_t)(engine->fail_streak + 1U);
    if (engine->fail_streak >= engine->link_fail_n)
    {
        engine->link = RF_LINK_DOWN;
    }
    finish_job(engine, RF_RESULT_TIMEOUT, NULL);
}

void rf_engine_on_response(rf_engine_t *engine, const scp_packet_t *rsp)
{
    if ((NULL == engine) || (NULL == rsp) || (!engine->busy))
    {
        return;
    }

    if ((rsp->cmd != engine->cmd) || (rsp->seq != engine->seq))
    {
        return;   /* stale or foreign response -> ignore */
    }

    if (SCP_TYPE_ACK == rsp->type)
    {
        engine->fail_streak = 0U;
        engine->link        = RF_LINK_UP;
        finish_job(engine, RF_RESULT_OK, rsp);
    }
    else if (SCP_TYPE_ERROR == rsp->type)
    {
        /* Peer answered (link is alive) but the job failed; includes the
         * 0x05 NOT_AVAILABLE "try again later" case. */
        engine->fail_streak = 0U;
        engine->link        = RF_LINK_UP;
        finish_job(engine, RF_RESULT_ERR, rsp);
    }
    else
    {
        /* MISRA 15.7: other TYPEs are not valid responses here. */
    }
}

bool rf_engine_is_busy(const rf_engine_t *engine)
{
    return (NULL != engine) ? engine->busy : false;
}

rf_link_state_t rf_engine_link_state(const rf_engine_t *engine)
{
    return (NULL != engine) ? engine->link : RF_LINK_UNKNOWN;
}

/*** end of file ***/
