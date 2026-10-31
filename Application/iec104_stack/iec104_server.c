/**
 * @file iec104_server.c
 * @brief GI transactions, command routing and spontaneous publication.
 * Created on: Oct 25, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_server.h"

#include <string.h>

static iec104_asdu_t header(const iec104_server_t *server, uint8_t type, uint8_t cause, uint8_t originator)
{
    return (iec104_asdu_t){
        .type = type, .cause = cause, .address = server->config.common_address, .originator = originator
    };
}

static bool write_frame(void *context, const uint8_t *data, size_t length)
{
    iec104_server_t *server = context;
    return server->io.write(server->io.context, data, length);
}

static void link_event(void *context, iec104_core_event_t event)
{
    iec104_server_t *server = context;
    if (event != IEC104_CORE_LINK_STARTED)
    {
        server->reset_selected = false;
    }
    if (event == IEC104_CORE_LINK_STARTED)
    {
        server->initialization_pending = !server->initialization_sent;
    }
    else if (event != IEC104_CORE_LINK_STOPPED)
    {
        server->query_state            = IEC104_QUERY_IDLE;
        server->response_pending       = false;
        server->reset_state            = IEC104_RESET_NONE;
        server->query_frame.length     = 0U;
        server->query_more             = false;
        server->initialization_pending = false;
    }
    else
    {
        /* STOP pauses transactions; START in this TCP session resumes them. */
    }
    if (server->io.event != NULL)
    {
        server->io.event(server->io.context, event);
    }
}

static void delivered(void *context, uint32_t tag)
{
    iec104_server_t *server = context;
    if (server->io.delivered != NULL)
    {
        server->io.delivered(server->io.context, tag);
    }
}

static void echo_response(iec104_server_t *server, const iec104_asdu_t *request, uint8_t cause, bool negative)
{
    iec104_asdu_t reply = *request;
    reply.cause         = cause;
    reply.negative      = negative;
    /* Echo arbitrary unknown-type payloads without assuming a value size. */
    (void)iec104_builder_begin(&server->response, &reply, 0U);
    server->response.data[1] = (uint8_t)(request->count | (request->sequence ? 0x80U : 0U));
    memcpy(&server->response.data[6], request->payload, request->payload_len);
    server->response.length  = 6U + request->payload_len;
    server->response_pending = true;
}

bool iec104_response_add(iec104_response_t *response, const iec104_point_t *point)
{
    if ((response == NULL) || (point == NULL) || (point->type == 0U) || (point->ioa > 0xFFFFFFU) ||
        (point->value_size == 0U) || (point->value_size > IEC104_POINT_VALUE_MAX) || (response->asdu.length < 6U) ||
        (response->asdu.length > IEC104_CORE_ASDU_MAX))
    {
        return false;
    }
    bool first = response->asdu.length == 6U;
    if (first && (response->asdu.data[1] != 0U))
    {
        return false;
    }
    if (!first && ((point->type != response->asdu.data[0]) || (point->value_size != response->asdu.value_size)))
    {
        return false;
    }
    if (first)
    {
        response->asdu.data[0]    = point->type;
        response->asdu.value_size = point->value_size;
    }
    return iec104_builder_add(&response->asdu, point->ioa, point->value);
}

/** Command confirmation for activation or selection cancellation. */
static uint8_t confirmation_cause(const iec104_request_t *request)
{
    return request->asdu.cause == IEC104_COT_DEACTIVATION ? IEC104_COT_DEACTIVATION_CON : IEC104_COT_ACTIVATION_CON;
}

static bool is_reset_control(const iec104_server_t *server, const iec104_request_t *request)
{
    return (request->asdu.type == IEC104_TYPE_C_SC_NA_1) && (server->config.reset_ioa != 0U) &&
           (request->ioa == server->config.reset_ioa);
}

