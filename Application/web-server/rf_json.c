/**
 * @file rf_json.c
 * @brief GET /config/rf JSON govdesi - tablo-tabanli uretici.
 *
 * Tek alan tanim tablosu + tek emitor: alan tanimi (ad/tip/ofset) tek
 * yerde; yeni alan eklemek tek satirlik tablo girdisidir. Yanit yalnizca
 * KONFIG alanlarini icerir (inUse, EUI-64 kimlikleri, 96B blok alanlari,
 * Unassigned kesif listesi). Durum/telemri bilgisi (Synced, LastTx, Mod,
 * Freq, ...) /monitor/rf uc noktasinindir - config yanitinda
 * tekrarlanmaz.
 *
 * Bilinen davranis farki (kasitli duzeltme): HatKopukHatBosta float alani
 * eski kodda %u ile basilirdi; artik %.1f ile basilir.
 */

#include "rf_json.h"
#include "rf_config.h"
#include "rf_discovery.h"
#include "xprintf.h"

#include <stddef.h>
#include <string.h>

/* ======================================================================
 * Alan tanimlari
 * ====================================================================== */

/* Tip = bayt boyu + bicim. */
#define RFJ_U8   0U
#define RFJ_U16  1U
#define RFJ_F1   2U
#define RFJ_F2   3U
#define RFJ_F3   4U

typedef struct
{
    const char *name;    /* JSON anahtari */
    uint8_t     type;    /* RFJ_* */
    uint16_t    off;     /* rf_feeder_config_t icinde ofset */
} rfj_desc_t;

/* --- Fider ana dizileri (config skalerleri) --- */
static const rfj_desc_t blk_fields[] =
{
    {"HatID",                         RFJ_U8,  offsetof(rf_feeder_config_t, fider_id)},
    {"ZoneID",                        RFJ_U8,  offsetof(rf_feeder_config_t, zone_id)},
    {"CalismaModu",                   RFJ_U8,  offsetof(rf_feeder_config_t, operating_mode)},
    {"SistemNominalAkimi",            RFJ_F1,  offsetof(rf_feeder_config_t, nominal_current)},
    {"SetEdilebilirActirmaEsikAkimi", RFJ_F1,  offsetof(rf_feeder_config_t, ia_threshold)},
    {"SetEdilebilirAcmaArizaSayisi",  RFJ_U8,  offsetof(rf_feeder_config_t, set_count)},
    {"ArtimliAkimEsigi",              RFJ_F1,  offsetof(rf_feeder_config_t, di_dt_threshold)},
    {"HatKopukHatBosta",              RFJ_F1,  offsetof(rf_feeder_config_t, line_break_threshold)},
    {"OluHatAkimiDogrulamaSuresi",    RFJ_U16, offsetof(rf_feeder_config_t, dead_line_verify_ms)},
    {"YenilenmeSifirlamaSuresi",      RFJ_U16, offsetof(rf_feeder_config_t, t_reclaim_sec)},
    {"HatFrekansi",                   RFJ_U8,  offsetof(rf_feeder_config_t, line_frequency)},
    {"IsSafety",                      RFJ_F3,  offsetof(rf_feeder_config_t, is_safety)},
    {"ThresholdMs",                   RFJ_U16, offsetof(rf_feeder_config_t, threshold_ms)},
    {"TMemDeadSec",                   RFJ_U16, offsetof(rf_feeder_config_t, t_mem_dead_sec)},
    {"InrushTimerMs",                 RFJ_U16, offsetof(rf_feeder_config_t, inrush_timer_ms)},
    {"InrushMultiplier",              RFJ_F2,  offsetof(rf_feeder_config_t, inrush_multiplier)},
    {"SyncTripDelayMs",               RFJ_U16, offsetof(rf_feeder_config_t, sync_trip_delay_ms)},
    {"TripPulseDurationMs",           RFJ_U16, offsetof(rf_feeder_config_t, trip_pulse_duration_ms)},
    {"TripMode",                      RFJ_U8,  offsetof(rf_feeder_config_t, trip_mode)},
    {"Inrush100HzRatio",              RFJ_U8,  offsetof(rf_feeder_config_t, inrush_100hz_ratio)},
    {"ClpEnabled",                    RFJ_U8,  offsetof(rf_feeder_config_t, clp_enabled)},
    {"ClpMultiplier",                 RFJ_F2,  offsetof(rf_feeder_config_t, clp_multiplier)},
    {"ClpDurationMs",                 RFJ_U16, offsetof(rf_feeder_config_t, clp_duration_ms)},
    {"VtripTarget",                   RFJ_F2,  offsetof(rf_feeder_config_t, vtrip_target)},
};

