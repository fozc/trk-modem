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
    if (event == IEC104_CORE_LINK_STARTED)
    {
        server->initialization_pending = !server->initialization_sent;
    }
    else if (event != IEC104_CORE_LINK_STOPPED)
    {
        server->query_state            = IEC104_QUERY_IDLE;
        server->response_pending       = false;
        server->reset_pending          = false;
        server->query_frame.length     = 0U;
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

static const iec104_handler_t *find_handler(const iec104_server_t *server, uint8_t type)
{
    for (size_t index = 0U; index < server->config.handler_count; index++)
    {
        if (server->config.handlers[index].type == type)
        {
            return &server->config.handlers[index];
        }
    }
    return NULL;
}

/** @brief Accept a new GI transaction or prepare a negative confirmation.
 * @param[in,out] server Query state and pending response storage.
 * @param[in] request Validated single-object GI; its OA belongs to this query.
 * @note Captures QOI/OA/cursor only. Actual data and CON/TERM are sent by poll;
 * a second GI never replaces an active query.
 */
static void handle_gi(iec104_server_t *server, const iec104_asdu_t *request)
{
    uint8_t qoi = request->payload[3];
    if ((qoi < IEC104_QOI_STATION) || (qoi > IEC104_QOI_GROUP_4) || (server->query_state != IEC104_QUERY_IDLE))
    {
        echo_response(server, request, IEC104_COT_ACTIVATION_CON, true);
        return;
    }
    server->query_qoi          = qoi;
    server->query_originator   = request->originator;
    server->query_index        = 0U;
    server->query_frame.length = 0U;
    server->query_state        = IEC104_QUERY_CONFIRM;
}

/** @brief Execute a validated command once and stage its response/query state.
 * @param[in,out] server Product callbacks and response/query storage.
 * @param[in] request Already checked for address/cause/extent/test-bit policy.
 * @param[in] handler Matching extension, or NULL for builtin commands.
 * @note Clock writes run here once; reset is only scheduled after positive
 * response acceptance in poll. Transport retries never reexecute the command.
 */
static void execute_command(iec104_server_t *server, const iec104_asdu_t *request, const iec104_handler_t *handler)
{
    /* A faulty extension must not resend a previous command's response. */
    server->response.length = 0U;
    if (request->type == IEC104_TYPE_C_IC_NA_1)
    {
        handle_gi(server, request);
    }
    else if (request->type == IEC104_TYPE_C_CS_NA_1)
    {
        bool success = iec104_cp56_time_valid(&request->payload[3]) && (server->io.set_clock != NULL) &&
                       server->io.set_clock(server->io.context, &request->payload[3]);
        echo_response(server, request, IEC104_COT_ACTIVATION_CON, !success);
    }
    else if (request->type == IEC104_TYPE_C_RP_NA_1)
    {
        bool success = (request->payload[3] == IEC104_RESET_PROCESS_GENERAL) && (server->io.request_reset != NULL);
        echo_response(server, request, IEC104_COT_ACTIVATION_CON, !success);
        server->reset_pending = success;
    }
    else if ((handler != NULL) && handler->handle(server->io.context, request, &server->response))
    {
        iec104_asdu_t response;
        if (iec104_asdu_parse(&response, server->response.data, server->response.length))
        {
            server->response_pending = true;
        }
        else
        {
            server->overloaded = true;
        }
    }
    else
    {
        echo_response(server, request, IEC104_COT_ACTIVATION_CON, true);
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
 * Type 100 (GI):    [0..2:IOA=0]           [3:QOI=20..24]
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
    const iec104_handler_t *handler = find_handler(server, request->type);
    bool builtin = (request->type == IEC104_TYPE_C_IC_NA_1) || (request->type == IEC104_TYPE_C_CS_NA_1) ||
                   (request->type == IEC104_TYPE_C_RP_NA_1);
    if (!builtin && (handler == NULL))
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_TYPE, true);
        return true;
    }
    size_t value_size = builtin ? (request->type == IEC104_TYPE_C_CS_NA_1 ? 7U : 1U) : handler->value_size;
    if (!iec104_asdu_objects_valid(request, value_size) || (builtin && ((request->count != 1U) || request->sequence)))
    {
        server->overloaded = true;
        return true;
    }
    uint32_t ioa = iec104_read_ioa(request->payload);
    if ((request->cause != IEC104_COT_ACTIVATION) || request->negative)
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_CAUSE, true);
    }
    else if (request->address != server->config.common_address)
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_ADDRESS, true);
    }
    else if (builtin && (ioa != 0U) && ((request->type != IEC104_TYPE_C_RP_NA_1) || (ioa != server->config.reset_ioa)))
    {
        echo_response(server, request, IEC104_COT_UNKNOWN_IOA, true);
    }
    else if (request->test)
    {
        /* A test-marked request must never change clock or reboot hardware.
         * This profile explicitly rejects it and echoes the test bit.
         */
        echo_response(server, request, IEC104_COT_ACTIVATION_CON, true);
    }
    else
    {
        execute_command(server, request, handler);
    }
    return true;
}

