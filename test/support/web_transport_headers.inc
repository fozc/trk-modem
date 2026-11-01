/*
 * web_transport_headers.inc
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Migrated host scenarios and existing stateful hardware boundaries.
 */

#ifndef HTTP_TRANSPORT_TEST_H
#define HTTP_TRANSPORT_TEST_H
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_server.h"
#include "http_handlers.h"
#include "http_response.h"
#include "sha256.h"
#include "web_server.h"
#include "raw_tcp_fw_update.h"
#include "index_html.h"
#include "fw_update_html.h"
#include "loading_html.h"
#include "ring_buff.h"
#include "xprintf.h"
#define CSLOG(...) ((void)0)
#define CSLOG_ERR(...) ((void)0)
#define CSLOG_WARN(...) ((void)0)
#define CSLOG_NODT(...) ((void)0)
#define CCSLOG(...) ((void)0)
#include "gsm_engine.h"
#include "gsm_listener_process.h"
#include "gsm_http_server.h"
#include "reboot.h"
#endif

/*** end of file ***/
