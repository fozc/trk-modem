/*
 * test_rf_config.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * RF config modulu host testleri: 96B blok codec (default / CRC / RMW,
 * spec R2 Ek-A + R2-ek3/ek4) + RAM store/staging + EUI-64 yardimcilari.
 *
 * Kullanim: make run  (Application/rf/test altinda)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>   /* offsetof */
#include <stdint.h>
#include "rf_config.h"
#include "rf_types.h"
#include "mock_nvram.h"

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                        \
    do                                                                \
    {                                                                 \
        if ((cond) != 0)                                              \
        {                                                             \
            test_pass++;                                              \
            printf("PASS: %s\r\n", (name));                            \
        }                                                             \
        else                                                          \
        {                                                             \
            test_fail++;                                              \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__); \
        }                                                             \
    } while (0)

static bool floats_close(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) { d = -d; }
    return (d < 0.001f) ? true : false;
}

/** Bagimsiz CRC-16/CCITT-FALSE referansi (codec ile ayni olmamali). */
static uint16_t ref_crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0U; i < len; i++)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t b = 0U; b < 8U; b++)
        {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U)
                                  : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

/* CRC-16/CCITT-FALSE check value: "123456789" -> 0x29B1. */
static void test_crc16_check_value(void)
{
    TEST_CHECK(ref_crc16_ccitt_false((const uint8_t *)"123456789", 9U) == 0x29B1U,
               "ref CRC-16/CCITT-FALSE check value 0x29B1");
}

static void fill_example_ram(rf_feeder_config_t *ram)
{
    (void)memset(ram, 0, sizeof(*ram));
    ram->zone_id = 2U;
    ram->fider_id = 5U;
    ram->phase_id = RF_CONFIG_PHASE_L2;
    ram->nominal_current = 10.0f;
    ram->ia_threshold = 15.0f;
    ram->is_safety = 0.200f;
    ram->di_dt_threshold = 500.0f;
    ram->line_break_threshold = 3.5f;
    ram->line_frequency = 60U;
    ram->threshold_ms = 90U;
    ram->t_reclaim_sec = 90U;
    ram->t_mem_dead_sec = 300U;
    ram->inrush_timer_ms = 40U;
    ram->inrush_multiplier = 7.5f;
    ram->dead_line_verify_ms = 150U;
    ram->sync_trip_delay_ms = 50U;
    ram->trip_pulse_duration_ms = 60U;
    ram->set_count = 2U;
    ram->operating_mode = 1U;
    ram->trip_mode = 1U;
    ram->inrush_100hz_ratio = 25U;
    ram->clp_enabled = 1U;
    ram->clp_multiplier = 2.5f;
    ram->clp_duration_ms = 4000U;
    ram->vtrip_target = 36.0f;
}

static void test_layout(void)
{
    /* 1) Yapi boyutu ve ofsetler (spec R2 Ek-A) - _Static_assert ayrica korur */
    TEST_CHECK(sizeof(rf_feeder_config_t) == 96U, "struct size == 96");
    TEST_CHECK(offsetof(rf_feeder_config_t, zone_id) == 0U, "offset zone_id=0");
    TEST_CHECK(offsetof(rf_feeder_config_t, phase_id) == 2U, "offset phase_id=2");
    TEST_CHECK(offsetof(rf_feeder_config_t, nominal_current) == 3U, "offset nominal_current=3");
    TEST_CHECK(offsetof(rf_feeder_config_t, line_frequency) == 23U, "offset line_frequency=23");
    TEST_CHECK(offsetof(rf_feeder_config_t, vtrip_target) == 53U, "offset vtrip_target=53");
    TEST_CHECK(offsetof(rf_feeder_config_t, rf_channel) == 57U, "offset rf_channel=57");
    TEST_CHECK(offsetof(rf_feeder_config_t, rf_atim) == 58U, "offset rf_atim=58");
    TEST_CHECK(offsetof(rf_feeder_config_t, last_modem_eui) == 59U, "offset last_modem_eui=59");
    TEST_CHECK(offsetof(rf_feeder_config_t, rf_reserved) == 67U, "offset rf_reserved=67");
    TEST_CHECK(offsetof(rf_feeder_config_t, crc16) == 94U, "offset crc16=94");
    TEST_CHECK(sizeof(rf_feeder_t) == 121U, "rf_feeder_t size == 121");
}