/* ======================================================================
 * Deger okuma + yazma yardimcilari
 * ====================================================================== */

static uint8_t rfj_type_size(uint8_t type)
{
    switch (type)
    {
        case RFJ_U8:  return 1U;
        case RFJ_U16: return 2U;
        default:      return 4U;   /* f32 */
    }
}

/** @brief Bir fiderin tanimli degerini oku (store yoksa 0). */
static uint32_t rfj_read(const rfj_desc_t *d, feeder_id_t line)
{
    const rf_feeder_t *cfg = rf_store_get(line);
    const uint8_t *val = NULL;

    if (cfg != NULL)
    {
        val = (const uint8_t *)&cfg->config + d->off;
    }

    if (NULL == val)
    {
        return 0U;
    }

    switch (rfj_type_size(d->type))
    {
        case 1U:  return (uint32_t)(*val);
        case 2U:  { uint16_t v; (void)memcpy(&v, val, sizeof(v)); return (uint32_t)v; }
        default:  { uint32_t v; (void)memcpy(&v, val, sizeof(v)); return v; }
    }
}

/** @brief Tek diziyi bas. */
static unsigned int rfj_emit(char *buf, unsigned int sz, unsigned int pos,
                             const rfj_desc_t *d)
{
    pos += xsnprintf(&buf[pos], (sz - pos), "\"%s\":[", d->name);

    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        uint32_t v = rfj_read(d, (feeder_id_t)i);
        float    fv;

        (void)memcpy(&fv, &v, sizeof(fv));   /* bit-duzeylik: tip-uyumlu */

        switch (d->type)
        {
            case RFJ_F1:
                pos += xsnprintf(&buf[pos], (sz - pos),
                                 "%s%.1f", (i > 0) ? "," : "", fv);
                break;

            case RFJ_F2:
                pos += xsnprintf(&buf[pos], (sz - pos),
                                 "%s%.2f", (i > 0) ? "," : "", fv);
                break;

            case RFJ_F3:
                pos += xsnprintf(&buf[pos], (sz - pos),
                                 "%s%.3f", (i > 0) ? "," : "", fv);
                break;

            default:
                pos += xsnprintf(&buf[pos], (sz - pos),
                                 "%s%u", (i > 0) ? "," : "", v);
                break;
        }
    }

    return pos + xsnprintf(&buf[pos], (sz - pos), "],");
}

/* ======================================================================
 * Ozel bolumler (bool / EUI-64 / kesif listesi)
 * ====================================================================== */

static unsigned int rfj_emit_inuse(char *buf, unsigned int sz, unsigned int pos)
{
    pos += xsnprintf(&buf[pos], (sz - pos), "\"inUse\":[");

    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *cfg = rf_store_get((feeder_id_t)i);
        pos += xsnprintf(&buf[pos], (sz - pos), "%s%s",
                          (i > 0) ? "," : "",
                          ((cfg != NULL) && cfg->in_use) ? "true" : "false");
    }

    return pos + xsnprintf(&buf[pos], (sz - pos), "],");
}

