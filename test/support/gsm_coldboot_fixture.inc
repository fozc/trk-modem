/*
 * gsm_coldboot_fixture.inc
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Migrated host scenarios and existing stateful hardware boundaries.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ================================================================== */
/* Stub'lar: gsm_init.c'nin dis bagimliliklari                        */
/* ================================================================== */

/* ---- Uretim dosyasi (statiklere erisim icin include) -------------- */
#include "../../Application/gsm/gsm_init.c"

/* ---- Test gozlem kuyrugu ------------------------------------------ */

/* ---- Stub durumlari ----------------------------------------------- */

gsm_t gsm;

static uint32_t tick_now = 0U;

static uint8_t restart_flag = 0U;
static unsigned restart_flag_sets = 0U;

static elog_code_t elog_last_code = ELOG_CODE_OK;
static uint8_t elog_last_arg = 0U;
static unsigned elog_cold_boot_calls = 0U;
static unsigned elog_exhausted_calls = 0U;

/* Not: gsm_set/get_init_state gsm_init.c icinde tanimli geliyor ve
 * gsm.init_state uzerinde calisiyor - stub'lanmaz. Ana durum ve
 * gecikme/bayraklar burada stub'lanir. */

/* ---- gsm_init.c'nin cagirdigi dis semboller ----------------------- */

void gsm_set_main_state(uint8_t state)
{
    gsm.main_state = state;
}
uint8_t gsm_get_main_state(void)
{
    return gsm.main_state;
}

uint32_t gsm_get_tick(void)
{
    return tick_now;
}
void gsm_set_delay(uint32_t d_time)
{
    (void)d_time;
}
void gsm_set_busy(void)
{
}
void gsm_set_free(void)
{
}
void gsm_set_tx_state(uint8_t state)
{
    gsm.tx_flag = state;
}
uint8_t gsm_get_socket_state(uint8_t socket)
{
    (void)socket;
    return SOCKET_CLOSED;
}
void gsm_set_socket_state(uint8_t socket, uint8_t state)
{
    (void)socket;
    (void)state;
}
uint8_t gsm_get_gprs_state(void)
{
    return 1U;
}

/* Bayrak API'si: gsm_process.c'deki gercek davranisin birebir kopyasi */
void gsm_request_module_restart(void)
{
    restart_flag = 1U;
    restart_flag_sets++;
}
bool gsm_module_restart_requested(void)
{
    return restart_flag != 0U;
}
void gsm_clear_module_restart(void)
{
    restart_flag = 0U;
}

void led_driver_set_modem_mode(led_modem_mode_t mode)
{
    (void)mode;
}
void led_driver_set_gsm_mode(led_gsm_mode_t mode)
{
    (void)mode;
}
void led_driver_set_web_mode(led_listener_mode_t mode)
{
    (void)mode;
}
void led_driver_set_iec104_mode(led_listener_mode_t mode)
{
    (void)mode;
}

void gsm_elog_modem_event(elog_code_t code)
{
    elog_last_code = code;
    if (code == ELOG_GSM_INIT_EXHAUSTED)
    {
        elog_exhausted_calls++;
    }
}

void gsm_elog_modem_event_with_arg(elog_code_t code, const void *arg, uint8_t arg_len)
{
    (void)arg_len;
    elog_last_code = code;
    if (code == ELOG_GSM_COLD_BOOT)
    {
        elog_cold_boot_calls++;
        elog_last_arg = *(const uint8_t *)arg;
    }
}

uint32_t gsm_engine_send_query(uint8_t query)
{
    (void)query;
    return 0U;
}
uint32_t gsm_engine_get_query_res(void)
{
    return 0U;
}
uint32_t gsm_pin_reset_module(void)
{
    return 0U;
}
void gsm_info_init(void)
{
}
void at_engine_clear_buff(void)
{
}
void at_engine_reset(void)
{
}
void gsm_listener_set_no_carrier(gsm_listener_id_t id, uint8_t v)
{
    (void)id;
    (void)v;
}

/* modem_config.h kullanimlari (SET_APN vb. adimlar linklenmesin diye) */
const char *modem_config_get_apn(void)
{
    return "test";
}
const char *modem_config_get_apn_user(void)
{
    return "u";
}
const char *modem_config_get_apn_pass(void)
{
    return "p";
}
bool modem_config_is_ntp_active(void)
{
    return false;
}

/* ================================================================== */
/* Testler                                                            */
/* ================================================================== */

static void reset_ladder_state(void)
{
    memset(&gsm, 0, sizeof(gsm));
    tick_now = 0U;
    restart_flag = 0U;
    restart_flag_sets = 0U;
    elog_last_code = ELOG_CODE_OK;
    elog_last_arg = 0U;
    elog_cold_boot_calls = 0U;
    elog_exhausted_calls = 0U;
    cold_boot_count = 0U;
}

/*** end of file ***/
