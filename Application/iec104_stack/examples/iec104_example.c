/**
 * @file iec104_example.c
 * @brief Optional GI/IOA example using all currently typed encoders and one extension.
 * Created on: Oct 30, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#include "iec104_example.h"

/** Example addresses only; replace with the target product's mapping. */
typedef enum
{
    EXAMPLE_SINGLE       = 100U,
    EXAMPLE_FLOAT        = 101U,
    EXAMPLE_TIMED_SINGLE = 102U,
    EXAMPLE_TIMED_DOUBLE = 103U,
    EXAMPLE_TIMED_FLOAT  = 104U,
    EXAMPLE_BITSTRING    = 105U
} example_ioa_t;

bool iec104_example_set_bitstring(iec104_point_t *point, uint32_t ioa, uint32_t bits, uint8_t quality)
{
    if ((quality & 0x0EU) != 0U)
    {
        return false;
    }
    /* A new type encoder owns its wire layout; the application passes host values. */
    uint8_t value[] = { (uint8_t)bits, (uint8_t)(bits >> 8U), (uint8_t)(bits >> 16U), (uint8_t)(bits >> 24U), quality };
    return iec104_point_set_encoded(point, ioa, IEC104_TYPE_M_BO_NA_1, value, sizeof(value));
}

static bool read_point(void *context, uint8_t qoi, uint32_t ioa, iec104_point_t *point)
{
    (void)qoi;
    const iec104_example_data_t *data = context;
    if (data == NULL)
    {
        return false;
    }
    switch (ioa)
    {
        case EXAMPLE_SINGLE:
            return iec104_point_set_single(point, ioa, data->status, data->quality, NULL);
        case EXAMPLE_FLOAT:
            return iec104_point_set_float(point, ioa, data->measurement, data->quality, NULL);
        case EXAMPLE_TIMED_SINGLE:
            return iec104_point_set_single(point, ioa, data->status, data->quality, &data->time);
        case EXAMPLE_TIMED_DOUBLE:
            return iec104_point_set_double(point, ioa, data->double_state, data->quality, &data->time);
        case EXAMPLE_TIMED_FLOAT:
            return iec104_point_set_float(point, ioa, data->measurement, data->quality, &data->time);
        case EXAMPLE_BITSTRING:
            return iec104_example_set_bitstring(point, ioa, data->bits, data->quality);
        default:
            return false;
    }
}

/** Application owns GI lists and progress; the library receives only ready response chunks. */
iec104_request_result_t iec104_example_handle_request(void *context, const iec104_request_t *request,
                                                      iec104_response_t *response)
{
    iec104_example_data_t *data     = context;
    static const uint32_t station[] = { EXAMPLE_SINGLE, EXAMPLE_FLOAT, EXAMPLE_BITSTRING };
    static const uint32_t group1[]  = { EXAMPLE_TIMED_SINGLE, EXAMPLE_TIMED_DOUBLE, EXAMPLE_TIMED_FLOAT };
    if ((data == NULL) || (request == NULL) || (response == NULL))
    {
        return IEC104_REQUEST_ERROR;
    }
    if ((request->asdu.type != IEC104_TYPE_C_IC_NA_1) ||
        ((request->qualifier != IEC104_QOI_STATION) && (request->qualifier != IEC104_QOI_GROUP_1)))
    {
        return IEC104_REQUEST_UNSUPPORTED;
    }
    response->more = true;
    if (!request->continuation)
    {
        data->query_index = 0U;
        return IEC104_REQUEST_ACCEPTED;
    }
    const uint32_t *ioas = request->qualifier == IEC104_QOI_STATION ? station : group1;
    while (data->query_index < sizeof(station) / sizeof(station[0]))
    {
        iec104_point_t point;
        if (!read_point(data, request->qualifier, ioas[data->query_index], &point))
        {
            return IEC104_REQUEST_ERROR;
        }
        if (!iec104_response_add(response, &point))
        {
            return response->asdu.length == 6U ? IEC104_REQUEST_ERROR : IEC104_REQUEST_ACCEPTED;
        }
        data->query_index++;
    }
    response->more = false;
    return IEC104_REQUEST_ACCEPTED;
}

bool iec104_example_publish_periodic(iec104_server_t *server, const iec104_example_data_t *data)
{
    if (data == NULL)
    {
        return false;
    }
    iec104_point_t point;
    return iec104_point_set_float(&point, EXAMPLE_FLOAT, data->measurement, data->quality, NULL) &&
           iec104_server_publish_periodic(server, &point, 1U);
}

/*** end of file ***/