/** Protect only the configured reset control; other application commands remain application-owned. */
static bool check_reset_request(iec104_server_t *server, const iec104_request_t *request)
{
    bool allowed = true;
    if (request->asdu.type == IEC104_TYPE_C_RP_NA_1)
    {
        allowed = !server->config.reset_sbo && (request->qualifier == IEC104_RESET_PROCESS_GENERAL) &&
                  (server->io.request_reset != NULL);
    }
    else if (is_reset_control(server, request))
    {
        bool select = request->single_command.select && (request->asdu.cause == IEC104_COT_ACTIVATION);
        if (select)
        {
            server->reset_selected = false;
            allowed                = server->config.reset_sbo;
        }
        else if (server->config.reset_sbo)
        {
            allowed = server->reset_selected &&
                      ((uint32_t)(server->core.now_ms - server->reset_selected_since) <
                       server->config.sbo_timeout_ms) &&
                      (server->reset_selected_originator == request->asdu.originator) &&
                      (server->reset_selected_command == (uint8_t)(request->qualifier & 0x7FU));
            /* An execute/cancel attempt consumes the selection, even when it mismatches. */
            server->reset_selected = false;
        }
        else
        {
            allowed = request->asdu.cause == IEC104_COT_ACTIVATION;
        }
        allowed = allowed && (server->io.request_reset != NULL);
    }
    else
    {
        /* A non-reset request cannot select or consume the reset control. */
    }
    if (!allowed)
    {
        echo_response(server, &request->asdu, confirmation_cause(request), true);
    }
    return allowed;
}

/** Commit selection after application acceptance; physical reset still waits for response TX acceptance. */
static void reset_request_accepted(iec104_server_t *server, const iec104_request_t *request)
{
    bool control  = is_reset_control(server, request);
    bool activate = request->asdu.cause == IEC104_COT_ACTIVATION;
    if (control && activate && request->single_command.select)
    {
        server->reset_selected            = true;
        server->reset_selected_command    = request->qualifier & 0x7FU;
        server->reset_selected_originator = request->asdu.originator;
        server->reset_selected_since      = server->core.now_ms;
    }
    if (control && activate && !request->single_command.select)
    {
        server->reset_state = IEC104_RESET_SEND_TERMINATION;
    }
    else if (request->asdu.type == IEC104_TYPE_C_RP_NA_1)
    {
        server->reset_state = IEC104_RESET_SCHEDULE;
    }
    else
    {
        server->reset_state = IEC104_RESET_NONE;
    }
}

/** Initialize one response with protocol header fields; application only adds typed values. */
static void response_init(iec104_response_t *response, const iec104_request_t *request)
{
    iec104_asdu_t asdu = request->asdu;
    asdu.cause         = asdu.type == IEC104_TYPE_C_IC_NA_1 ? request->qualifier : confirmation_cause(request);
    asdu.sequence      = false;
    /* builder_begin ASDU alanini zaten baslatir; ayni tamponu iki kez sifirlama. */
    response->more = false;
    (void)iec104_builder_begin(&response->asdu, &asdu, 0U);
}

/** Check an application-produced chunk before caching/sending it. */
static bool response_valid(const iec104_response_t *response, const iec104_request_t *request)
{
    if (response->asdu.length == 6U)
    {
        return response->asdu.data[1] == 0U;
    }
    iec104_asdu_t asdu;
    return iec104_asdu_parse(&asdu, response->asdu.data, response->asdu.length) &&
           iec104_asdu_objects_valid(&asdu, response->asdu.value_size) && !asdu.negative && !asdu.test &&
           (asdu.address == request->asdu.address) && (asdu.originator == request->asdu.originator) &&
           (asdu.cause ==
            (request->asdu.type == IEC104_TYPE_C_IC_NA_1 ? request->qualifier : confirmation_cause(request)));
}

/** @brief Invoke the single application handler once, then cache its response.
 * GI continuation uses the same handler later; transport retries never execute it again.
 */
