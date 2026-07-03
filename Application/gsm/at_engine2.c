/*
 * at_engine2.c
 *
 *  Created on: Apr 11, 2026
 *      Author: fatih
 */
#include "at_engine2.h"
#include "bsp.h"
#include "ring_buff.h"
#include <string.h>
#include "utils.h"
#include "gsm_process.h"
#include "uart.h"
#include "main.h"
#include "gsm_log.h"

typedef enum
{
	AT_ENGINE_IDLE,
	AT_ENGINE_URC_INCOMING,
	AT_ENGINE_SEND_CMD,
	AT_ENGINE_WAIT_RESPONSE,
	AT_ENGINE_DONE
} at_engine_state_t;

#define AT_ENGINE_RX_BUFFER_SIZE  2048U
#define AT_ENGINE_MAX_CMD_LEN     128U
#define AT_ENGINE_RESPONSE_BUFFER_SIZE 1280U
#define AT_ENGINE_URC_BUFFER_SIZE 128U

/* Standard AT error responses to detect */
static const char AT_ERROR_STR[]     = "\r\nERROR\r\n";
static const char AT_NO_SIM_CARD_STR[] = "\r\n+CME ERROR: 10\r\n";//
static const char AT_CME_ERROR_STR[] = "+CME ERROR:";

typedef struct
{
	uint8_t  cmd[AT_ENGINE_MAX_CMD_LEN];
	uint16_t cmd_len;
	uint8_t  expected_response[AT_ENGINE_MAX_CMD_LEN];
	uint8_t  expected_response_len;
	uint8_t  response_buffer[AT_ENGINE_RESPONSE_BUFFER_SIZE];
	uint16_t response_buffer_len;
	const uint8_t *data_ptr;
	uint16_t data_len;
	bool     is_data_mode;
	uint8_t  retry_count;
	at_engine_state_t state;
	uint16_t binary_bytes_remaining; /* >0: consuming binary #SRECV payload, skip terminator checks */
	uint16_t srecv_payload_end;      /* buffer offset one past the last #SRECV payload byte (0 = none) */
	uint32_t timeout;
	uint32_t send_time;
	at_engine_result_t result;
	uint32_t urc_timeout;
} at_engine_t;

static uint8_t rx_buffer[AT_ENGINE_RX_BUFFER_SIZE] = {0};
static at_engine_t at_engine = {0};
static rbuff_t rx_ringbuf;

static void log_response(const char *p_result_str);

/* TX state for non-blocking (interrupt-driven) send */
static volatile uint16_t s_tx_remaining = 0U;
static volatile const uint8_t *s_tx_ptr = NULL;
static volatile uint8_t s_tx_done_flag = 0U;


void at_engine_set_log(bool is_enabled)
{
	if (is_enabled)
	{
		gsm_log_set_level(GSM_LOG_VERBOSE);
	}
	else
	{
		gsm_log_set_level(GSM_LOG_OFF);
	}
}

bool at_engine_get_log(void)
{
	return gsm_log_get_level() >= GSM_LOG_VERBOSE;
}

void at_engine_rx_byte(uint8_t byte)
{
	rbuff_write_byte(&rx_ringbuf, byte);
}

void at_engine_send_with_dma(const uint8_t *p_data, uint16_t len)
{
	extern UART_HandleTypeDef huart1;
	/* Set flag BEFORE starting DMA to avoid race with the TC interrupt:
	 * if TC fires between HAL_UART_Transmit_DMA() and the flag assignment
	 * the callback clears it to 0, then main overwrites with 1 — stuck. */
	s_tx_done_flag = 1U;
	HAL_StatusTypeDef res = HAL_UART_Transmit_DMA(&huart1, p_data, len);

	if(res != HAL_OK)
	{
		s_tx_done_flag = 0U; /* Failed to start DMA, allow retry */
		GSM_LOG_ERR("AT [DMA TX ERROR]\r\n");
	}
}

static void send_at_command(const uint8_t *p_cmd, uint16_t cmd_len)
{
	uart_send_buffer(UART_1, p_cmd, (int)cmd_len);
}

void at_engine_init(void)
{

	rbuff_init(&rx_ringbuf, rx_buffer, AT_ENGINE_RX_BUFFER_SIZE);
}


/* true: busy processing a command or waiting for response
 * false: idle and ready for new command
*/
bool at_engine_is_busy(void)
{
	return (at_engine.state != AT_ENGINE_IDLE) &&
	       (at_engine.state != AT_ENGINE_DONE);
}


