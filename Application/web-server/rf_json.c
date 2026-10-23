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
 *
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */

#include "rf_json.h"
#include "rf_config.h"
#include "rf_discovery.h"
#include "web_json.h"

#include <stddef.h>
#include <string.h>

/* ======================================================================
 * Alan tanimlari
 * ====================================================================== */

/* Tip = bayt boyu + bicim. */
#define RFJ_U8 0U
#define RFJ_U16 1U
#define RFJ_F1 2U
#define RFJ_F2 3U
#define RFJ_F3 4U

typedef struct
{
    const char *name; /* JSON anahtari */
    uint8_t type;     /* RFJ_* */
    uint16_t off;     /* rf_feeder_config_t icinde ofset */
} rfj_desc_t;

/* --- Fider ana dizileri (config skalerleri) --- */
static const rfj_desc_t blk_fields[] = {
    {"HatID", RFJ_U8, offsetof(rf_feeder_config_t, fider_id)},
    {"ZoneID", RFJ_U8, offsetof(rf_feeder_config_t, zone_id)},
    {"CalismaModu", RFJ_U8, offsetof(rf_feeder_config_t, operating_mode)},
    {"SistemNominalAkimi", RFJ_F1,
     offsetof(rf_feeder_config_t, nominal_current)},
    {"SetEdilebilirActirmaEsikAkimi", RFJ_F1,
     offsetof(rf_feeder_config_t, ia_threshold)},
    {"SetEdilebilirAcmaArizaSayisi", RFJ_U8,
     offsetof(rf_feeder_config_t, set_count)},
    {"ArtimliAkimEsigi", RFJ_F1, offsetof(rf_feeder_config_t, di_dt_threshold)},
    {"HatKopukHatBosta", RFJ_F1,
     offsetof(rf_feeder_config_t, line_break_threshold)},
    {"OluHatAkimiDogrulamaSuresi", RFJ_U16,
     offsetof(rf_feeder_config_t, dead_line_verify_ms)},
    {"YenilenmeSifirlamaSuresi", RFJ_U16,
     offsetof(rf_feeder_config_t, t_reclaim_sec)},
    {"HatFrekansi", RFJ_U8, offsetof(rf_feeder_config_t, line_frequency)},
    {"IsSafety", RFJ_F3, offsetof(rf_feeder_config_t, is_safety)},
    {"ThresholdMs", RFJ_U16, offsetof(rf_feeder_config_t, threshold_ms)},
    {"TMemDeadSec", RFJ_U16, offsetof(rf_feeder_config_t, t_mem_dead_sec)},
    {"InrushTimerMs", RFJ_U16, offsetof(rf_feeder_config_t, inrush_timer_ms)},
    {"InrushMultiplier", RFJ_F2,
     offsetof(rf_feeder_config_t, inrush_multiplier)},
    {"SyncTripDelayMs", RFJ_U16,
     offsetof(rf_feeder_config_t, sync_trip_delay_ms)},
    {"TripPulseDurationMs", RFJ_U16,
     offsetof(rf_feeder_config_t, trip_pulse_duration_ms)},
    {"TripMode", RFJ_U8, offsetof(rf_feeder_config_t, trip_mode)},
    {"Inrush100HzRatio", RFJ_U8,
     offsetof(rf_feeder_config_t, inrush_100hz_ratio)},
    {"ClpEnabled", RFJ_U8, offsetof(rf_feeder_config_t, clp_enabled)},
    {"ClpMultiplier", RFJ_F2, offsetof(rf_feeder_config_t, clp_multiplier)},
    {"ClpDurationMs", RFJ_U16, offsetof(rf_feeder_config_t, clp_duration_ms)},
    {"VtripTarget", RFJ_F2, offsetof(rf_feeder_config_t, vtrip_target)},
};

/* ======================================================================
 * Deger okuma + yazma yardimcilari
 * ====================================================================== */