static void execute_request(iec104_server_t *server, const iec104_asdu_t *asdu)
{
    bool gi = asdu->type == IEC104_TYPE_C_IC_NA_1;
    if (gi && ((asdu->payload[3] < IEC104_QOI_STATION) || (asdu->payload[3] > IEC104_QOI_GROUP_16) ||
               (server->query_state != IEC104_QUERY_IDLE)))
    {
        echo_response(server, asdu, IEC104_COT_ACTIVATION_CON, true);
        return;
    }
    iec104_request_t request = { .asdu      = *asdu,
                                 .ioa       = asdu->payload_len >= 3U ? iec104_read_ioa(asdu->payload) : 0U,
                                 .qualifier = asdu->payload_len >= 4U ? asdu->payload[3] : 0U };
    if (((asdu->type == IEC104_TYPE_C_CS_NA_1) && !iec104_time_parse(&request.time, &asdu->payload[3])) ||
        ((asdu->type == IEC104_TYPE_C_SC_NA_1) &&
         !iec104_single_command_parse(&request.single_command, request.qualifier)))
    {
        echo_response(server, asdu, confirmation_cause(&request), true);
        return;
    }
    if (!check_reset_request(server, &request))
    {
        return;
    }
    iec104_response_t response;
    response_init(&response, &request);
    iec104_request_result_t result = server->io.handle_request == NULL ?
                                         IEC104_REQUEST_UNSUPPORTED :
                                         server->io.handle_request(server->io.context, &request, &response);
    if ((result == IEC104_REQUEST_ERROR) ||
        ((result == IEC104_REQUEST_ACCEPTED) &&
         (!response_valid(&response, &request) || (!gi && response.more) ||
          (((asdu->type == IEC104_TYPE_C_CS_NA_1) || (asdu->type == IEC104_TYPE_C_RP_NA_1) ||
            is_reset_control(server, &request)) &&
           (response.asdu.length != 6U)))))
    {
        server->overloaded = true;
    }
    else if (result != IEC104_REQUEST_ACCEPTED)
    {
        bool builtin = gi || (asdu->type == IEC104_TYPE_C_CS_NA_1) || (asdu->type == IEC104_TYPE_C_RP_NA_1) ||
                       is_reset_control(server, &request);
        uint8_t cause = (builtin || (result == IEC104_REQUEST_REJECTED)) ? confirmation_cause(&request) :
                                                                           IEC104_COT_UNKNOWN_TYPE;
        if (result == IEC104_REQUEST_UNKNOWN_IOA)
        {
            cause = IEC104_COT_UNKNOWN_IOA;
        }
        echo_response(server, asdu, cause, true);
    }
    else if (gi)
    {
        server->query_qoi        = request.qualifier;
        server->query_originator = asdu->originator;
        server->query_more       = response.more;
        server->query_frame      = response.asdu;
        if (server->query_frame.length == 6U)
        {
            server->query_frame.length = 0U;
        }
        server->query_state = IEC104_QUERY_CONFIRM;
    }
    else
    {
        if (response.asdu.length == 6U)
        {
            echo_response(server, asdu, confirmation_cause(&request), false);
        }
        else
        {
            server->response         = response.asdu;
            server->response_pending = true;
        }
        reset_request_accepted(server, &request);
    }
}

/** @brief Validate and route a decoded request from the core receive callback.
 * @param[in,out] context Server instance registered as core.io.context.
 * @param[in] request Borrowed ASDU view, valid only during this callback.
 * @return True: this server consumes the request synchronously, including errors.
 * @note Errors stage negative responses or trigger bounded session closure.
 * Builtin commands require count=1 and SQ=0.
 * All offsets here are relative to request->payload, NOT the APDU header.
 * @code
 * Type 45 (SCO):    [0..2:IOA]             [3:S/E bit 7, QU bits 6..2, reserved bit 1=0, SCS bit 0]
 * Type 100 (GI):    [0..2:IOA=0]           [3:QOI=20..36]
 * Type 103 (clock): [0..2:IOA=0]           [3..9:CP56Time2a]
 * Type 105 (reset): [0..2:IOA=0/reset IOA] [3:QRP=1 for general reset]
 *
 * payload[0] = ASDU[6] = complete APDU/frame[12].
 * Clock time thus starts at payload[3] = ASDU[9] = APDU[15].
 * @endcode
 * ASDU header fields (CA/OA/COT/test) were separated by iec104_asdu_parse().
 * Validate the object extent before accessing IOA or the qualifier/time.
 */