static bool at_engine_send_cmd(const uint8_t *p_cmd, uint16_t cmd_len,
	const uint8_t *p_data, uint16_t data_len, bool is_data_mode,
	const uint8_t *p_expected, uint8_t expected_len,
	uint8_t retry_count, uint32_t timeout)
{
	if(at_engine_is_busy())
	{
		return false;
	}

	if(p_cmd != NULL && cmd_len > 0U)
	{
		if(cmd_len > AT_ENGINE_MAX_CMD_LEN)
		{
			return false;
		}
		memcpy(at_engine.cmd, p_cmd, cmd_len);
		at_engine.cmd_len = cmd_len;
	}
	else
	{
		at_engine.cmd_len = 0U;
	}

	if(p_data != NULL && data_len > 0U)
	{
		at_engine.is_data_mode = true;
		at_engine.data_ptr = p_data;
		at_engine.data_len = data_len;
	}
	else
	{
		at_engine.is_data_mode = false;
		at_engine.data_ptr = NULL;
		at_engine.data_len = 0U;
	}

	if(p_expected != NULL && expected_len > 0U)
	{
		if(expected_len > AT_ENGINE_MAX_CMD_LEN)
		{
			return false;
		}
		memcpy(at_engine.expected_response, p_expected, expected_len);
		at_engine.expected_response_len = expected_len;
	}
	else
	{
		at_engine.expected_response_len = 0U;
	}

	at_engine.timeout = timeout;
	at_engine.retry_count = retry_count;
	at_engine.response_buffer_len = 0U;
	at_engine.result = AT_ENGINE_RESULT_NONE;
	at_engine.state = AT_ENGINE_SEND_CMD;

	return true;
}

bool at_engine_send_at_command(const void *p_cmd, uint16_t cmd_len,
	const void *p_expected, uint8_t expected_len,
	uint8_t retry_count, uint32_t timeout)
{
	if((p_cmd == NULL) || (cmd_len == 0U))
	{
		return false;
	}

	if(cmd_len + 1U > AT_ENGINE_MAX_CMD_LEN)
	{
		return false;
	}

	return at_engine_send_cmd(p_cmd, cmd_len,
							NULL, 0U, false,
							(const uint8_t *)p_expected, expected_len,
							retry_count, timeout);
}

bool at_engine_send_data(const uint8_t *p_data, uint16_t data_len, uint32_t timeout)
{
	/* Adjust timeout for TX duration at 9600 bps */
	uint32_t adjusted_timeout = timeout + (uint32_t)(0.0868f * (float)data_len);

	return at_engine_send_cmd(NULL, 0U,
		p_data, data_len, true,
		(const uint8_t *)"OK\r\n", 4U,
		1U, adjusted_timeout);
}

static void at_engine_send_process(void)
{
	if(at_engine.retry_count == 0U)
	{
		at_engine.state = AT_ENGINE_DONE;
		at_engine.result = AT_ENGINE_RESULT_TIMEOUT;
		log_response("TIMEOUT");
		return;
	}

	if(s_tx_done_flag){
		return;     /* Previous transmission still in progress, wait for completion before sending next */
	}

	if(at_engine.is_data_mode)
	{
		/* data mode TX — no log */
		at_engine_send_with_dma(at_engine.data_ptr, at_engine.data_len);
		//send_at_command(at_engine.cmd, at_engine.cmd_len);
	}
	else
	{
		GSM_LOG_INF_C(XCOLOR_GREEN, "AT TX> %.*s\r\n",
		              (int)at_engine.cmd_len, at_engine.cmd);

		at_engine_send_with_dma(at_engine.cmd, at_engine.cmd_len);
		//send_at_command(at_engine.cmd, at_engine.cmd_len);
	}

	at_engine.send_time = bsp_get_tick();
	at_engine.retry_count--;
	at_engine.response_buffer_len = 0U; /* Clear buffer for fresh response */
	at_engine.binary_bytes_remaining = 0U; /* Exit any residual binary SRECV mode */
	at_engine.srecv_payload_end = 0U;      /* No SRECV payload boundary yet */
	at_engine.state = AT_ENGINE_WAIT_RESPONSE;
}

static bool at_engine_check_timeout(void)
{
	uint32_t now = bsp_get_tick();
	return (now - at_engine.send_time) >= at_engine.timeout;
}

/**
 * @brief Remove a range [start, end) from the response buffer and shift remaining data left.
 */
