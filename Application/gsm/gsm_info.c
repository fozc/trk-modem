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
    uint8_t            ber;               /* +CESQ ber,  99 = unknown */
    uint8_t            ecno;              /* +CESQ ecno, 255 = unknown */
    uint8_t            rsrq;              /* +CESQ rsrq, 255 = unknown */
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
    s_info.signal_quality_3G = 255U;
    s_info.signal_quality_4G = 255U;
    s_info.ber               = 99U;
    s_info.ecno              = 255U;
    s_info.rsrq              = 255U;
    s_info.access_technology  = GSM_ACCESS_TECH_UNDEFINED;

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

void gsm_info_set_2G_ber(uint8_t value)
{
    s_info.ber = value;
}

uint8_t gsm_info_get_2G_ber(void)
{
    return s_info.ber;
}

void gsm_info_set_3G_ecno(uint8_t value)
{
    s_info.ecno = value;
}

uint8_t gsm_info_get_3G_ecno(void)
{
    return s_info.ecno;
}

void gsm_info_set_4G_rsrq(uint8_t value)
{
    s_info.rsrq = value;
}

uint8_t gsm_info_get_4G_rsrq(void)
{
    return s_info.rsrq;
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

network_generation_t get_network_generation(void)
{
    switch (gsm_info_get_access_technology())
    {
        case GSM_ACCESS_TECH_GSM:
        case GSM_ACCESS_TECH_GSM_COMPACT:
        case GSM_ACCESS_TECH_GSM_EGPRS:
            return NETWORK_GEN_2G;

        case GSM_ACCESS_TECH_UTRAN:
        case GSM_ACCESS_TECH_UTRAN_HSDPA:
        case GSM_ACCESS_TECH_UTRAN_HSUPA:
        case GSM_ACCESS_TECH_UTRAN_HSDPA_HSUPA:
        case GSM_ACCESS_TECH_UTRAN_HSPA_PLUS:
            return NETWORK_GEN_3G;

        case GSM_ACCESS_TECH_E_UTRAN:
        case GSM_ACCESS_TECH_E_UTRAN_CA:
            return NETWORK_GEN_4G;

        default:
            return NETWORK_GEN_UNKNOWN;
    }
}

const char* get_access_tech_str(gsm_access_technology_t tech)
{
    switch (tech)
    {
        case GSM_ACCESS_TECH_GSM:               return "2G";
        case GSM_ACCESS_TECH_GSM_COMPACT:       return "2G-CMPT";
        case GSM_ACCESS_TECH_UTRAN:             return "3G";
        case GSM_ACCESS_TECH_GSM_EGPRS:         return "2G-EDGE";
        case GSM_ACCESS_TECH_UTRAN_HSDPA:       return "3G-HSDPA";
        case GSM_ACCESS_TECH_UTRAN_HSUPA:       return "3G-HSUPA";
        case GSM_ACCESS_TECH_UTRAN_HSDPA_HSUPA: return "3G-HSPA";
        case GSM_ACCESS_TECH_E_UTRAN:           return "4G";
        case GSM_ACCESS_TECH_UTRAN_HSPA_PLUS:   return "3G-HSPA+";
        case GSM_ACCESS_TECH_E_UTRAN_CA:        return "4G+";
        default:                                return "UNK";
    }
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

/* ---------- +CESQ telemetry report ---------- */

/*
 * Kalitatif esikler x10 (deci-birim) olarak tutulur; boylece 0.5 dB
 * adimli rsrq/ecno icin float gerekmez. Daha yukari her zaman daha iyidir.
 * Esikler yaygin saha-testi (field-test) uygulamalarindan alinmistir;
 * kolayca ayarlanabilir.
 *
 *   Guce gore (dBm): rsrp / rscp / rxlev
 *   Kaliteye gore (dB): rsrq / ecno
 *   ber ayri: RXQUAL 0-7 (0 en iyi)
 */

/* Sinyal gucu (dBm) esikleri, x10 */
#define RSRP_EXC_X10   (-800)   /* -80 dBm */
#define RSRP_STR_X10   (-900)   /* -90     */
#define RSRP_MID_X10   (-1000)  /* -100    */
#define RSRP_WEAK_X10  (-1100)  /* -110    */

#define RSCP_EXC_X10   (-600)
#define RSCP_STR_X10   (-750)
#define RSCP_MID_X10   (-850)
#define RSCP_WEAK_X10  (-950)

#define RXLEV_EXC_X10  (-600)
#define RXLEV_STR_X10  (-750)
#define RXLEV_MID_X10  (-850)
#define RXLEV_WEAK_X10 (-950)

/* Sinyal kalitesi (dB) esikleri, x10 */
#define RSRQ_EXC_X10   (-100)   /* -10 dB */
#define RSRQ_STR_X10   (-150)
#define RSRQ_MID_X10   (-180)
#define RSRQ_WEAK_X10  (-200)

#define ECNO_EXC_X10   (-60)
#define ECNO_STR_X10   (-100)
#define ECNO_MID_X10   (-140)
#define ECNO_WEAK_X10  (-180)

static const char *cesq_label_5level(int16_t v,
                                     int16_t exc, int16_t strong,
                                     int16_t mid,   int16_t weak)
{
    if (v >= exc)    { return "Excellent"; }
    if (v >= strong) { return "Strong";    }
    if (v >= mid)    { return "Mid";       }
    if (v >= weak)   { return "Weak";      }
    return "Very Weak";
}

static const char *cesq_label_ber(uint8_t ber)
{
    if (ber <= 1U) { return "Excellent"; }
    if (ber <= 3U) { return "Strong";    }
    if (ber == 4U) { return "Mid";       }
    if (ber <= 6U) { return "Weak";      }
    return "Very Weak";  /* 7 */
}

static void cesq_field_set_na(gsm_cesq_field_t *f, gsm_cesq_unit_t unit, uint8_t raw)
{
    f->raw       = raw;
    f->physical  = 0;
    f->available = false;
    f->unit      = unit;
    f->label     = "N/A";
}

void gsm_info_get_cesq_report(gsm_cesq_report_t *p)
{
    if (p == NULL)
    {
        return;
    }
    (void)memset(p, 0, sizeof(*p));

    /* 2G - rxlev (guc, dBm):  dBm = deger - 111  */
    p->rxlev.raw  = s_info.signal_quality_2G;
    p->rxlev.unit = GSM_CESQ_UNIT_DBM;
    if (s_info.signal_quality_2G != 99U)
    {
        p->rxlev.available = true;
        p->rxlev.physical  = (int16_t)(((int16_t)s_info.signal_quality_2G - 111) * 10);
        p->rxlev.label     = cesq_label_5level(p->rxlev.physical,
                                                RXLEV_EXC_X10, RXLEV_STR_X10,
                                                RXLEV_MID_X10, RXLEV_WEAK_X10);
    }
    else
    {
        cesq_field_set_na(&p->rxlev, GSM_CESQ_UNIT_DBM, s_info.signal_quality_2G);
    }

    /* 2G - ber (kalite, RXQUAL 0-7; 0 en iyi) */
    p->ber.raw  = s_info.ber;
    p->ber.unit = GSM_CESQ_UNIT_NONE;
    if (s_info.ber != 99U)
    {
        p->ber.available = true;
        p->ber.physical  = (int16_t)s_info.ber;
        p->ber.label     = cesq_label_ber(s_info.ber);
    }
    else
    {
        cesq_field_set_na(&p->ber, GSM_CESQ_UNIT_NONE, s_info.ber);
    }

    /* 3G - rscp (guc, dBm):  dBm = deger - 121  */
    p->rscp.raw  = s_info.signal_quality_3G;
    p->rscp.unit = GSM_CESQ_UNIT_DBM;
    if (s_info.signal_quality_3G != 255U)
    {
        p->rscp.available = true;
        p->rscp.physical  = (int16_t)(((int16_t)s_info.signal_quality_3G - 121) * 10);
        p->rscp.label     = cesq_label_5level(p->rscp.physical,
                                                RSCP_EXC_X10, RSCP_STR_X10,
                                                RSCP_MID_X10, RSCP_WEAK_X10);
    }
    else
    {
        cesq_field_set_na(&p->rscp, GSM_CESQ_UNIT_DBM, s_info.signal_quality_3G);
    }

    /* 3G - ecno (kalite, dB):  dB = deger*0.5 - 24.5  =>  x10: deger*5 - 245  */
    p->ecno.raw  = s_info.ecno;
    p->ecno.unit = GSM_CESQ_UNIT_DB;
    if (s_info.ecno != 255U)
    {
        p->ecno.available = true;
        p->ecno.physical  = (int16_t)(((int16_t)s_info.ecno * 5) - 245);
        p->ecno.label     = cesq_label_5level(p->ecno.physical,
                                                ECNO_EXC_X10, ECNO_STR_X10,
                                                ECNO_MID_X10, ECNO_WEAK_X10);
    }
    else
    {
        cesq_field_set_na(&p->ecno, GSM_CESQ_UNIT_DB, s_info.ecno);
    }

    /* 4G - rsrq (kalite, dB):  dB = deger*0.5 - 20  =>  x10: deger*5 - 200  */
    p->rsrq.raw  = s_info.rsrq;
    p->rsrq.unit = GSM_CESQ_UNIT_DB;
    if (s_info.rsrq != 255U)
    {
        p->rsrq.available = true;
        p->rsrq.physical  = (int16_t)(((int16_t)s_info.rsrq * 5) - 200);
        p->rsrq.label     = cesq_label_5level(p->rsrq.physical,
                                                RSRQ_EXC_X10, RSRQ_STR_X10,
                                                RSRQ_MID_X10, RSRQ_WEAK_X10);
    }
    else
    {
        cesq_field_set_na(&p->rsrq, GSM_CESQ_UNIT_DB, s_info.rsrq);
    }

    /* 4G - rsrp (guc, dBm):  dBm = deger - 141  */
    p->rsrp.raw  = s_info.signal_quality_4G;
    p->rsrp.unit = GSM_CESQ_UNIT_DBM;
    if (s_info.signal_quality_4G != 255U)
    {
        p->rsrp.available = true;
        p->rsrp.physical  = (int16_t)(((int16_t)s_info.signal_quality_4G - 141) * 10);
        p->rsrp.label     = cesq_label_5level(p->rsrp.physical,
                                                RSRP_EXC_X10, RSRP_STR_X10,
                                                RSRP_MID_X10, RSRP_WEAK_X10);
    }
    else
    {
        cesq_field_set_na(&p->rsrp, GSM_CESQ_UNIT_DBM, s_info.signal_quality_4G);
    }

    /* Aktif teknoloji onceligi: 4G (rsrp) > 3G (rscp) > 2G (rxlev) */
    if (s_info.signal_quality_4G != 255U)
    {
        p->active_technology = "4G / LTE";
    }
    else if (s_info.signal_quality_3G != 255U)
    {
        p->active_technology = "3G / WCDMA";
    }
    else if (s_info.signal_quality_2G != 99U)
    {
        p->active_technology = "2G / GSM";
    }
    else
    {
        p->active_technology = "None";
    }
}
