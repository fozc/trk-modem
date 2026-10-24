/*
 * gsm_sms.c
 *
 *  Created on: Oct 24, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Parse received text-mode SMS and log through the received callback.
 */

#define CSLOG_MODULE LOG_MOD_GSM
#include "gsm_sms.h"
#include "console_logger.h"

#include <limits.h>
#include <string.h>

static const uint8_t *find_line_end(const uint8_t *data, size_t length)
{
    for (size_t index = 1U; index < length; index++)
    {
        if ((data[index - 1U] == '\r') && (data[index] == '\n'))
        {
            return data + index - 1U;
        }
    }
    return NULL;
}

void gsm_sms_received_callback(const uint8_t *sender, size_t sender_len,
                               const uint8_t *message, size_t message_len)
{
    if ((sender == NULL) || (message == NULL) ||
        (sender_len > (size_t)INT_MAX) ||
        (message_len > (size_t)INT_MAX))
    {
        return;
    }
    CSLOG("SMS received from %.*s: %.*s\r\n",
          (int)sender_len, sender, (int)message_len, message);
}

bool gsm_sms_handle_response(const uint8_t *response, size_t response_len,
                             bool *is_unread)
{
    static const char prefix[] = "+CMGR:";
    static const char footer[] = "\r\nOK\r\n";
    if ((response == NULL) || (is_unread == NULL) ||
        (response_len < sizeof(prefix) + sizeof(footer)))
    {
        return false;
    }
    const uint8_t *header = response;
    while (((size_t)(header - response) < response_len) &&
           ((*header == '\r') || (*header == '\n')))
    {
        header++;
    }
    const size_t remaining = response_len - (size_t)(header - response);
    if ((remaining < sizeof(prefix) - 1U) ||
        (memcmp(header, prefix, sizeof(prefix) - 1U) != 0))
    {
        return false;
    }
    const uint8_t *header_end = find_line_end(header, remaining);
    const uint8_t *end = response + response_len;
    if ((header_end == NULL) ||
        (memcmp(end - (sizeof(footer) - 1U), footer,
                sizeof(footer) - 1U) != 0))
    {
        return false;
    }
    const uint8_t *status = memchr(header, '"',
                                   (size_t)(header_end - header));
    if (status == NULL)
    {
        return false;
    }
    status++;
    const uint8_t *status_end = memchr(status, '"',
                                       (size_t)(header_end - status));
    const bool unread = (status_end != NULL) &&
        ((size_t)(status_end - status) == 10U) &&
        (memcmp(status, "REC UNREAD", 10U) == 0);
    const bool read = (status_end != NULL) &&
        ((size_t)(status_end - status) == 8U) &&
        (memcmp(status, "REC READ", 8U) == 0);
    if ((!unread && !read) || (status_end + 1U >= header_end) ||
        (status_end[1] != ','))
    {
        return false;
    }
    const uint8_t *sender = memchr(status_end + 2U, '"',
        (size_t)(header_end - status_end - 2));
    if (sender == NULL)
    {
        return false;
    }
    sender++;
    const uint8_t *sender_end = memchr(sender, '"',
                                       (size_t)(header_end - sender));
    const uint8_t *message = header_end + 2U;
    const uint8_t *message_end = end - (sizeof(footer) - 1U);
    if ((sender_end == NULL) || (message > message_end))
    {
        return false;
    }
    *is_unread = unread;
    gsm_sms_received_callback(sender, (size_t)(sender_end - sender),
                              message, (size_t)(message_end - message));
    return true;
}

/*** end of file ***/