static void response_buffer_remove(uint16_t start, uint16_t end)
{
	if(end > at_engine.response_buffer_len)
	{
		end = at_engine.response_buffer_len;
	}
	if(start >= end)
	{
		return;
	}

	uint16_t remove_len = end - start;
	uint16_t remaining  = at_engine.response_buffer_len - end;

	if(remaining > 0U)
	{
		memmove(&at_engine.response_buffer[start],
		        &at_engine.response_buffer[end],
		        remaining);
	}
	at_engine.response_buffer_len -= remove_len;
	at_engine.response_buffer[at_engine.response_buffer_len] = '\0';
}

/**
 * @brief Check for URC patterns in the response buffer.
 *        If found, extract the URC line, forward to callback, and remove from buffer.
 *
 * @return true if a URC was found and processed, false otherwise.
 */
static bool check_urc_in_response(void)
{
	static const char *urc_table[] = {
		"SRING:",
		"NO CARRIER",
		"#HTTPRING:",
		"#SL: ABORTED",
		"+CGEV:",
		"#NITZ:",
		"+CUSD:",
	};
	static const size_t urc_table_size = sizeof(urc_table) / sizeof(urc_table[0]);

	static uint8_t urc_buffer[AT_ENGINE_URC_BUFFER_SIZE];

	const char *p_buf = (const char *)at_engine.response_buffer;

	for(size_t i = 0U; i < urc_table_size; i++)
	{
		/* Find URC keyword in response buffer */
		int32_t urc_start = str_index_of(p_buf, urc_table[i]);
		if(urc_start < 0)
		{
			continue;
		}

		/* Find the end of the URC line: search for \r\n AFTER the keyword */
		uint16_t keyword_len = (uint16_t)strlen(urc_table[i]);
		uint16_t after_keyword = (uint16_t)urc_start + keyword_len;

		if(after_keyword >= at_engine.response_buffer_len)
		{
			/* Keyword found but line not yet complete — wait for more data */
			return false;
		}

		int32_t line_end_offset = str_index_of(&p_buf[after_keyword], "\r\n");
		uint16_t urc_end;

		if(line_end_offset >= 0)
		{
			/* Include the trailing \r\n in the removal range */
			urc_end = after_keyword + (uint16_t)line_end_offset + 2U;
		}
		else
		{
			/* No trailing \r\n yet — URC line incomplete, wait for more data */
			return false;
		}

		/* Extract URC content (skip leading \r\n, include rest of line) */
		uint16_t content_start = (uint16_t)urc_start;
		uint16_t content_len   = urc_end - content_start - 2U; /* exclude trailing \r\n */

		if(content_len > AT_ENGINE_URC_BUFFER_SIZE - 1U)
		{
			content_len = AT_ENGINE_URC_BUFFER_SIZE - 1U;
		}

		memcpy(urc_buffer, &at_engine.response_buffer[content_start], content_len);
		urc_buffer[content_len] = '\0';

		GSM_LOG_INF_C(XCOLOR_YELLOW, "AT URC: %.*s\r\n",
		              (int)content_len, urc_buffer);

		gsm_URC_callback(urc_buffer, content_len);

		/* Remove URC from response buffer.
		 * Also include the preceding \r\n separator if present so that no
		 * whitespace residue remains in the buffer after extraction.  Without
		 * this, at_engine_urc_incoming_process() would never see
		 * response_buffer_len == 0 and the engine would block in
		 * AT_ENGINE_URC_INCOMING for the full 300 ms timeout, causing
		 * at_engine_is_busy() == true and dropping subsequent AT commands
		 * (e.g. AT#SRECV triggered by the very SRING just dispatched). */
		uint16_t remove_from = (uint16_t)urc_start;
		if((remove_from >= 2U) &&
		   (at_engine.response_buffer[remove_from - 2U] == '\r') &&
		   (at_engine.response_buffer[remove_from - 1U] == '\n'))
		{
			remove_from -= 2U;
		}
		response_buffer_remove(remove_from, urc_end);

		return true;
	}

	return false;
}

/**
 * @brief Log the response buffer content with escaped control characters.
 */
static void log_response(const char *p_result_str)
{
	if (gsm_log_get_level() < GSM_LOG_VERBOSE)
	{
		return;
	}
	const char *p_color = (strcmp(p_result_str, "OK") == 0) ? XCOLOR_CYAN : XCOLOR_RED;
	CCSLOG(p_color, "AT RX< [%s] %u bytes [", p_result_str, at_engine.response_buffer_len);
	for(uint16_t i = 0U; i < at_engine.response_buffer_len; i++)
	{
		uint8_t c = at_engine.response_buffer[i];
		if(c == '\r')      { CSLOG_NODT("\\r"); }
		else if(c == '\n') { CSLOG_NODT("\\n"); }
		else if(c >= 0x20U && c < 0x7FU) { CSLOG_NODT("%c", c); }
		else { CSLOG_NODT("."); }
	}
	CSLOG_NODT("]\r\n");
}

