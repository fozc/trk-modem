/*
 * rf_config.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 *
 * RF ayirici konfigurasyonu: 96 baytlik blok codec (default / CRC / RMW,
 * spec R2 Ek-A + R2-ek/ek2/ek3/ek4) + RAM SSOT store + staging.
 */

#include "rf_config.h"
#include "types.h"
#include "nvram.h"
#include <string.h>

#define breaker_config (nvram_get_breaker_rw())

/* ---------------------------------------------------------------------------
 * 96 baytlik blok codec (spec R2 Ek-A).
 * --------------------------------------------------------------------------- */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - SCP kutuphanesiyle ayni
 * parametreler. */
#define RF_CRC16_INIT   0xFFFFU
#define RF_CRC16_POLY   0x1021U

static uint16_t rf_config_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = RF_CRC16_INIT;

    for (size_t idx = 0U; idx < length; idx++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint32_t)data[idx] << 8U));
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)(((uint32_t)crc << 1U) ^ (uint32_t)RF_CRC16_POLY);
            }
            else
            {
                crc = (uint16_t)((uint32_t)crc << 1U);
            }
        }
    }
    return crc;
}

/* Default tablo - spec R2 section 3. rf_config_for_write, device NULL
 * iken bunu taban olarak kullanir. */
static const rf_feeder_config_t rf_config_default =
{
    .zone_id                = 1U,
    .fider_id               = 0U,
    .phase_id               = RF_CONFIG_PHASE_L1,
    .nominal_current        = 6.0f,
    .ia_threshold           = 13.0f,
    .is_safety              = 0.300f,
    .di_dt_threshold        = 1000.0f,
    .line_break_threshold   = 2.0f,
    .line_frequency         = 50U,
    .threshold_ms           = 60U,
    .t_reclaim_sec          = 30U,
    .t_mem_dead_sec         = 180U,
    .inrush_timer_ms        = 60U,
    .inrush_multiplier      = 5.0f,
    .dead_line_verify_ms    = 200U,
    .sync_trip_delay_ms     = 100U,
    .trip_pulse_duration_ms = 40U,
    .set_count              = 3U,
    .operating_mode         = 0U,
    .trip_mode              = 0U,
    .inrush_100hz_ratio     = 39U,
    .clp_enabled            = 0U,
    .clp_multiplier         = 2.0f,
    .clp_duration_ms        = 5000U,
    .vtrip_target           = 32.0f,
    .rf_channel             = 0U,
    .rf_atim                = 0U,
    .last_modem_eui         = {0U},
    .rf_reserved            = {0U},
    .crc16                  = 0U
};

const rf_feeder_config_t *rf_config_default_block_get(void)
{
    return &rf_config_default;
}

void rf_config_defaults(rf_feeder_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    cfg->config = rf_config_default;
    memset(cfg->r_eui64, 0, RF_EUI64_LEN);
    memset(cfg->s_eui64, 0, RF_EUI64_LEN);
    memset(cfg->t_eui64, 0, RF_EUI64_LEN);
    cfg->in_use = false;
}

void rf_config_copy_writable(const rf_feeder_config_t *src,
                             rf_feeder_config_t *dst)
{
    if ((src == NULL) || (dst == NULL))
    {
        return;
    }

    (void)memcpy(((uint8_t *)dst) + RF_CONFIG_WRITABLE_OFFS,
                 ((const uint8_t *)src) + RF_CONFIG_WRITABLE_OFFS,
                 RF_CONFIG_WRITABLE_LEN);
}

bool rf_config_for_write(const rf_feeder_config_t *ram,
                         const rf_feeder_config_t *device,
                         rf_feeder_config_t *out)
{
    if ((ram == NULL) || (out == NULL))
    {
        return false;
    }

    /* RMW tabani (spec R2 section 5.4 + 5.3-7): cihazdan son okunan blok
     * varsa tamami ondan kopyalanir; maskeli alanlar (toploloji @0-2 + RF
     * ailesi @57-66) boylece cihaz degerinde korunur - cihaz zaten yok
     * sayar, RTU override etmez. Yoksa default taban. */
    if (device != NULL)
    {
        (void)memcpy(out, device, sizeof(rf_feeder_config_t));
    }
    else
    {
        (void)memcpy(out, &rf_config_default, sizeof(rf_feeder_config_t));
        /* Ilk-imaj yolu: phase bilgi-amacli RAM modelinden alinir. */
        out->phase_id = ram->phase_id;
    }

    /* Yalnizca yazilabilir aralik (@3-56) RAM modelinden gelir. */
    rf_config_copy_writable(ram, out);

    /* RF-rezervi her zaman sifir (spec Ek-A). */
    (void)memset(out->rf_reserved, 0, sizeof(out->rf_reserved));

    /* Blok CRC-16 (ofset 0-93). 0x22-yazim yolunda denetlenmez (R2-ek4);
     * hesap zararsizdir ve FRAM-boot benzeri kullanim icin dogrudur. */
    out->crc16 = rf_config_crc16((const uint8_t *)out,
                                   RF_CONFIG_CRC_RANGE_LEN);

    return true;
}

bool rf_config_crc_ok(const rf_feeder_config_t *blk)
{
    bool is_valid = false;

    if (blk != NULL)
    {
        const uint16_t computed =
            rf_config_crc16((const uint8_t *)blk, RF_CONFIG_CRC_RANGE_LEN);
        is_valid = (computed == blk->crc16) ? true : false;
    }
    return is_valid;
}

uint16_t rf_config_crc_compute(const rf_feeder_config_t *blk)
{
    uint16_t crc = 0U;

    if (blk != NULL)
    {
        crc = rf_config_crc16((const uint8_t *)blk, RF_CONFIG_CRC_RANGE_LEN);
    }
    return crc;
}