static unsigned int rfj_emit_deviceid(char *buf, unsigned int sz, unsigned int pos,
                                      const char *key, uint16_t off)
{
    pos += xsnprintf(&buf[pos], (sz - pos), "\"%s\":[", key);

    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *cfg = rf_store_get((feeder_id_t)i);
        const uint8_t     *eui = (cfg != NULL)
                               ? (const uint8_t *)cfg + off
                               : NULL;
        char hex[RF_EUI64_HEX_LEN];

        if ((eui != NULL) && !rf_eui64_is_zero(eui))
        {
            rf_eui64_to_hex(eui, hex);
        }
        else
        {
            hex[0] = '\0';
        }

        pos += xsnprintf(&buf[pos], (sz - pos),
                          "%s\"%s\"", (i > 0) ? "," : "", hex);
    }

    return pos + xsnprintf(&buf[pos], (sz - pos), "],");
}

/** @brief Kesif kuyrugundaki EUI herhangi bir fider/faz kutusunda mi? */
static bool rfj_eui_assigned(const uint8_t eui[RF_EUI64_LEN])
{
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        const rf_feeder_t *cfg = rf_store_get((feeder_id_t)i);

        if (cfg == NULL)
        {
            continue;
        }

        if ((memcmp(eui, cfg->r_eui64, RF_EUI64_LEN) == 0) ||
            (memcmp(eui, cfg->s_eui64, RF_EUI64_LEN) == 0) ||
            (memcmp(eui, cfg->t_eui64, RF_EUI64_LEN) == 0))
        {
            return true;
        }
    }

    return false;
}

static unsigned int rfj_emit_unassigned(char *buf, unsigned int sz, unsigned int pos)
{
    const uint8_t count = rf_discovery_get_count();
    int emitted = 0;

    pos += xsnprintf(&buf[pos], (sz - pos), "\"Unassigned\":[");

    for (uint8_t k = 0U; k < count; k++)
    {
        uint8_t eui[RF_EUI64_LEN];
        char hex[RF_EUI64_HEX_LEN];

        if (!rf_discovery_get_device(k, eui))
        {
            break;
        }

        if (rfj_eui_assigned(eui))
        {
            continue;
        }

        rf_eui64_to_hex(eui, hex);
        pos += xsnprintf(&buf[pos], (sz - pos),
                          "%s{\"EUI64\":\"%s\",\"FiderID\":0}",
                          (emitted > 0) ? "," : "", hex);
        emitted++;
    }

    return pos + xsnprintf(&buf[pos], (sz - pos), "]");
}

/* ======================================================================
 * Public API
 * ====================================================================== */

int rf_json_config_build(char *buf, int buf_size)
{
    const unsigned int size = (unsigned int)buf_size;
    unsigned int pos;

    if ((NULL == buf) || (buf_size <= 0))
    {
        return 0;
    }

    pos = xsnprintf(buf, size, "{");
    pos = rfj_emit_inuse(buf, size, pos);

    /* Orijinal alan sirasi: HatID, ZoneID, sonra EUI-64 uclemesi. */
    pos = rfj_emit(buf, size, pos, &blk_fields[0]);
    pos = rfj_emit(buf, size, pos, &blk_fields[1]);
    pos = rfj_emit_deviceid(buf, size, pos, "R_DEVICEID",
                            (uint16_t)offsetof(rf_feeder_t, r_eui64));
    pos = rfj_emit_deviceid(buf, size, pos, "S_DEVICEID",
                            (uint16_t)offsetof(rf_feeder_t, s_eui64));
    pos = rfj_emit_deviceid(buf, size, pos, "T_DEVICEID",
                            (uint16_t)offsetof(rf_feeder_t, t_eui64));

    for (size_t k = 2U; k < (sizeof(blk_fields) / sizeof(blk_fields[0])); k++)
    {
        pos = rfj_emit(buf, size, pos, &blk_fields[k]);
    }

    /* Tum diziler "]," ile biter; Unassigned son alandir. */
    pos = rfj_emit_unassigned(buf, size, pos);
    pos += xsnprintf(&buf[pos], (size - pos), "}");

    if (pos >= (size - 1U))
    {
        return (buf_size - 1);   /* tasma isaretle: cagiran uyarmali */
    }

    return (int)pos;
}

/*** end of file ***/