/**
 * @brief Check if the response buffer ends with the expected response or an error.
 */
static void check_response_complete(void)
{
	uint16_t buf_len = at_engine.response_buffer_len;

	/* Check for expected response (typically "OK\r\n") */
	if(at_engine.expected_response_len > 0U && buf_len >= at_engine.expected_response_len)
	{
		const uint8_t *p_tail = &at_engine.response_buffer[buf_len - at_engine.expected_response_len];
		if(memcmp(p_tail, at_engine.expected_response, at_engine.expected_response_len) == 0)
		{
			/* Ignore a terminator that starts inside the declared #SRECV payload.
			 * srecv_payload_end is the offset one past the last payload byte
			 * (set when "#SRECV: N,LEN" was parsed).  An "OK\r\n" whose start lies
			 * before it is a byte pattern within the binary payload (e.g. data
			 * ending in "OK" followed by the modem's own "\r\n" framing), not the
			 * real end of response. */
			uint16_t ok_pos = buf_len - at_engine.expected_response_len;
			if((at_engine.srecv_payload_end > 0U) && (ok_pos < at_engine.srecv_payload_end))
			{
				return; /* Terminator pattern inside SRECV payload -- keep reading */
			}

			at_engine.state = AT_ENGINE_DONE;
			at_engine.result = AT_ENGINE_RESULT_OK;
			log_response("OK");
			return;
		}
	}

	/* Check for ERROR response */
	if(buf_len >= (sizeof(AT_ERROR_STR) - 1U))
	{
		const uint8_t *p_tail = &at_engine.response_buffer[buf_len - (sizeof(AT_ERROR_STR) - 1U)];
		if(memcmp(p_tail, AT_ERROR_STR, sizeof(AT_ERROR_STR) - 1U) == 0)
		{
			uint16_t err_pos = buf_len - (uint16_t)(sizeof(AT_ERROR_STR) - 1U);
			if((at_engine.srecv_payload_end > 0U) && (err_pos < at_engine.srecv_payload_end))
			{
				return; /* Pattern inside SRECV payload -- keep reading */
			}
			at_engine.state = AT_ENGINE_DONE;
			at_engine.result = AT_ENGINE_RESULT_ERROR;
			log_response("ERROR");
			return;
		}
	}

	/* Check for +CME ERROR: 10 (NO SIM) — must precede generic CME check */
	if(buf_len >= (sizeof(AT_NO_SIM_CARD_STR) - 1U))
	{
		const uint8_t *p_tail = &at_engine.response_buffer[buf_len - (sizeof(AT_NO_SIM_CARD_STR) - 1U)];
		if(memcmp(p_tail, AT_NO_SIM_CARD_STR, sizeof(AT_NO_SIM_CARD_STR) - 1U) == 0)
		{
			/* Exempt AT+CPIN? and AT#NITZ — these legitimately receive CME ERROR: 10 */
			const char *p_cmd = (const char *)at_engine.cmd;
			if((str_index_of(p_cmd, "AT+CPIN?") < 0) &&
			   (str_index_of(p_cmd, "AT#NITZ") < 0))
			{
				at_engine.state = AT_ENGINE_DONE;
				at_engine.result = AT_ENGINE_RESULT_NO_SIM;
				log_response("NO SIM (CME ERROR: 10)");
				return;
			}
		}
	}

	/* Check for +CME ERROR: anywhere in buffer */
	if(str_index_of((const char *)at_engine.response_buffer, AT_CME_ERROR_STR) >= 0)
	{
		/* +CME ERROR lines end with \r\n — check if line is complete */
		if(buf_len >= 2U &&
		   at_engine.response_buffer[buf_len - 2U] == '\r' &&
		   at_engine.response_buffer[buf_len - 1U] == '\n')
		{
			at_engine.state = AT_ENGINE_DONE;
			at_engine.result = AT_ENGINE_RESULT_ERROR;
			log_response("CME ERROR");
		}
	}
}