uint16_t rf_config_writable_crc(const rf_feeder_config_t *blk)
{
    uint16_t crc = 0U;

    if (blk != NULL)
    {
        crc = rf_config_crc16(((const uint8_t *)blk) + RF_CONFIG_WRITABLE_OFFS,
                              RF_CONFIG_WRITABLE_LEN);
    }
    return crc;
}

/* ---------------------------------------------------------------------------
 * RAM SSOT store + staging (plan Faz 1).
 *
 * Operasyonel SSOT RAM'de tutulur; NVRAM kopyasi (line[i].rf) last-known
 * aynasidir. RF operasyonu bu senkrona bagimli degildir; yalniz cihaz
 * erisilemediginde acilis fallback'i olarak kullanilir.
 * --------------------------------------------------------------------------- */

static rf_feeder_t rf_ram[MAX_POWER_LINE_COUNT];
static bool rf_ram_initialized = false;

/* Staging: POST parse'i buraya yazar; commit edilene kadar gorunmez. */
static rf_feeder_t staging[MAX_POWER_LINE_COUNT];
static bool staging_active = false;

static void store_ensure_init(void)
{
    if (!rf_ram_initialized)
    {
        rf_store_init();
    }
}

void rf_store_init(void)
{
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        /* NVRAM last-known aynasi: gecerli cihaz erisimi yokken acilis
         * degeri. Cihazdan okuma (SCP fazinda) geldiginde onun sonucu bu
         * yuklemenin yerine gecer. */
        rf_ram[i] = breaker_config->line[i].rf;
    }
    rf_ram_initialized = true;
    staging_active = false;
}

int rf_store_sync(void)
{
    /* RAM -> NVRAM aynasi (last-known kopya). */
    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        breaker_config->line[i].rf = rf_ram[i];
    }
    return nvram_sync(false);
}

const rf_feeder_t* rf_store_get(feeder_id_t line_id)
{
    if (line_id >= MAX_POWER_LINE_COUNT) {
        return NULL;
    }
    store_ensure_init();
    return &rf_ram[line_id];
}

rf_feeder_t* rf_store_get_mutable(feeder_id_t line_id)
{
    if (line_id >= MAX_POWER_LINE_COUNT) {
        return NULL;
    }
    store_ensure_init();
    /* Staging aktifse parse yazimlari staging kopyasina gider; boylece
     * yarida kalan bir POST kalici store'u kirletmez. */
    return staging_active ? &staging[line_id] : &rf_ram[line_id];
}

bool rf_store_set(feeder_id_t line_id, const rf_feeder_t* cfg)
{
    if (line_id >= MAX_POWER_LINE_COUNT || cfg == NULL) {
        return false;
    }
    store_ensure_init();
    rf_ram[line_id] = *cfg;
    return true;
}

bool rf_store_stage_begin(void)
{
    if (staging_active) {
        return false;  /* ic ice staging desteklenmez */
    }
    store_ensure_init();
    (void)memcpy(staging, rf_ram, sizeof(rf_ram));
    staging_active = true;
    return true;
}

void rf_store_stage_commit(void)
{
    if (!staging_active) {
        return;
    }
    (void)memcpy(rf_ram, staging, sizeof(rf_ram));
    staging_active = false;
}

void rf_store_stage_abort(void)
{
    staging_active = false;
}

/* ---------------------------------------------------------------------------
 * EUI-64 yardimcilari: ham 8 bayt (MSB-first) <-> 16-hex string.
 * --------------------------------------------------------------------------- */

void rf_eui64_to_hex(const uint8_t eui[RF_EUI64_LEN], char out[RF_EUI64_HEX_LEN])
{
    static const char hex_chars[] = "0123456789ABCDEF";

    if ((eui == NULL) || (out == NULL)) {
        return;
    }

    for (unsigned int i = 0U; i < RF_EUI64_LEN; i++)
    {
        out[(i * 2)]     = hex_chars[(eui[i] >> 4) & 0x0FU];
        out[(i * 2) + 1] = hex_chars[eui[i] & 0x0FU];
    }
    out[RF_EUI64_HEX_LEN - 1] = '\0';
}

static int hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'A') && (c <= 'F')) { return c - 'A' + 10; }
    if ((c >= 'a') && (c <= 'f')) { return c - 'a' + 10; }
    return -1;
}

bool rf_eui64_from_hex(uint8_t eui[RF_EUI64_LEN], const char *hex)
{
    if ((eui == NULL) || (hex == NULL)) {
        return false;
    }

    /* Bos string = atanmamis (tum-sifir). */
    if (hex[0] == '\0') {
        (void)memset(eui, 0, RF_EUI64_LEN);
        return true;
    }

    for (unsigned int i = 0U; i < RF_EUI64_LEN; i++)
    {
        const int hi = hex_nibble(hex[i * 2]);
        const int lo = hex_nibble(hex[(i * 2) + 1]);
        if ((hi < 0) || (lo < 0)) {
            return false;  /* gecersiz karakter veya kisa string */
        }
        eui[i] = (uint8_t)((hi << 4) | lo);
    }

    if (hex[RF_EUI64_LEN * 2] != '\0') {
        return false;  /* fazladan karakter */
    }
    return true;
}

bool rf_eui64_is_zero(const uint8_t eui[RF_EUI64_LEN])
{
    bool is_zero = true;

    if (eui != NULL) {
        for (unsigned int i = 0U; i < RF_EUI64_LEN; i++) {
            if (eui[i] != 0U) {
                is_zero = false;
                break;
            }
        }
    }
    return is_zero;
}

/*** end of file ***/
