/*
 * gsm_info.c
 *
 * Centralised storage for GSM module runtime information.
 */

#include "gsm_info.h"
#include <string.h>

/* ---------- Internal state ---------- */

typedef struct
{
    char               imei[16];          /* 15 digits + '\0' */
    char               iccid[21];        /* 20 digits + '\0' */
    char               imsi[16];         /* 15 digits + '\0' */
    uint8_t            signal_quality;   /* +CSQ level, 99 = unknown */
    uint8_t            signal_quality_2G; /* +CESQ rxlev, 99 = unknown */
    uint8_t            signal_quality_3G; /* +CESQ rscp, 255 = unknown */
    uint8_t            signal_quality_4G; /* +CESQ rsrp, 255 = unknown */
    uint8_t            access_technology; /* 0=none, 2=2G, 3=3G, 4=4G */
    gsm_module_model_t module_model;
    char               fw_version[16];   /* e.g. "16.01.173\0" */
    gsm_sim_state_t    sim_state;
    gsm_net_reg_state_t creg;            /* 2G CS registration */
    gsm_net_reg_state_t cgreg;           /* 2G/3G PS registration */
    gsm_net_reg_state_t cereg;           /* 4G EPS registration */
    gsm_info_cell_t    cell;
    uint8_t            gprs_state;       /* 0 = off, 1 = connected */
    uint32_t           ip_addr;          /* Host byte order */
    uint32_t           tx_counter;
    uint32_t           rx_counter;
} gsm_info_store_t;

static gsm_info_store_t s_info;

/* ---------- Lifecycle ---------- */

void gsm_info_init(void)
{
    (void)memset(&s_info, 0, sizeof(s_info));
    s_info.signal_quality    = 99U;
    s_info.signal_quality_2G = 99U;
    s_info.signal_quality_3G = 99U;
    s_info.signal_quality_4G = 99U;
    s_info.sim_state         = GSM_SIM_STATE_UNKNOWN;
    s_info.creg              = GSM_NET_REG_NOT_REGISTERED;
    s_info.cgreg             = GSM_NET_REG_NOT_REGISTERED;
    s_info.cereg             = GSM_NET_REG_NOT_REGISTERED;
}

/* ---------- IMEI ---------- */

void gsm_info_set_imei(const char *p_imei)
{
    if (p_imei == NULL)
    {
        return;
    }
    (void)strncpy(s_info.imei, p_imei, sizeof(s_info.imei) - 1U);
    s_info.imei[sizeof(s_info.imei) - 1U] = '\0';
}

const char *gsm_info_get_imei(void)
{
    return s_info.imei;
}

/* ---------- ICCID ---------- */

void gsm_info_set_iccid(const char *p_iccid)
{
    if (p_iccid == NULL)
    {
        return;
    }
    (void)strncpy(s_info.iccid, p_iccid, sizeof(s_info.iccid) - 1U);
    s_info.iccid[sizeof(s_info.iccid) - 1U] = '\0';
}

const char *gsm_info_get_iccid(void)
{
    return s_info.iccid;
}

/* ---------- IMSI ---------- */

void gsm_info_set_imsi(const char *p_imsi)
{
    if (p_imsi == NULL)
    {
        return;
    }
    (void)strncpy(s_info.imsi, p_imsi, sizeof(s_info.imsi) - 1U);
    s_info.imsi[sizeof(s_info.imsi) - 1U] = '\0';
}

const char *gsm_info_get_imsi(void)
{
    return s_info.imsi;
}

/* ---------- Signal quality ---------- */

void gsm_info_set_signal_quality(uint8_t level)
{
    s_info.signal_quality = level;
}

uint8_t gsm_info_get_signal_quality(void)
{
    return s_info.signal_quality;
}

void gsm_info_set_signal_quality_2G(uint8_t level)
{
    s_info.signal_quality_2G = level;
}

uint8_t gsm_info_get_signal_quality_2G(void)
{
    return s_info.signal_quality_2G;
}

void gsm_info_set_signal_quality_3G(uint8_t level)
{
    s_info.signal_quality_3G = level;
}

uint8_t gsm_info_get_signal_quality_3G(void)
{
    return s_info.signal_quality_3G;
}

void gsm_info_set_signal_quality_4G(uint8_t level)
{
    s_info.signal_quality_4G = level;
}

uint8_t gsm_info_get_signal_quality_4G(void)
{
    return s_info.signal_quality_4G;
}

/* ---------- Access technology (RAT) ---------- */

void gsm_info_set_access_technology(uint8_t rat)
{
    s_info.access_technology = rat;
}

uint8_t gsm_info_get_access_technology(void)
{
    return s_info.access_technology;
}

/* ---------- Module model & firmware ---------- */

void gsm_info_set_module_model(gsm_module_model_t model)
{
    s_info.module_model = model;
}

gsm_module_model_t gsm_info_get_module_model(void)
{
    return s_info.module_model;
}

void gsm_info_set_fw_version(const char *p_ver)
{
    if (p_ver == NULL)
    {
        return;
    }
    (void)strncpy(s_info.fw_version, p_ver, sizeof(s_info.fw_version) - 1U);
    s_info.fw_version[sizeof(s_info.fw_version) - 1U] = '\0';
}

const char *gsm_info_get_fw_version(void)
{
    return s_info.fw_version;
}

/* ---------- SIM card state ---------- */

void gsm_info_set_sim_state(gsm_sim_state_t state)
{
    s_info.sim_state = state;
}

gsm_sim_state_t gsm_info_get_sim_state(void)
{
    return s_info.sim_state;
}

/* ---------- Network registration ---------- */

void gsm_info_set_creg(gsm_net_reg_state_t state)
{
    s_info.creg = state;
}

gsm_net_reg_state_t gsm_info_get_creg(void)
{
    return s_info.creg;
}

void gsm_info_set_cgreg(gsm_net_reg_state_t state)
{
    s_info.cgreg = state;
}

gsm_net_reg_state_t gsm_info_get_cgreg(void)
{
    return s_info.cgreg;
}

void gsm_info_set_cereg(gsm_net_reg_state_t state)
{
    s_info.cereg = state;
}

gsm_net_reg_state_t gsm_info_get_cereg(void)
{
    return s_info.cereg;
}

/* ---------- Cell information ---------- */

void gsm_info_set_cell(const gsm_info_cell_t *p_cell)
{
    if (p_cell == NULL)
    {
        return;
    }
    s_info.cell = *p_cell;
}

void gsm_info_get_cell(gsm_info_cell_t *p_cell)
{
    if (p_cell == NULL)
    {
        return;
    }
    *p_cell = s_info.cell;
}

/* ---------- GPRS / IP ---------- */

void gsm_info_set_gprs_state(uint8_t state)
{
    s_info.gprs_state = state;
}

uint8_t gsm_info_get_gprs_state(void)
{
    return s_info.gprs_state;
}

void gsm_info_set_ip(uint32_t ip_u32)
{
    s_info.ip_addr = ip_u32;
}

uint32_t gsm_info_get_ip(void)
{
    return s_info.ip_addr;
}

/* ---------- Counters ---------- */

void gsm_info_add_rx_bytes(uint32_t count)
{
    s_info.rx_counter += count;
}

void gsm_info_add_tx_bytes(uint32_t count)
{
    s_info.tx_counter += count;
}

void gsm_info_get_rxtx_counters(uint32_t *p_tx, uint32_t *p_rx)
{
    if (p_tx != NULL)
    {
        *p_tx = s_info.tx_counter;
    }
    if (p_rx != NULL)
    {
        *p_rx = s_info.rx_counter;
    }
}