/* ===========================================================================
 * AT#SRECV binary reception -- why the extra bookkeeping below exists
 * ===========================================================================
 *
 * HOW THE DATA ARRIVES
 * --------------------
 * We read TCP socket data from the modem with "AT#SRECV=<sock>,<n>".  The modem
 * answers with a text header, then <LEN> RAW bytes of socket payload, then its
 * own text framing:
 *
 *     \r\n#SRECV: 1,1024\r\n <1024 raw payload bytes> \r\n\r\nOK\r\n
 *     |<---- header ----->| |<---- binary payload --->||<- framing ->|
 *
 * The payload is arbitrary BINARY (here: encrypted firmware chunks delivered
 * inside an HTTP POST body).  It may contain any byte value: 0x00, \r, \n, and
 * even the ASCII letters "OK".
 *
 * THE PROBLEM (a real field bug)
 * ------------------------------
 * Normally we detect end-of-response by tail-matching the buffer against the
 * expected terminator "OK\r\n".  But because the payload is binary, this can
 * fire too early.  The nastiest case: the payload's LAST TWO bytes happen to be
 * "OK", sitting exactly at the SRECV window boundary.  The modem then appends
 * its framing, so the byte stream around the boundary looks like:
 *
 *     ... 4F 4B | 0D 0A 0D 0A 4F 4B 0D 0A
 *          O  K | \r \n \r \n  O  K \r \n
 *          ^^^^^^^^^^^
 *       payload "OK" + framing "\r\n"  ==  "OK\r\n"   <-- FALSE terminator!
 *
 * A naive tail-match stops right there, 6 bytes short of the true end.  The
 * caller then receives a truncated chunk, the HTTP body never completes, and
 * the transfer stalls forever at that exact offset.  (Observed at firmware
 * offset 98304, where chunk[596..597] == "OK".)  Note the payload contains only
 * the 2-byte "OK"; the "\r\n" comes from the modem, so scanning the file for a
 * 4-byte "OK\r\n" finds nothing -- the pattern only exists on the wire.
 *
 * WHAT WE MUST DO
 * ---------------
 * The response is length-prefixed: once we read "#SRECV: 1,LEN" we KNOW exactly
 * how many payload bytes follow.  So we must NOT interpret those LEN bytes as
 * text at all, and we must only accept a terminator that starts AFTER them.
 *
 * WHAT WE DO (two cooperating mechanisms)
 * ---------------------------------------
 *   1) binary_bytes_remaining : armed to LEN when the header line completes.
 *      While > 0, each incoming byte is stored and counted down with NO URC /
 *      OK / ERROR checking.  This isolates the payload from all text parsing.
 *   2) srecv_payload_end : buffer offset one past the last payload byte
 *      (= buffer length at arm time + LEN).  In check_response_complete() an
 *      "OK\r\n"/ERROR match is IGNORED if its start position is < this offset,
 *      i.e. the match lies inside the declared payload.  Only a terminator
 *      at/after srecv_payload_end is the real end of response.
 *
 * WORKED EXAMPLE (LEN = 1024, header is 18 bytes)
 * -----------------------------------------------
 *   - Header "\r\n#SRECV: 1,1024\r\n" ends at buffer offset 18.
 *   - Arm: binary_bytes_remaining = 1024, srecv_payload_end = 18 + 1024 = 1042.
 *   - Payload occupies offsets [18 .. 1042); its last 2 bytes ("OK") are at
 *     offsets 1040,1041.
 *   - Modem framing "\r\n\r\nOK\r\n" follows at offsets 1042..1049.
 *   - First "OK\r\n" tail match happens at buffer length 1044 (payload "OK" +
 *     framing "\r\n").  ok_pos = 1044 - 4 = 1040 < 1042  -> REJECTED, keep reading.
 *   - Real terminator matches at buffer length 1050. ok_pos = 1046 >= 1042
 *     -> ACCEPTED.  Full 1024-byte payload is delivered intact.
 *
 * Non-SRECV responses leave srecv_payload_end == 0, so the guard is inert and
 * ordinary commands terminate exactly as before.
 * ===========================================================================
 */

/**
 * @brief Parse the data byte count from a buffered #SRECV response header.
 *
 * Expects the response buffer to currently end with "\r\n#SRECV: N,LEN\r\n"
 * (i.e. the header line of an AT#SRECV response has just been completed).
 * Returns the LEN value so the caller can arm the binary receive counter,
 * preventing firmware binary data that contains "\r\nOK\r\n" from triggering
 * a premature end-of-response detection.
 *
 * @param[out] p_len  Receives the declared data byte count.
 * @return true if a valid #SRECV header was found, false otherwise.
 */
