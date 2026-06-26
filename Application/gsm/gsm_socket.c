/**
 * @file  gsm_socket.c
 * @brief Centralized socket state and session tracking implementation.
 */
#include "gsm_socket.h"
#include "bsp.h"
#include <string.h>

#define GSM_SOCKET_CHECK_TIMEOUT_MS  180000U
#define GSM_SOCKET_INVALID_STATE     0xFFU

/* ------------------------------------------------------------------ */
/*  Modem socket states (AT#SS response character codes)              */
/* ------------------------------------------------------------------ */
#define SS_CLOSED       '0'
#define SS_OPEN         '1'
#define SS_SUSPENDED    '2'   /* Client connected (idle)  */
#define SS_DATA_PENDING '3'   /* Connected + unread data  */
#define SS_LISTENING    '4'
#define SS_INCOMING     '5'
#define SS_CONNECTING   '7'

typedef struct
{
    uint8_t            state;
    uint32_t           last_activity;
    uint32_t           check_timeout;
    gsm_socket_stats_t stats;
} gsm_socket_entry_t;

static gsm_socket_entry_t s_sockets[GSM_SOCKET_COUNT];

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Determine if a modem state code represents an active connection.
 *
 * For listeners: states 2/3 mean a client is connected.
 * For dialer:    states 2/3 mean connected to remote server.
 */
static bool is_connected_state(uint8_t state)
{
    return (state == SS_SUSPENDED) || (state == SS_DATA_PENDING);
}

static uint32_t tick_diff_ms(uint32_t start, uint32_t end)
{
    return (end >= start) ? (end - start) : (end + (UINT32_MAX - start) + 1U);
}

static void session_start(gsm_socket_entry_t *p_entry)
{
    uint32_t now = bsp_get_tick();
    p_entry->stats.is_connected   = true;
    p_entry->stats.connect_tick   = now;
    p_entry->stats.connect_epoch  = bsp_get_epoch_time();
    p_entry->stats.total_sessions++;
}

static void session_end(gsm_socket_entry_t *p_entry, sock_disconnect_reason_t reason)
{
    if (!p_entry->stats.is_connected)
    {
        return;  /* Already disconnected — avoid double-counting */
    }

    uint32_t now      = bsp_get_tick();
    uint32_t duration = tick_diff_ms(p_entry->stats.connect_tick, now);

    p_entry->stats.is_connected           = false;
    p_entry->stats.last_connect_epoch     = p_entry->stats.connect_epoch;
    p_entry->stats.last_disconnect_epoch  = bsp_get_epoch_time();
    p_entry->stats.last_session_ms        = duration;
    p_entry->stats.last_disconnect_reason = (uint8_t)reason;
    p_entry->stats.total_connected_ms    += duration;
    p_entry->stats.total_disconnects++;

    if (duration > p_entry->stats.max_session_ms)
    {
        p_entry->stats.max_session_ms = duration;
    }

    p_entry->stats.connect_tick  = 0U;
    p_entry->stats.connect_epoch = 0U;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void gsm_socket_init(void)
{
    (void)memset(s_sockets, 0, sizeof(s_sockets));
    for (uint8_t i = 0U; i < (uint8_t)GSM_SOCKET_COUNT; i++)
    {
        s_sockets[i].check_timeout = GSM_SOCKET_CHECK_TIMEOUT_MS;
    }
}

void gsm_socket_set_state(uint8_t socket, uint8_t state)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return;
    }

    gsm_socket_entry_t *p = &s_sockets[socket];
    uint8_t old_state = p->state;

    if (old_state == state)
    {
        return;  /* No change — skip */
    }

    p->state         = state;
    p->last_activity = bsp_get_tick();

    /* Detect session transitions */
    bool was_connected = is_connected_state(old_state);
    bool now_connected = is_connected_state(state);

    if (!was_connected && now_connected)
    {
        session_start(p);
    }
    else if (was_connected && !now_connected)
    {
        sock_disconnect_reason_t reason = SOCK_DISC_SOCKET_CLOSED;
        if (state == SS_CLOSED)
        {
            reason = SOCK_DISC_SOCKET_CLOSED;
        }
        else if (state == SS_LISTENING)
        {
            reason = SOCK_DISC_NO_CARRIER;  /* Client disconnected, back to listening */
        }
        session_end(p, reason);
    }
}

void gsm_socket_touch_activity(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return;
    }

    s_sockets[socket].last_activity = bsp_get_tick();
}

uint8_t gsm_socket_get_state(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return GSM_SOCKET_INVALID_STATE;
    }

    return s_sockets[socket].state;
}

uint32_t gsm_socket_get_last_activity(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return 0U;
    }

    return s_sockets[socket].last_activity;
}

bool gsm_socket_is_check_expired(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return false;
    }

    uint32_t now = bsp_get_tick();
    return (now - s_sockets[socket].last_activity) >= s_sockets[socket].check_timeout;
}

void gsm_socket_record_disconnect(uint8_t socket, sock_disconnect_reason_t reason)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return;
    }

    session_end(&s_sockets[socket], reason);
}

void gsm_socket_record_attempt(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return;
    }

    s_sockets[socket].stats.connect_attempts++;
}

void gsm_socket_record_error(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return;
    }

    s_sockets[socket].stats.error_count++;
}

const gsm_socket_stats_t *gsm_socket_get_stats(uint8_t socket)
{
    if (socket >= (uint8_t)GSM_SOCKET_COUNT)
    {
        return NULL;
    }

    return &s_sockets[socket].stats;
}
