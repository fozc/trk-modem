/*
 * test_rf_json.c
 *
 *  Created on: Aug 20, 2026
 *      Author: fatih
 *
 * rf_json altin-cikti (golden output) host testi: sabit store/monitor/
 * kesif girisi ile uretilen JSON govdesinin tam metin karsilastirmasi.
 *
 * Beklenen metin, ayni xsnprintf ile (float bicimleri birebir) insa
 * edilir; boylece tablo/ofset/tanim kaymalari ve bicim gerilemeleri
 * yakalanir.
 *
 * Kullanim: make run  (Application/web-server/test altinda)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "rf_json.h"
#include "rf_config.h"
#include "rf_discovery.h"
#include "xprintf.h"
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

static const uint8_t EUI_R[RF_EUI64_LEN] =
{
    0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0x9FU, 0x40U
};
static const uint8_t EUI_S[RF_EUI64_LEN] =
{
    0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0xA0U, 0x50U
};
static const uint8_t EUI_UNASSIGNED[RF_EUI64_LEN] =
{
    0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0xEEU, 0x01U
};
static const uint8_t EUI_ASSIGNED_LATER[RF_EUI64_LEN] =
{
    0xA4U, 0x05U, 0x67U, 0x82U, 0x1CU, 0x3BU, 0xEEU, 0x02U
};

/** Tek fiderin config degerlerini test amacli doldur. */
static void fill_feeder(rf_feeder_t *feeder, int idx)
{
    (void)rf_config_defaults(feeder);

    feeder->in_use             = (idx < 2);
    feeder->config.fider_id    = (uint8_t)((unsigned int)idx + 1U);
    feeder->config.zone_id     = 1U;
    feeder->config.operating_mode = 1U;
    feeder->config.set_count   = 3U;
    feeder->config.line_frequency = 50U;
    feeder->config.nominal_current = 100.0f + (float)idx;
    feeder->config.ia_threshold = 150.0f + (float)idx;
    feeder->config.di_dt_threshold = 1000.5f;
    feeder->config.line_break_threshold = 2.25f;
    feeder->config.dead_line_verify_ms = 200U;
    feeder->config.t_reclaim_sec = 60U;
    feeder->config.is_safety = 0.300f;
    feeder->config.threshold_ms = 60U;
    feeder->config.t_mem_dead_sec = 180U;
    feeder->config.inrush_timer_ms = 60U;
    feeder->config.inrush_multiplier = 5.0f;
    feeder->config.sync_trip_delay_ms = 100U;
    feeder->config.trip_pulse_duration_ms = 40U;
    feeder->config.trip_mode = 0U;
    feeder->config.inrush_100hz_ratio = 39U;
    feeder->config.clp_enabled = 0U;
    feeder->config.clp_multiplier = 2.0f;
    feeder->config.clp_duration_ms = 5000U;
    feeder->config.vtrip_target = 32.0f;
}

static void setup_state(void)
{
    rf_feeder_t f;

    mock_nvram_reset();
    rf_store_init();

    for (int i = 0; i < MAX_POWER_LINE_COUNT; i++)
    {
        fill_feeder(&f, i);

        if (0 == i)
        {
            (void)memcpy(f.r_eui64, EUI_R, RF_EUI64_LEN);
            (void)memcpy(f.s_eui64, EUI_S, RF_EUI64_LEN);
            (void)memcpy(f.t_eui64, EUI_ASSIGNED_LATER, RF_EUI64_LEN);
        }

        (void)rf_store_set((feeder_id_t)i, &f);

    }

    rf_discovery_reset();
    rf_discovery_add(EUI_UNASSIGNED);
    rf_discovery_add(EUI_ASSIGNED_LATER);   /* atanan: listelenmemeli */
}