static void test_defaults(void)
{
    const rf_feeder_config_t *d = rf_config_default_block_get();

    TEST_CHECK(d != NULL, "default block available");
    TEST_CHECK(d->zone_id == 1U && d->fider_id == 0U, "default topology zone=1 fider=0");
    TEST_CHECK(floats_close(d->nominal_current, 6.0f), "default nominal 6.0");
    TEST_CHECK(floats_close(d->ia_threshold, 13.0f), "default ia 13.0");
    TEST_CHECK(floats_close(d->is_safety, 0.300f), "default is_safety 0.300");
    TEST_CHECK(floats_close(d->di_dt_threshold, 1000.0f), "default di_dt 1000");
    TEST_CHECK(floats_close(d->line_break_threshold, 2.0f), "default line_break 2.0");
    TEST_CHECK(d->line_frequency == 50U, "default freq 50");
    TEST_CHECK(d->threshold_ms == 60U, "default threshold_ms 60");
    TEST_CHECK(d->t_reclaim_sec == 30U, "default t_reclaim 30");
    TEST_CHECK(d->t_mem_dead_sec == 180U, "default t_mem_dead 180");
    TEST_CHECK(d->inrush_timer_ms == 60U, "default inrush_timer 60");
    TEST_CHECK(floats_close(d->inrush_multiplier, 5.0f), "default inrush_mult 5");
    TEST_CHECK(d->dead_line_verify_ms == 200U, "default dead_line 200");
    TEST_CHECK(d->sync_trip_delay_ms == 100U, "default sync_trip 100");
    TEST_CHECK(d->trip_pulse_duration_ms == 40U, "default trip_pulse 40");
    TEST_CHECK(d->set_count == 3U, "default set_count 3");
    TEST_CHECK(d->operating_mode == 0U, "default op_mode 0");
    TEST_CHECK(d->trip_mode == 0U, "default trip_mode 0");
    TEST_CHECK(d->inrush_100hz_ratio == 39U, "default inrush_100hz 39");
    TEST_CHECK(d->clp_enabled == 0U, "default clp_enabled 0");
    TEST_CHECK(floats_close(d->clp_multiplier, 2.0f), "default clp_mult 2");
    TEST_CHECK(d->clp_duration_ms == 5000U, "default clp_duration 5000");
    TEST_CHECK(floats_close(d->vtrip_target, 32.0f), "default vtrip 32");

    /* Feeder kaydi default'u */
    {
        rf_feeder_t cfg;
        rf_config_defaults(&cfg);
        TEST_CHECK(cfg.in_use == false, "feeder default in_use=false");
        TEST_CHECK(rf_eui64_is_zero(cfg.r_eui64) &&
                   rf_eui64_is_zero(cfg.s_eui64) &&
                   rf_eui64_is_zero(cfg.t_eui64), "feeder default EUI'ler atanmamis");
        TEST_CHECK(floats_close(cfg.config.nominal_current, 6.0f),
                   "feeder default config = default blok");
    }
}