static bool at_srecv_parse_pending_length(uint16_t *p_len)
{
	const char *p_buf   = (const char *)at_engine.response_buffer;
	uint16_t    buf_len = at_engine.response_buffer_len;

	/* Buffer must end with \r\n (just completed a line) */
	if ((buf_len < 2U) ||
	    (at_engine.response_buffer[buf_len - 1U] != '\n') ||
	    (at_engine.response_buffer[buf_len - 2U] != '\r'))
	{
		return false;
	}

	/* Find the start of the LAST line by scanning backwards past the
	 * trailing \r\n to find the previous \r\n (or start of buffer).
	 * We must check ONLY the last line — using strstr() would match an
	 * old #SRECV header that is already deep in the buffer, causing the
	 * binary byte counter to be re-armed on every subsequent \r\n. */
	uint16_t line_start = 0U;
	if (buf_len > 2U)
	{
		uint16_t i = buf_len - 3U; /* start just before trailing \r\n */
		while (i > 0U)
		{
			if ((p_buf[i] == '\n') && (i >= 1U) && (p_buf[i - 1U] == '\r'))
			{
				line_start = i + 1U; /* byte after previous \r\n */
				break;
			}
			i--;
		}
		/* If i==0 without finding a previous \r\n, line_start stays 0 (whole buffer is one line) */
	}

	/* Last line content (without the trailing \r\n) */
	const char *last_line     = p_buf + line_start;
	uint16_t    last_line_len = (buf_len - 2U) - line_start; /* exclude trailing \r\n */

	/* Must start with "#SRECV:" */
	if ((last_line_len < 7U) || (strncmp(last_line, "#SRECV:", 7) != 0))
	{
		return false;
	}

	/* Parse: #SRECV: <socket>,<len> */
	const char *comma = strchr(last_line, ',');
	if (comma == NULL) { return false; }
	comma++;

	uint16_t len = 0U;
	for (const char *p = comma; (*p != '\0') && (*p != '\r'); p++)
	{
		if ((*p < '0') || (*p > '9')) { return false; }
		len = (uint16_t)((uint16_t)(len * 10U) + (uint16_t)((uint8_t)*p - (uint8_t)'0'));
	}

	*p_len = len;
	return (len > 0U);
}

/**
 * @brief Read available bytes from ring buffer into response buffer.
 *        URC and response checks run only on line boundaries ('\n').
 *
 * @param[in] check_response  true = also check for OK/ERROR (WAIT_RESPONSE),
 *                            false = only check for URCs (URC_INCOMING).
 */
static void consume_rx_data(bool check_response)
{
	uint8_t byte;

	while(rbuff_read_safe(&rx_ringbuf, &byte) &&
	      at_engine.response_buffer_len < AT_ENGINE_RESPONSE_BUFFER_SIZE)
	{
		at_engine.response_buffer[at_engine.response_buffer_len++] = byte;

		/* Maintain null-termination for string operations */
		if(at_engine.response_buffer_len < AT_ENGINE_RESPONSE_BUFFER_SIZE)
		{
			at_engine.response_buffer[at_engine.response_buffer_len] = '\0';
		}

		/* Binary SRECV mode: consume the declared payload bytes without checking
		 * for AT terminators. This prevents firmware binary data containing
		 * "\r\nOK\r\n" from causing premature end-of-response detection. */
		if(at_engine.binary_bytes_remaining > 0U)
		{
			at_engine.binary_bytes_remaining--;
			continue;
		}

		/* Only check on line boundary to avoid per-byte search overhead.
		 * Exception: if the expected response does not end with '\n'
		 * (e.g. "\r\n> "), also check when the last expected byte arrives. */
		if(byte != '\n')
		{
			if(check_response &&
			   at_engine.expected_response_len > 0U &&
			   byte == at_engine.expected_response[at_engine.expected_response_len - 1U])
			{
				check_response_complete();
				if(at_engine.state == AT_ENGINE_DONE)
				{
					break;
				}
			}
			continue;
		}

		/* Extract any completed URC lines from the buffer.
		 * Loop because multiple URCs may be present (e.g. two SRING lines
		 * delivered in the same TCP segment), and because a URC can arrive
		 * without a leading \r\n so the buffer may already contain more than
		 * one complete URC after a single '\n' boundary. */
		while(check_urc_in_response()) {
			/* repeat until no more URCs found */
		}

		/* After each '\n': check if this ended a #SRECV header line.
		 * If so, arm the binary byte counter to protect the payload from
		 * false terminator detection. */
		if(check_response)
		{
			uint16_t srecv_len = 0U;
			if(at_srecv_parse_pending_length(&srecv_len))
			{
				at_engine.binary_bytes_remaining = srecv_len;
				/* Record one-past-the-last payload byte so terminator detection
				 * ignores any OK/ERROR byte pattern inside the binary payload. */
				at_engine.srecv_payload_end = (uint16_t)(at_engine.response_buffer_len + srecv_len);
				continue; /* Binary payload follows -- do not check for OK yet */
			}
		}

		if(check_response)
		{
			check_response_complete();
			if(at_engine.state == AT_ENGINE_DONE)
			{
				/* Response complete. Before stopping, drain any URCs that arrived
				 * in the same TCP segment as the final OK/ERROR (e.g. SRING
				 * piggy-backed onto the AT#SSEND "OK" response). */
				while(check_urc_in_response()) {
					/* repeat until no more URCs found */
				}
				break;
			}
		}
	}

	/* Response buffer overflow: ring buffer still has data but response buffer
	 * is full.  Drain the remaining bytes to prevent stale data from
	 * contaminating the next AT command's response on retry/next cycle.
	 * The current command will likely timeout and be retried. */
	if((at_engine.response_buffer_len >= AT_ENGINE_RESPONSE_BUFFER_SIZE) &&
	   (rbuff_available(&rx_ringbuf) > 0U))
	{
		GSM_LOG_ERR("AT response buffer overflow! Draining %u stale bytes from ring buffer\r\n",
		            rbuff_available(&rx_ringbuf));
		rbuff_clear(&rx_ringbuf);
	}
}