static uint8_t rfj_type_size(uint8_t type)
{
    switch (type)
    {
    case RFJ_U8:
        return 1U;
    case RFJ_U16:
        return 2U;
    default:
        return 4U; /* f32 */
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
    case 1U:
        return (uint32_t)(*val);
    case 2U:
    {
        uint16_t v;
        (void)memcpy(&v, val, sizeof(v));
        return (uint32_t)v;
    }
    default:
    {
        uint32_t v;
        (void)memcpy(&v, val, sizeof(v));
        return v;
    }
    }
}

static void rfj_emit(json_writer_t *writer, const rfj_desc_t *field)
{
    web_json_array(writer, field->name);
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; ++index)
    {
        uint32_t bits = rfj_read(field, (feeder_id_t)index);
        if (field->type == RFJ_U8 || field->type == RFJ_U16)
        {
            (void)json_write_uint32(writer, bits);
        }
        else
        {
            float value;
            (void)memcpy(&value, &bits, sizeof(value));
            uint8_t decimals =
                field->type == RFJ_F1 ? 1U : (field->type == RFJ_F2 ? 2U : 3U);
            web_json_float(writer, value, decimals);
        }
    }
    (void)json_write_array_end(writer);
}

static void rfj_emit_inuse(json_writer_t *writer)
{
    web_json_array(writer, "inUse");
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; ++index)
    {
        const rf_feeder_t *config = rf_store_get((feeder_id_t)index);
        (void)json_write_bool(writer, config != NULL && config->in_use);
    }
    (void)json_write_array_end(writer);
}

static void rfj_emit_deviceid(json_writer_t *writer, const char *name,
                              size_t offset)
{
    web_json_array(writer, name);
    for (size_t index = 0U; index < MAX_POWER_LINE_COUNT; ++index)
    {
        const rf_feeder_t *config = rf_store_get((feeder_id_t)index);
        char hex[RF_EUI64_HEX_LEN] = "";
        if (config != NULL)
        {
            const uint8_t *eui = (const uint8_t *)config + offset;
            if (!rf_eui64_is_zero(eui))
            {
                rf_eui64_to_hex(eui, hex);
            }
        }
        (void)json_write_string(writer, web_json_span(hex));
    }
    (void)json_write_array_end(writer);
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

static void rfj_emit_unassigned(json_writer_t *writer)
{
    web_json_array(writer, "Unassigned");
    uint8_t count = rf_discovery_get_count();
    for (uint8_t index = 0U; index < count; ++index)
    {
        uint8_t eui[RF_EUI64_LEN];
        char hex[RF_EUI64_HEX_LEN];
        if (!rf_discovery_get_device(index, eui))
        {
            break;
        }
        if (rfj_eui_assigned(eui))
        {
            continue;
        }
        rf_eui64_to_hex(eui, hex);
        (void)json_write_object_begin(writer);
        web_json_string(writer, "EUI64", hex);
        web_json_uint32(writer, "FiderID", 0U);
        (void)json_write_object_end(writer);
    }
    (void)json_write_array_end(writer);
}

int rf_json_config_build(char *buffer, int capacity)
{
    if (buffer == NULL || capacity <= 0)
    {
        return 0;
    }
    json_writer_t writer;
    size_t length = 0U;
    if (!web_json_init(&writer, buffer, (size_t)capacity))
    {
        return 0;
    }
    (void)json_write_object_begin(&writer);
    rfj_emit_inuse(&writer);
    rfj_emit(&writer, &blk_fields[0]);
    rfj_emit(&writer, &blk_fields[1]);
    rfj_emit_deviceid(&writer, "R_DEVICEID", offsetof(rf_feeder_t, r_eui64));
    rfj_emit_deviceid(&writer, "S_DEVICEID", offsetof(rf_feeder_t, s_eui64));
    rfj_emit_deviceid(&writer, "T_DEVICEID", offsetof(rf_feeder_t, t_eui64));
    for (size_t index = 2U; index < sizeof(blk_fields) / sizeof(blk_fields[0]);
         ++index)
    {
        rfj_emit(&writer, &blk_fields[index]);
    }
    rfj_emit_unassigned(&writer);
    (void)json_write_object_end(&writer);
    if (!web_json_finish(&writer, buffer, &length))
    {
        return 0;
    }
    /* The supplied int capacity bounds length. */
    return (int)length;
}

/*** end of file ***/
