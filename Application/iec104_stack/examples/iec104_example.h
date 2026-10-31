/**
 * @file iec104_example.h
 * @brief Optional application callback example; never enabled by the modem firmware.
 * Created on: Oct 30, 2026
 * Author: Fatih Ozcan
 *         fatihozcan@gmail.com
 */
#ifndef IEC104_EXAMPLE_H
#define IEC104_EXAMPLE_H

#include "iec104_server.h"

typedef struct
{
    size_t query_index; /**< Application continuation position, not library state. */
    bool status;
    uint8_t double_state;
    float measurement;
    uint8_t quality;
    uint32_t bits;
    iec104_time_t time;
} iec104_example_data_t;

/** @brief Handle requests and GI continuation through one application entry point.
 * Unsupported requests/groups return UNSUPPORTED; server creates their error replies.
 */
iec104_request_result_t iec104_example_handle_request(void *context, const iec104_request_t *request,
                                                      iec104_response_t *response);
/** @brief Extension encoder example for Type 7, a 32-bit bitstring plus QDS.
 * @note For real use, promote the encoder to the library with its type-specific tests.
 */
bool iec104_example_set_bitstring(iec104_point_t *point, uint32_t ioa, uint32_t bits, uint8_t quality);

/** @brief Example periodic content entry; call from the application's existing interval timer.
 * @return True when the selected measurement batch enters transport; false on backpressure.
 */
bool iec104_example_publish_periodic(iec104_server_t *server, const iec104_example_data_t *data);

#endif
/*** end of file ***/