static void test_for_write_rmw(void)
{
    rf_feeder_config_t ram_blk;
    rf_feeder_config_t dev_blk;
    rf_feeder_config_t out;

    fill_example_ram(&ram_blk);

    (void)memset(&dev_blk, 0, sizeof(dev_blk));
    dev_blk.zone_id = 4U;
    dev_blk.fider_id = 3U;
    dev_blk.phase_id = RF_CONFIG_PHASE_L3;
    dev_blk.rf_channel = 17U;
    dev_blk.rf_atim = 9U;
    for (size_t k = 0U; k < 8U; k++)
    {
        dev_blk.last_modem_eui[k] = (uint8_t)(0x11U * (uint8_t)(k + 1U));
    }
    dev_blk.vtrip_target = 30.0f;  /* RAM'de 36.0 gelmeli */
    dev_blk.crc16 = rf_config_crc_compute(&dev_blk);

    TEST_CHECK(rf_config_for_write(&ram_blk, &dev_blk, &out) == true, "for_write(dev) OK");
    TEST_CHECK(rf_config_crc_ok(&out) == true, "for_write(dev) CRC valid");
    TEST_CHECK(out.zone_id == 4U, "RMW zone from device");
    TEST_CHECK(out.fider_id == 3U, "RMW fider from device");
    TEST_CHECK(out.phase_id == RF_CONFIG_PHASE_L3, "RMW phase from device");
    TEST_CHECK(out.rf_channel == 17U, "RMW rf_channel from device");
    TEST_CHECK(out.rf_atim == 9U, "RMW rf_atim from device");
    TEST_CHECK(memcmp(out.last_modem_eui, dev_blk.last_modem_eui, 8U) == 0,
               "RMW last_modem_eui from device");
    TEST_CHECK(floats_close(out.vtrip_target, 36.0f), "RMW writable vtrip from RAM");
    TEST_CHECK((out.operating_mode == 1U) && (out.clp_enabled == 1U),
               "RMW writable flags from RAM");
    {
        bool res_zero = true;
        for (size_t i = 0U; i < sizeof(out.rf_reserved); i++)
        {
            if (out.rf_reserved[i] != 0U) { res_zero = false; break; }
        }
        TEST_CHECK(res_zero == true, "for_write rf_reserved zeroed");
    }
    TEST_CHECK(rf_config_crc_compute(&out) ==
               ref_crc16_ccitt_false((const uint8_t *)&out, RF_CONFIG_CRC_RANGE_LEN),
               "codec CRC == bagimsiz referans");

    /* device = NULL yolu: default taban + RAM writable + RAM phase. */
    TEST_CHECK(rf_config_for_write(&ram_blk, NULL, &out) == true, "for_write(default) OK");
    TEST_CHECK(out.zone_id == rf_config_default_block_get()->zone_id,
               "default-taban zone");
    TEST_CHECK(out.phase_id == RF_CONFIG_PHASE_L2, "default-taban phase RAM'den");
    TEST_CHECK(floats_close(out.vtrip_target, 36.0f), "default-taban writable RAM'den");
    TEST_CHECK(rf_config_crc_ok(&out) == true, "default-taban CRC valid");

    /* NULL parametre reddi. */
    TEST_CHECK(rf_config_for_write(NULL, &dev_blk, &out) == false, "for_write NULL ram reddi");
    TEST_CHECK(rf_config_for_write(&ram_blk, &dev_blk, NULL) == false, "for_write NULL out reddi");
}

static void test_writable_crc(void)
{
    rf_feeder_config_t blk;
    const rf_feeder_config_t *d = rf_config_default_block_get();

    blk = *d;
    TEST_CHECK(rf_config_writable_crc(&blk) ==
               ref_crc16_ccitt_false(((const uint8_t *)&blk) + RF_CONFIG_WRITABLE_OFFS,
                                     RF_CONFIG_WRITABLE_LEN),
               "writable_crc == referans (@3-56)");

    /* Writable alan degisince cfg_crc degismeli; maskeli alan degisince
     * DEGISMEMELI (cfg_crc yalniz yazilabilir alanlari kapsar - R2-ek3). */
    blk = *d;
    blk.nominal_current = 99.0f;
    TEST_CHECK(rf_config_writable_crc(&blk) != rf_config_writable_crc(d),
               "cfg_crc writable degisimden etkilenir");

    blk = *d;
    blk.rf_channel = 42U;
    blk.rf_atim = 7U;
    blk.zone_id = 5U;
    TEST_CHECK(rf_config_writable_crc(&blk) == rf_config_writable_crc(d),
               "cfg_crc maskeli degisimden etkilenmez");
}