/** Beklenen JSON'u ayni xsnprintf ile kur (floatlar birebir). */
static void build_expected(char *out, int sz)
{
    char f_nom[7][24];
    char f_ia[7][24];
    char f_didt[24];
    char f_brk[24];
    char f_is[24];
    char f_inm[24];
    char f_clpm[24];
    char f_vt[24];

    for (int i = 0; i < 7; i++)
    {
        (void)xsnprintf(f_nom[i], (unsigned int)sizeof(f_nom[i]), "%.1f", 100.0f + (float)i);
        (void)xsnprintf(f_ia[i], (unsigned int)sizeof(f_ia[i]), "%.1f", 150.0f + (float)i);
    }

    (void)xsnprintf(f_didt, (unsigned int)sizeof(f_didt), "%.1f", 1000.5f);
    (void)xsnprintf(f_brk, (unsigned int)sizeof(f_brk), "%.1f", 2.25f);
    (void)xsnprintf(f_is, (unsigned int)sizeof(f_is), "%.3f", 0.300f);
    (void)xsnprintf(f_inm, (unsigned int)sizeof(f_inm), "%.2f", 5.0f);
    (void)xsnprintf(f_clpm, (unsigned int)sizeof(f_clpm), "%.2f", 2.0f);
    (void)xsnprintf(f_vt, (unsigned int)sizeof(f_vt), "%.2f", 32.0f);

    (void)xsnprintf(out, (unsigned int)sz,
        "{\"inUse\":[true,true,false,false,false,false,false],"
        "\"HatID\":[1,2,3,4,5,6,7],"
        "\"ZoneID\":[1,1,1,1,1,1,1],"
        "\"R_DEVICEID\":[\"A40567821C3B9F40\",\"\",\"\",\"\",\"\",\"\",\"\"],"
        "\"S_DEVICEID\":[\"A40567821C3BA050\",\"\",\"\",\"\",\"\",\"\",\"\"],"
        "\"T_DEVICEID\":[\"A40567821C3BEE02\",\"\",\"\",\"\",\"\",\"\",\"\"],"
        "\"CalismaModu\":[1,1,1,1,1,1,1],"
        "\"SistemNominalAkimi\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"SetEdilebilirActirmaEsikAkimi\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"SetEdilebilirAcmaArizaSayisi\":[3,3,3,3,3,3,3],"
        "\"ArtimliAkimEsigi\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"HatKopukHatBosta\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"OluHatAkimiDogrulamaSuresi\":[200,200,200,200,200,200,200],"
        "\"YenilenmeSifirlamaSuresi\":[60,60,60,60,60,60,60],"
        "\"HatFrekansi\":[50,50,50,50,50,50,50],"
        "\"IsSafety\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"ThresholdMs\":[60,60,60,60,60,60,60],"
        "\"TMemDeadSec\":[180,180,180,180,180,180,180],"
        "\"InrushTimerMs\":[60,60,60,60,60,60,60],"
        "\"InrushMultiplier\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"SyncTripDelayMs\":[100,100,100,100,100,100,100],"
        "\"TripPulseDurationMs\":[40,40,40,40,40,40,40],"
        "\"TripMode\":[0,0,0,0,0,0,0],"
        "\"Inrush100HzRatio\":[39,39,39,39,39,39,39],"
        "\"ClpEnabled\":[0,0,0,0,0,0,0],"
        "\"ClpMultiplier\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"ClpDurationMs\":[5000,5000,5000,5000,5000,5000,5000],"
        "\"VtripTarget\":[%s,%s,%s,%s,%s,%s,%s],"
        "\"Unassigned\":[{\"EUI64\":\"A40567821C3BEE01\",\"FiderID\":0}]}",
        f_nom[0], f_nom[1], f_nom[2], f_nom[3], f_nom[4], f_nom[5], f_nom[6],
        f_ia[0], f_ia[1], f_ia[2], f_ia[3], f_ia[4], f_ia[5], f_ia[6],
        f_didt, f_didt, f_didt, f_didt, f_didt, f_didt, f_didt,
        f_brk, f_brk, f_brk, f_brk, f_brk, f_brk, f_brk,
        f_is, f_is, f_is, f_is, f_is, f_is, f_is,
        f_inm, f_inm, f_inm, f_inm, f_inm, f_inm, f_inm,
        f_clpm, f_clpm, f_clpm, f_clpm, f_clpm, f_clpm, f_clpm,
        f_vt, f_vt, f_vt, f_vt, f_vt, f_vt, f_vt);
}

int main(void)
{
    static char actual[8192];
    static char expected[8192];
    int n;

    setup_state();
    build_expected(expected, (int)sizeof(expected));
    n = rf_json_config_build(actual, (int)sizeof(actual));

    TEST_CHECK(n > 0, "build pozitif uzunluk dondurur");
    TEST_CHECK(n < (int)sizeof(actual), "tampona sigar");
    TEST_CHECK(strlen(actual) == (size_t)n, "uzunluk strlen ile ayni");
    TEST_CHECK(strcmp(actual, expected) == 0, "golden JSON tam eslesme");

    if (strcmp(actual, expected) != 0)
    {
        /* Ilk fark noktasini bas (hata ayiklama). */
        size_t k = 0U;

        while ((k < strlen(actual)) && (k < strlen(expected)) &&
               (actual[k] == expected[k]))
        {
            k++;
        }

        printf("first diff at %lu: act='%.20s' exp='%.20s'\r\n",
               (unsigned long)k, &actual[k], &expected[k]);
    }

    printf("\r\n==== rf_json host tests: %u pass / %u fail ====\r\n",
           test_pass, test_fail);
    return (test_fail == 0U) ? 0 : 1;
}

/*** end of file ***/