static void at_engine_wait_response_process(void)
{
	if(at_engine_check_timeout())
	{
		at_engine.state = AT_ENGINE_SEND_CMD; /* Retry */
		return;
	}

	consume_rx_data(true);

	/* Do NOT drain the ring buffer here in DONE state.
	 * SRING URCs may be arriving byte-by-byte via UART ISR.  Reading a
	 * partial URC into the response buffer (which is cleared on reset)
	 * splits the keyword across two buffers, making it unrecognizable.
	 * Instead, leave URC bytes in the ring buffer.  at_engine_clear_buff()
	 * no longer clears the ring buffer, so the data is preserved through
	 * the reset cycle.  The next IDLE -> URC_INCOMING transition will
	 * process the complete URC cleanly.  If the caller sends a new AT
	 * command immediately, consume_rx_data(true) will extract the URC
	 * from the response buffer via check_urc_in_response() before
	 * checking for OK/ERROR. */
}

static void at_engine_urc_incoming_process(void)
{
	if((bsp_get_tick() - at_engine.urc_timeout) > 300U)
	{
		at_engine.response_buffer_len = 0U; /* Discard incomplete URC data */
		at_engine.state = AT_ENGINE_IDLE;
		return;
	}

	consume_rx_data(false);

	/* Safety-net: trim any leading CR/LF whitespace left in the buffer after
	 * URC extraction.  check_urc_in_response() now strips the preceding \r\n
	 * as well, so normally the buffer is already empty here.  This trim
	 * handles edge cases where urc_start == 0 (no preceding \r\n) or where
	 * multiple \r\n separators accumulated between URCs. */
	{
		uint16_t skip = 0U;
		while((skip < at_engine.response_buffer_len) &&
		      ((at_engine.response_buffer[skip] == '\r') ||
		       (at_engine.response_buffer[skip] == '\n')))
		{
			skip++;
		}
		if(skip > 0U)
		{
			response_buffer_remove(0U, skip);
		}
	}

	/* Return to IDLE as soon as all URCs are consumed and only whitespace
	 * (or nothing) remains.  This unblocks at_engine_is_busy() immediately
	 * so that gsm_process can issue AT#SRECV without the 300 ms delay that
	 * previously caused SRING data to be lost. */
	if(at_engine.response_buffer_len == 0U)
	{
		at_engine.state = AT_ENGINE_IDLE;
	}
}

static void at_engine_idle_process(void)
{
	if(rbuff_available(&rx_ringbuf))
	{
			at_engine.urc_timeout = bsp_get_tick();
			at_engine.state = AT_ENGINE_URC_INCOMING;
	}
}

at_engine_result_t at_engine_get_result(void)
{
	return at_engine.result;
}

void at_engine_reset(void)
{
	/* Abort any in-flight DMA TX so HAL returns to READY state. */
	if (s_tx_done_flag != 0U)
	{
		extern UART_HandleTypeDef huart1;
		(void)HAL_UART_AbortTransmit(&huart1);
		s_tx_done_flag = 0U;
	}

	at_engine.state = AT_ENGINE_IDLE;
	at_engine.result = AT_ENGINE_RESULT_NONE;
	at_engine.data_ptr = NULL;
	at_engine.data_len = 0U;
	at_engine.is_data_mode = false;
	at_engine.cmd_len = 0U;
	at_engine.expected_response_len = 0U;
	at_engine.response_buffer_len = 0U;
}