static void test_store_and_staging(void)
{
    const rf_feeder_t *ro = NULL;
    rf_feeder_t *rw = NULL;
    rf_feeder_t probe;

    mock_nvram_reset();

    /* Init: NVRAM aynasindan yukleme. */
    rf_store_init();
    ro = rf_store_get(FEEDER_1);
    TEST_CHECK(ro != NULL, "store get OK");
    TEST_CHECK(ro->in_use == false, "init sonrasi in_use=false (mock sifir)");

    /* Sync: RAM -> NVRAM aynasi. */
    probe = *ro;
    probe.in_use = true;
    probe.config.fider_id = 2U;
    TEST_CHECK(rf_store_set(FEEDER_1, &probe) == true, "store set OK");
    TEST_CHECK(rf_store_sync() == 0, "store sync OK");
    TEST_CHECK(mock_nvram_sync_count() == 1, "nvram_sync bir kez cagrildi");
    TEST_CHECK(nvram_get_breaker_rw()->line[FEEDER_1].rf.config.fider_id == 2U,
               "NVRAM aynasina yazildi");

    /* Staging: yarim kalan POST kirletmemeli. */
    rf_store_init();   /* NVRAM'den geri yukle (fider_id=2) */
    ro = rf_store_get(FEEDER_1);
    TEST_CHECK(ro->config.fider_id == 2U, "re-init NVRAM aynasindan yukler");

    TEST_CHECK(rf_store_stage_begin() == true, "stage_begin OK");
    TEST_CHECK(rf_store_stage_begin() == false, "ic ice staging reddedilir");
    rw = rf_store_get_mutable(FEEDER_1);
    rw->config.fider_id = 7U;   /* parse yarida kaldi simülasyonu */
    rf_store_stage_abort();
    ro = rf_store_get(FEEDER_1);
    TEST_CHECK(ro->config.fider_id == 2U, "abort sonrasi committed deger korunur");

    /* Commit yolu: staging -> committed. */
    TEST_CHECK(rf_store_stage_begin() == true, "ikinci stage_begin OK");
    rw = rf_store_get_mutable(FEEDER_1);
    rw->config.fider_id = 4U;
    rf_store_stage_commit();
    ro = rf_store_get(FEEDER_1);
    TEST_CHECK(ro->config.fider_id == 4U, "commit sonrasi yeni deger gorunur");

    /* Sinir kontrolleri. */
    TEST_CHECK(rf_store_get(MAX_POWER_LINE_COUNT) == NULL, "store get sinir disi NULL");
    TEST_CHECK(rf_store_get_mutable(MAX_POWER_LINE_COUNT) == NULL,
               "store get_mutable sinir disi NULL");
    TEST_CHECK(rf_store_set(MAX_POWER_LINE_COUNT, &probe) == false,
               "store set sinir disi reddi");
}

static void test_eui64(void)
{
    uint8_t eui[RF_EUI64_LEN];
    char hex[RF_EUI64_HEX_LEN];
    const char *sample = "A40567821C3B9F40";

    TEST_CHECK(rf_eui64_from_hex(eui, sample) == true, "from_hex gecerli");
    TEST_CHECK(eui[0] == 0xA4U && eui[7] == 0x40U, "from_hex MSB-first bayt sirasi");
    rf_eui64_to_hex(eui, hex);
    TEST_CHECK(strcmp(hex, sample) == 0, "to_hex/from_hex round-trip");

    TEST_CHECK(rf_eui64_from_hex(eui, "") == true, "bos string = atanmamis");
    TEST_CHECK(rf_eui64_is_zero(eui) == true, "bos string sonrasi tum-sifir");

    TEST_CHECK(rf_eui64_from_hex(eui, "123") == false, "kisa string reddi");
    TEST_CHECK(rf_eui64_from_hex(eui, "A40567821C3B9F4G") == false,
               "gecersiz karakter reddi");
    TEST_CHECK(rf_eui64_from_hex(eui, "A40567821C3B9F4012") == false,
               "fazladan karakter reddi");

    rf_eui64_from_hex(eui, "0000000000000001");
    TEST_CHECK(rf_eui64_is_zero(eui) == false, "tek set bit = atanmis");
}

int main(void)
{
    test_crc16_check_value();
    test_layout();
    test_defaults();
    test_for_write_rmw();
    test_writable_crc();
    test_store_and_staging();
    test_eui64();

    printf("\r\n==== rf_config host tests: %u pass / %u fail ====\r\n",
           test_pass, test_fail);
    return (test_fail == 0U) ? 0 : 1;
}

/*** end of file ***/