static bool process_asdu(void *context, const iec104_asdu_t *request)
{
    iec104_server_t *server = context;
    if (server->response_pending)
    {
        /* Bounded overload is explicit: close without executing a command
         * or silently losing a reply. The caller can reconnect and retry.
         * Never block parsing an ACK behind a queued I frame indefinitely.
         */
        server->overloaded = true;
        return true;
    }
    bool builtin = (request->type == IEC104_TYPE_C_IC_NA_1) || (request->type == IEC104_TYPE_C_CS_NA_1) ||
                   (request->type == IEC104_TYPE_C_RP_NA_1) || (request->type == IEC104_TYPE_C_SC_NA_1);
    if (builtin && (!iec104_asdu_objects_valid(request, request->type == IEC104_TYPE_C_CS_NA_1 ? 7U : 1U) ||
                    (request->count != 1U) || request->sequence))
    {
        server->overloaded = true;
        return true;
    }
    uint32_t ioa = request->payload_len >= 3U ? iec104_read_ioa(request->payload) : 0U;
    bool cancel  = (request->type == IEC104_TYPE_C_SC_NA_1) && (request->cause == IEC104_COT_DEACTIVATION);
    if (((request->cause != IEC104_COT_ACTIVATION) && !cancel) || request->negative)
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_CAUSE, true);
    }
    else if (request->address != server->config.common_address)
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_ADDRESS, true);
    }
    else if (builtin && (request->type != IEC104_TYPE_C_SC_NA_1) && (ioa != 0U) &&
             ((request->type != IEC104_TYPE_C_RP_NA_1) || (ioa != server->config.reset_ioa)))
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_IOA, true);
    }
    else if (request->test)
    {
        /* A test-marked request must never change clock or reboot hardware.
         * This profile explicitly rejects it and echoes the test bit.
         */
        echo_response(server, request, cancel ? IEC104_COT_DEACTIVATION_CON : IEC104_COT_ACTIVATION_CON, true);
    }
    else
    {
        execute_request(server, request);
    }
    return true;
}

bool iec104_server_init(iec104_server_t *server, const iec104_core_config_t *link_config,
                        const iec104_server_config_t *config, const iec104_server_io_t *io)
{
    if ((server == NULL) || (config == NULL) || (io == NULL) || (io->write == NULL) || (config->common_address == 0U) ||
        (config->common_address == UINT16_MAX) || (config->reset_ioa > 0xFFFFFFU) ||
        (config->reset_sbo && ((config->sbo_timeout_ms == 0U) || (config->sbo_timeout_ms > INT32_MAX))))
    {
        return false;
    }

    iec104_core_io_t core_io = {
        .context = server, .write = write_frame, .receive = process_asdu, .event = link_event, .delivered = delivered
    };

    if (!iec104_core_init(&server->core, link_config, &core_io))
    {
        return false;
    }
    server->config                 = *config;
    server->io                     = *io;
    server->response_pending       = false;
    server->reset_state            = IEC104_RESET_NONE;
    server->reset_selected         = false;
    server->overloaded             = false;
    server->initialization_pending = false;
    server->initialization_sent    = false;
    server->query_state            = IEC104_QUERY_IDLE;
    server->query_frame.length     = 0U;
    server->query_more             = false;
    return true;
}

void iec104_server_open(iec104_server_t *server, uint32_t now_ms)
{
    if (server != NULL)
    {
        iec104_core_open(&server->core, now_ms);
        server->reset_selected      = false;
        server->initialization_sent = false;
        server->overloaded          = false;
    }
}

void iec104_server_close(iec104_server_t *server)
{
    if (server != NULL)
    {
        iec104_core_close(&server->core);
    }
}

