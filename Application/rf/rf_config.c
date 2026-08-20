/*
 * rf_config.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 *
 * RF ayirici konfigurasyonu: 96 baytlik blok codec (default / CRC / RMW,
 * spec R2 Ek-A + R2-ek/ek2/ek3/ek4) + eski NVRAM sarmalayici API.
 */

#include "rf_config.h"
#include "types.h"
#include "nvram.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * ESKI API - NVRAM ince sarmalayici (plan Faz 1-2'de kaldirilacak).
 * --------------------------------------------------------------------------- */

#define breaker_config (nvram_get_breaker_rw())

int rf_config_sync(void)
{
	return nvram_sync(false);
}

bool rf_config_set(feeder_id_t line_id, const rf_config_t* config)
{
	if(line_id >= MAX_POWER_LINE_COUNT || config == NULL) {
		return false;
	}

	breaker_config->line[line_id].rf_config = *config;
	return true;
}

const rf_config_t* rf_config_get(feeder_id_t line_id)
{
	if(line_id >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}
	return &breaker_config->line[line_id].rf_config;
}

rf_config_t* rf_config_get_mutable(feeder_id_t line_id)
{
	if(line_id >= MAX_POWER_LINE_COUNT) {
		return NULL;
	}
	return &breaker_config->line[line_id].rf_config;
}

/* ---------------------------------------------------------------------------
 * 96 baytlik blok codec (spec R2 Ek-A).
 * --------------------------------------------------------------------------- */

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - SCP kutuphanesiyle ayni
 * parametreler. */
#define RF_CRC16_INIT   0xFFFFU
#define RF_CRC16_POLY   0x1021U

static uint16_t rf_config_crc16(const uint8_t *p_data, size_t length)
{
    uint16_t crc = RF_CRC16_INIT;

    for (size_t idx = 0U; idx < length; idx++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)((uint32_t)p_data[idx] << 8U));
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

/* Default tablo - spec R2 section 3. rf_config_for_write, p_device NULL
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

void rf_config_defaults(rf_feeder_t *p_cfg)
{
    if (p_cfg == NULL)
    {
        return;
    }

    p_cfg->config = rf_config_default;
    memset(p_cfg->r_eui64, 0, RF_EUI64_LEN);
    memset(p_cfg->s_eui64, 0, RF_EUI64_LEN);
    memset(p_cfg->t_eui64, 0, RF_EUI64_LEN);
    p_cfg->in_use = false;
}

void rf_config_copy_writable(const rf_feeder_config_t *p_src,
                             rf_feeder_config_t *p_dst)
{
    if ((p_src == NULL) || (p_dst == NULL))
    {
        return;
    }

    (void)memcpy(((uint8_t *)p_dst) + RF_CONFIG_WRITABLE_OFFS,
                 ((const uint8_t *)p_src) + RF_CONFIG_WRITABLE_OFFS,
                 RF_CONFIG_WRITABLE_LEN);
}

bool rf_config_for_write(const rf_feeder_config_t *p_ram,
                         const rf_feeder_config_t *p_device,
                         rf_feeder_config_t *p_out)
{
    if ((p_ram == NULL) || (p_out == NULL))
    {
        return false;
    }

    /* RMW tabani (spec R2 section 5.4 + 5.3-7): cihazdan son okunan blok
     * varsa tamami ondan kopyalanir; maskeli alanlar (toploloji @0-2 + RF
     * ailesi @57-66) boylece cihaz degerinde korunur - cihaz zaten yok
     * sayar, RTU override etmez. Yoksa default taban. */
    if (p_device != NULL)
    {
        (void)memcpy(p_out, p_device, sizeof(rf_feeder_config_t));
    }
    else
    {
        (void)memcpy(p_out, &rf_config_default, sizeof(rf_feeder_config_t));
        /* Ilk-imaj yolu: phase bilgi-amacli RAM modelinden alinir. */
        p_out->phase_id = p_ram->phase_id;
    }

    /* Yalnizca yazilabilir aralik (@3-56) RAM modelinden gelir. */
    rf_config_copy_writable(p_ram, p_out);

    /* RF-rezervi her zaman sifir (spec Ek-A). */
    (void)memset(p_out->rf_reserved, 0, sizeof(p_out->rf_reserved));

    /* Blok CRC-16 (ofset 0-93). 0x22-yazim yolunda denetlenmez (R2-ek4);
     * hesap zararsizdir ve FRAM-boot benzeri kullanim icin dogrudur. */
    p_out->crc16 = rf_config_crc16((const uint8_t *)p_out,
                                   RF_CONFIG_CRC_RANGE_LEN);

    return true;
}

bool rf_config_crc_ok(const rf_feeder_config_t *p_blk)
{
    bool is_valid = false;

    if (p_blk != NULL)
    {
        const uint16_t computed =
            rf_config_crc16((const uint8_t *)p_blk, RF_CONFIG_CRC_RANGE_LEN);
        is_valid = (computed == p_blk->crc16) ? true : false;
    }
    return is_valid;
}

uint16_t rf_config_crc_compute(const rf_feeder_config_t *p_blk)
{
    uint16_t crc = 0U;

    if (p_blk != NULL)
    {
        crc = rf_config_crc16((const uint8_t *)p_blk, RF_CONFIG_CRC_RANGE_LEN);
    }
    return crc;
}

uint16_t rf_config_writable_crc(const rf_feeder_config_t *p_blk)
{
    uint16_t crc = 0U;

    if (p_blk != NULL)
    {
        crc = rf_config_crc16(((const uint8_t *)p_blk) + RF_CONFIG_WRITABLE_OFFS,
                              RF_CONFIG_WRITABLE_LEN);
    }
    return crc;
}

/*** end of file ***/