bool iec104_server_init(iec104_server_t *server, const iec104_core_config_t *link_config,
                        const iec104_server_config_t *config, const iec104_server_io_t *io)
{
    if ((server == NULL) || (config == NULL) || (io == NULL) || (io->write == NULL) || (io->read_point == NULL) ||
        (config->common_address == 0U) || (config->common_address == UINT16_MAX) || (config->reset_ioa > 0xFFFFFFU) ||
        (config->point_limit == 0U) || ((config->handler_count != 0U) && (config->handlers == NULL)))
    {
        return false;
    }

    for (size_t index = 0U; index < config->handler_count; index++)
    {
        const iec104_handler_t *handler = &config->handlers[index];
        if ((handler->handle == NULL) || (handler->type == IEC104_TYPE_C_IC_NA_1) ||
            (handler->type == IEC104_TYPE_C_CS_NA_1) || (handler->type == IEC104_TYPE_C_RP_NA_1) ||
            (handler->value_size > IEC104_SERVER_VALUE_MAX))
        {
            return false;
        }
        for (size_t previous = 0U; previous < index; previous++)
        {
            if (config->handlers[previous].type == handler->type)
            {
                return false;
            }
        }
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
    server->reset_pending          = false;
    server->overloaded             = false;
    server->initialization_pending = false;
    server->initialization_sent    = false;
    server->query_state            = IEC104_QUERY_IDLE;
    server->query_frame.length     = 0U;
    return true;
}

void iec104_server_open(iec104_server_t *server, uint32_t now_ms)
{
    if (server != NULL)
    {
        iec104_core_open(&server->core, now_ms);
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
        server->query_state = cause == IEC104_COT_ACTIVATION_CON ? IEC104_QUERY_DATA : IEC104_QUERY_IDLE;
    }
}

/** @brief Build or reuse one pending same-type GI data ASDU.
 * @param[in,out] server Provider, read cursor and cached ASDU buffer.
 * @return True when cached data is ready to send; false for no data yet/end/error.
 * @note Reads at most 32 positions, preserves a prepared frame on TX rejection,
 * and aborts on provider errors instead of claiming successful termination.
 */
static bool prepare_query_frame(iec104_server_t *server)
{
    if (server->query_frame.length != 0U)
    {
        return true;
    }
    for (size_t work = 0U; work < 32U; work++)
    {
        iec104_point_t point         = { 0 };
        iec104_point_result_t result = server->io.read_point(server->io.context, server->query_qoi, server->query_index,
                                                             &point);
        if (result == IEC104_POINT_END)
        {
            server->query_state = IEC104_QUERY_TERMINATE;
            break;
        }
        if ((result == IEC104_POINT_ERROR) || (server->query_index >= server->config.point_limit))
        {
            iec104_core_abort(&server->core);
            return false;
        }
        if (result == IEC104_POINT_SKIP)
        {
            server->query_index++;
            continue;
        }
        if ((result != IEC104_POINT_VALUE) || (point.value_size > IEC104_SERVER_VALUE_MAX) ||
            (point.value_size == 0U) || (point.ioa > 0xFFFFFFU))
        {
            iec104_core_abort(&server->core);
            return false;
        }
        if (server->query_frame.length == 0U)
        {
            iec104_asdu_t asdu = header(server, point.type, server->query_qoi, server->query_originator);
            (void)iec104_builder_begin(&server->query_frame, &asdu, point.value_size);
        }
        if ((point.type != server->query_frame.data[0]) || (point.value_size != server->query_frame.value_size) ||
            !iec104_builder_add(&server->query_frame, point.ioa, point.value))
        {
            break;
        }
        server->query_index++;
    }
    return server->query_frame.length != 0U;
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
    iec104_core_poll(&server->core, now_ms);
    if (server->overloaded)
    {
        iec104_core_abort(&server->core);
        return;
    }
    if (server->core.state != IEC104_CORE_STARTED)
    {
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
            server->response_pending = false;
            if (server->reset_pending)
            {
                server->reset_pending = false;
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

bool iec104_server_publish(iec104_server_t *server, const iec104_point_t *points, size_t count, uint32_t tag)
{
    if ((server == NULL) || (points == NULL) || (count == 0U) || (count > 127U) ||
        (server->query_state != IEC104_QUERY_IDLE) || server->response_pending || server->initialization_pending ||
        (points[0].value_size == 0U) || (points[0].value_size > IEC104_SERVER_VALUE_MAX))
    {
        return false;
    }
    iec104_builder_t builder;
    iec104_asdu_t asdu = header(server, points[0].type, IEC104_COT_SPONTANEOUS, server->config.originator);
    (void)iec104_builder_begin(&builder, &asdu, points[0].value_size);
    for (size_t index = 0U; index < count; index++)
    {
        if ((points[index].type != points[0].type) || (points[index].value_size != points[0].value_size) ||
            !iec104_builder_add(&builder, points[index].ioa, points[index].value))
        {
            return false;
        }
    }
    return iec104_core_send(&server->core, builder.data, builder.length, tag);
}
/*** end of file ***/