size_t iec104_server_process_data(iec104_server_t *server, const uint8_t *data, size_t length, uint32_t now_ms)
{
    if ((server == NULL) || (data == NULL))
    {
        return 0U;
    }
    /* Dispatch each complete command before consuming the next one.
     * This allows coalesced commands when the transport/window has room.
     */
    size_t consumed = 0U;
    while ((consumed < length) && (server->core.state != IEC104_CORE_CLOSED))
    {
        size_t needed    = server->core.rx_length < 2U ? 2U - server->core.rx_length :
                                                         (size_t)server->core.rx[1] + 2U - server->core.rx_length;
        size_t available = length - consumed;
        size_t chunk     = needed < available ? needed : available;
        size_t step      = iec104_core_process_data(&server->core, &data[consumed], chunk, now_ms);
        consumed += step;
        if (server->core.rx_length == 0U)
        {
            iec104_server_poll(server, now_ms);
        }
        if (step == 0U)
        {
            break;
        }
    }
    return consumed;
}

/** @brief Send this query's ACT_CON or ACT_TERM with its original QOI/OA.
 * @param[in,out] server Active query; stage advances only on accepted transport.
 * @param[in] cause ACTIVATION_CON or ACTIVATION_TERM.
 */
static void query_control(iec104_server_t *server, uint8_t cause)
{
    iec104_asdu_t asdu = header(server, IEC104_TYPE_C_IC_NA_1, cause, server->query_originator);
    iec104_builder_t builder;
    (void)iec104_builder_begin(&builder, &asdu, 1U);
    (void)iec104_builder_add(&builder, 0U, &server->query_qoi);
    if (iec104_core_send(&server->core, builder.data, builder.length, 0U))
    {
        server->query_state = cause == IEC104_COT_ACTIVATION_CON ?
                                  ((server->query_more || (server->query_frame.length != 0U)) ?
                                       IEC104_QUERY_DATA :
                                       IEC104_QUERY_TERMINATE) :
                                  IEC104_QUERY_IDLE;
    }
}

/** Request at most one continuation chunk; cached bytes are never resampled on backpressure. */
static bool prepare_query_frame(iec104_server_t *server)
{
    if (server->query_frame.length != 0U)
    {
        return true;
    }
    if (!server->query_more)
    {
        server->query_state = IEC104_QUERY_TERMINATE;
        return false;
    }
    iec104_request_t request = { .asdu         = { .type       = IEC104_TYPE_C_IC_NA_1,
                                                   .cause      = IEC104_COT_ACTIVATION,
                                                   .address    = server->config.common_address,
                                                   .originator = server->query_originator,
                                                   .count      = 1U },
                                 .qualifier    = server->query_qoi,
                                 .continuation = true };
    iec104_response_t response;
    response_init(&response, &request);
    if ((server->io.handle_request == NULL) ||
        (server->io.handle_request(server->io.context, &request, &response) != IEC104_REQUEST_ACCEPTED) ||
        !response_valid(&response, &request))
    {
        iec104_core_abort(&server->core);
        return false;
    }
    server->query_more = response.more;
    if (response.asdu.length == 6U)
    {
        if (!response.more)
        {
            server->query_state = IEC104_QUERY_TERMINATE;
        }
        return false;
    }
    server->query_frame = response.asdu;
    return true;
}

static void send_initialization(iec104_server_t *server)
{
    iec104_builder_t builder;
    iec104_asdu_t asdu = header(server, IEC104_TYPE_M_EI_NA_1, IEC104_COT_INITIALIZED, server->config.originator);
    const uint8_t coi  = 0U;
    (void)iec104_builder_begin(&builder, &asdu, 1U);
    (void)iec104_builder_add(&builder, 0U, &coi);
    if (iec104_core_send(&server->core, builder.data, builder.length, 0U))
    {
        server->initialization_sent    = true;
        server->initialization_pending = false;
    }
}