int32_t at_engine_process(void)
{
	switch(at_engine.state)
	{
		case AT_ENGINE_IDLE:
			at_engine_idle_process();
			break;
		case AT_ENGINE_URC_INCOMING:
			at_engine_urc_incoming_process();
			break;	
		case AT_ENGINE_SEND_CMD:
			at_engine_send_process();
			break;
		case AT_ENGINE_WAIT_RESPONSE:
			at_engine_wait_response_process();
			break;
		case AT_ENGINE_DONE:
			/* Wait for caller to read result and call at_engine_reset().
			 * Do NOT drain the ring buffer here — partial URC reads would
			 * split the URC keyword between the response buffer (cleared on
			 * reset) and the ring buffer, causing missed URC detections.
			 * URC data in the ring buffer is preserved through reset and
			 * processed in the next IDLE -> URC_INCOMING cycle. */
			break;
		default:
			at_engine.state = AT_ENGINE_IDLE;
			break;
	}

	return 0;
}

/* ---------- Response Access ---------- */

const uint8_t *at_engine_get_response(uint16_t *p_length)
{
	if(p_length != NULL)
	{
		*p_length = at_engine.response_buffer_len;
	}
	return at_engine.response_buffer;
}

/* ---------- Buffer Management ---------- */

void at_engine_clear_buff(void)
{
	/* Do NOT clear the ring buffer here.
	 * It may contain pending URC data (e.g. SRING) that arrived after the
	 * last AT response but before this call.  Clearing the ring buffer would
	 * destroy those bytes and cause missed URC detections.
	 * The ring buffer will be consumed normally when the engine returns to
	 * IDLE and transitions to URC_INCOMING. */
	at_engine.response_buffer_len = 0U;
	at_engine.response_buffer[0] = '\0';
}

void at_engine_cancel(void)
{
	at_engine.state = AT_ENGINE_IDLE;
	at_engine.result = AT_ENGINE_RESULT_NONE;
	at_engine.data_ptr = NULL;
	at_engine.data_len = 0U;
	at_engine.is_data_mode = false;
	at_engine.cmd_len = 0U;
	at_engine.expected_response_len = 0U;
	at_engine.response_buffer_len = 0U;
	rbuff_clear(&rx_ringbuf);
}

const char *at_engine_get_state_str(void)
{
	static const char * const s_names[] = {
		[AT_ENGINE_IDLE]          = "IDLE",
		[AT_ENGINE_URC_INCOMING]  = "URC",
		[AT_ENGINE_SEND_CMD]      = "SEND",
		[AT_ENGINE_WAIT_RESPONSE] = "WAIT",
		[AT_ENGINE_DONE]          = "DONE",
	};
	uint8_t idx = (uint8_t)at_engine.state;
	if (idx < (uint8_t)(sizeof(s_names) / sizeof(s_names[0])))
	{
		return s_names[idx];
	}
	return "?";
}

/* ---------- TX Functions ---------- */

void at_engine_send_raw(const uint8_t *p_data, uint16_t len)
{
	if((p_data == NULL) || (len == 0U))
	{
		return;
	}
	uart_send_buffer(UART_1, (const char *)p_data, (int)len);
}

bool at_engine_is_tx_done(void)
{
	return (s_tx_done_flag == 0U);
}

void at_engine_dma_tx_complete_callback(void)
{
	s_tx_done_flag = 0U;
}



#if 0
void at_engine_send_raw_async(uint8_t *p_data, uint16_t len)
{
	if((p_data == NULL) || (len == 0U))
	{
		return;
	}

	s_tx_ptr = p_data + 1;
	s_tx_remaining = len - 1U;
	s_tx_done_flag = 1U;

	/* Wait for TX register to be available */
	while(!LL_USART_IsActiveFlag_TXE_TXFNF(USART1))
	{
		/* Busy wait */
	}

	/* Send first byte directly */
	LL_USART_TransmitData8(USART1, p_data[0]);

	if(s_tx_remaining > 0U)
	{
		LL_USART_EnableIT_TXE_TXFNF(USART1);
	}
	else
	{
		LL_USART_EnableIT_TC(USART1);
	}
}

void at_engine_tx_empty_callback(void)
{
	if(s_tx_remaining > 0U)
	{
		LL_USART_TransmitData8(USART1, *s_tx_ptr);
		s_tx_ptr++;
		s_tx_remaining--;
	}
	else
	{
		LL_USART_DisableIT_TXE_TXFNF(USART1);
		LL_USART_EnableIT_TC(USART1);
	}
}

void at_engine_tx_complete_callback(void)
{
	LL_USART_DisableIT_TC(USART1);
	s_tx_done_flag = 0U;
}
#endif
/* End of file */