void iec104_server_poll(iec104_server_t *server, uint32_t now_ms)
{
    if (server == NULL)
    {
        return;
    }
    if (server->reset_selected && ((uint32_t)(now_ms - server->reset_selected_since) >= server->config.sbo_timeout_ms))
    {
        server->reset_selected = false;
    }
    iec104_core_poll(&server->core, now_ms);
    if (server->overloaded)
    {
        iec104_core_abort(&server->core);
        return;
    }
    if (server->core.state != IEC104_CORE_STARTED)
    {
        /* STOPPING may wait for old I-frame ACKs; selection ends as soon as STOP is received. */
        server->reset_selected = false;
        return;
    }
    if (server->initialization_pending)
    {
        send_initialization(server);
        return;
    }
    if (server->response_pending)
    {
        if (iec104_core_send(&server->core, server->response.data, server->response.length, 0U))
        {
            if (server->reset_state == IEC104_RESET_SEND_TERMINATION)
            {
                /* Reuse the accepted Execute echo; no new queue or repeated application call. */
                server->reset_state      = IEC104_RESET_SCHEDULE;
                server->response.data[2] = IEC104_COT_ACTIVATION_TERM;
                return;
            }
            server->response_pending = false;
            if (server->reset_state == IEC104_RESET_SCHEDULE)
            {
                server->reset_state = IEC104_RESET_NONE;
                server->io.request_reset(server->io.context);
            }
        }
        return;
    }
    if (server->query_state == IEC104_QUERY_CONFIRM)
    {
        query_control(server, IEC104_COT_ACTIVATION_CON);
    }
    else if ((server->query_state == IEC104_QUERY_DATA) || (server->query_frame.length != 0U))
    {
        if (prepare_query_frame(server) &&
            iec104_core_send(&server->core, server->query_frame.data, server->query_frame.length, 0U))
        {
            server->query_frame.length = 0U;
            if (!server->query_more)
            {
                server->query_state = IEC104_QUERY_TERMINATE;
            }
        }
    }
    else if (server->query_state == IEC104_QUERY_TERMINATE)
    {
        query_control(server, IEC104_COT_ACTIVATION_TERM);
    }
    else
    {
        /* Spontaneous/replay producers may publish while idle. */
    }
}

static bool publish_points(iec104_server_t *server, const iec104_point_t *points, size_t count, uint32_t tag,
                           uint8_t cause)
{
    if ((server == NULL) || (points == NULL) || (count == 0U) || (count > 127U) ||
        (server->query_state != IEC104_QUERY_IDLE) || server->response_pending || server->initialization_pending ||
        (points[0].value_size == 0U) || (points[0].value_size > IEC104_POINT_VALUE_MAX))
    {
        return false;
    }
    iec104_builder_t builder;
    iec104_asdu_t asdu = header(server, points[0].type, cause, server->config.originator);
    (void)iec104_builder_begin(&builder, &asdu, points[0].value_size);
    for (size_t index = 0U; index < count; index++)
    {
        if ((points[index].type != points[0].type) || (points[index].value_size != points[0].value_size) ||
            !iec104_builder_add(&builder, points[index].ioa, points[index].value))
        {
            return false;
        }
    }
    return iec104_server_publish_asdu(server, &builder, tag);
}
bool iec104_server_publish(iec104_server_t *server, const iec104_point_t *points, size_t count, uint32_t tag)
{
    return publish_points(server, points, count, tag, IEC104_COT_SPONTANEOUS);
}

bool iec104_server_publish_periodic(iec104_server_t *server, const iec104_point_t *points, size_t count)
{
    return publish_points(server, points, count, 0U, IEC104_COT_PERIODIC);
}

bool iec104_server_publish_asdu(iec104_server_t *server, const iec104_builder_t *asdu, uint32_t tag)
{
    iec104_asdu_t view;
    if ((server == NULL) || (asdu == NULL) || (server->query_state != IEC104_QUERY_IDLE) || server->response_pending ||
        server->initialization_pending || (asdu->value_size == 0U) || (asdu->value_size > IEC104_CORE_ASDU_MAX - 9U) ||
        !iec104_asdu_parse(&view, asdu->data, asdu->length) || !iec104_asdu_objects_valid(&view, asdu->value_size) ||
        (view.type == 0U) || view.negative || view.test ||
        ((view.cause != IEC104_COT_PERIODIC) && (view.cause != IEC104_COT_SPONTANEOUS)) ||
        (view.address != server->config.common_address) || (view.originator != server->config.originator))
    {
        return false;
    }
    return iec104_core_send(&server->core, asdu->data, asdu->length, tag);
}

/*** end of file ***/
