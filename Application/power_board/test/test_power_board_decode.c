/*
 * test_power_board_decode.c
 *
 *  Created on: Aug 31, 2026
 *      Author: fatih
 *
 * power_board_decode host testleri: XSUM, PROT_VER reddi, isaretli SoC/IBUS,
 * yeni 0x4A-0x4F alanlari, guc blogu ve lastgasp cozumlemesi.
 *
 * Kullanim: make run  (Application/power_board/test altinda)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "power_board_decode.h"

static unsigned int test_pass = 0U;
static unsigned int test_fail = 0U;

#define TEST_CHECK(cond, name)                                        \
    do                                                                \
    {                                                                 \
        if ((cond) != 0)                                              \
        {                                                             \
            test_pass++;                                              \
            printf("PASS: %s\r\n", (name));                           \
        }                                                             \
        else                                                          \
        {                                                             \
            test_fail++;                                              \
            printf("FAIL: %s  (%s:%d)\r\n", (name), __FILE__, __LINE__); \
        }                                                             \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Yardimcilar                                                        */
/* ------------------------------------------------------------------ */

static void tlm_seal(uint8_t *f)
{
    f[0x5FU] = power_board_xsum(f, 0x5FU);
}

static void tlm_init(uint8_t *f, uint8_t prot_ver)
{
    memset(f, 0, 96U);
    f[0x1EU] = prot_ver;
    tlm_seal(f);
}

static void pwr_seal(uint8_t *b)
{
    b[20U] = power_board_xsum(b, 20U);
}

static void lg_seal(uint8_t *b)
{
    b[26U] = power_board_xsum(b, 26U);
}

static void put_u16(uint8_t *b, uint8_t off, uint16_t v)
{
    b[off] = (uint8_t)(v >> 8);
    b[off + 1U] = (uint8_t)v;
}

static void put_u32(uint8_t *b, uint8_t off, uint32_t v)
{
    b[off] = (uint8_t)(v >> 24);
    b[off + 1U] = (uint8_t)(v >> 16);
    b[off + 2U] = (uint8_t)(v >> 8);
    b[off + 3U] = (uint8_t)v;
}

/* ------------------------------------------------------------------ */
/*  XSUM yardimcisi                                                    */
/* ------------------------------------------------------------------ */

static void test_xsum_helper(void)
{
    uint8_t b[2] = {0U, 0U};
    TEST_CHECK(power_board_xsum(b, 2U) == 0x5AU, "xsum: all-zero -> salt");

    b[0] = 0x01U;
    b[1] = 0x02U;
    TEST_CHECK(power_board_xsum(b, 2U) == 0x59U, "xsum: 01^02^5A = 59");

    TEST_CHECK(power_board_xsum(NULL, 0U) == 0x5AU, "xsum: empty -> salt");
}

/* ------------------------------------------------------------------ */
/*  Telemetri: PROT_VER reddi ve butunluk                              */
/* ------------------------------------------------------------------ */

static void test_telemetry_validity(void)
{
    uint8_t f[96];
    power_board_telemetry_t t;

    /* Gecerli cerceve: PROT_VER 0x09 + dogru XSUM */
    tlm_init(f, 0x09U);
    TEST_CHECK(power_board_decode_telemetry(f, &t) == true,
               "tlm: PROT_VER 0x09 + good xsum -> valid");

    /* Eski surum: XSUM dogru olsa bile RED */
    tlm_init(f, 0x06U);
    TEST_CHECK(power_board_decode_telemetry(f, &t) == false,
               "tlm: PROT_VER 0x06 rejected despite good xsum");
    TEST_CHECK(t.prot_ver == 0x06U,
               "tlm: rejected frame still reports its prot_ver");
    TEST_CHECK(t.soc_x10 == 0,
               "tlm: rejected frame still decodes fields");

    tlm_init(f, 0x08U);
    TEST_CHECK(power_board_decode_telemetry(f, &t) == false,
               "tlm: PROT_VER 0x08 rejected");

    /* Bozuk XSUM */
    tlm_init(f, 0x09U);
    f[0x10U] ^= 0x01U;   /* alan boz, XSUM eski */
    TEST_CHECK(power_board_decode_telemetry(f, &t) == false,
               "tlm: corrupted field -> xsum fail");

    /* NULL guard */
    TEST_CHECK(power_board_decode_telemetry(f, NULL) == false,
               "tlm: NULL out -> false");
    TEST_CHECK(power_board_decode_telemetry(NULL, &t) == false,
               "tlm: NULL frame -> false");
}

/* ------------------------------------------------------------------ */
/*  Telemetri: isaretli alanlar                                        */
/* ------------------------------------------------------------------ */

static void test_telemetry_signed_fields(void)
{
    uint8_t f[96];
    power_board_telemetry_t t;

    /* SoC negatif: -100 -> 0xFF9C */
    tlm_init(f, 0x09U);
    put_u16(f, 0x1AU, (uint16_t)(int16_t)-100);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK(t.soc_x10 == -100, "tlm: SoC -100 decodes signed");

    /* SoC sinirlar: -1000 ve +1000 */
    put_u16(f, 0x1AU, (uint16_t)(int16_t)-1000);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK(t.soc_x10 == -1000, "tlm: SoC lower bound -1000");

    put_u16(f, 0x1AU, 1000U);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK(t.soc_x10 == 1000, "tlm: SoC upper bound +1000");

    /* IBUS artik sozlesmede isaretli */
    put_u16(f, 0x58U, (uint16_t)(int16_t)-250);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK(t.ibus_ma == -250, "tlm: IBUS -250 mA decodes signed");

    /* IBAT zaten isaretliydi */
    put_u16(f, 0x56U, (uint16_t)(int16_t)-1234);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK(t.ibat_ma == -1234, "tlm: IBAT -1234 mA decodes signed");
}

/* ------------------------------------------------------------------ */
/*  Telemetri: yeni 0x4A-0x4F alanlari                                 */
/* ------------------------------------------------------------------ */

static void test_telemetry_new_fields(void)
{
    uint8_t f[96];
    power_board_telemetry_t t;

    tlm_init(f, 0x09U);
    f[0x4AU] = (uint8_t)PB_CHG_REAL_CHARGING;
    f[0x4BU] = 3U;
    f[0x4CU] = 17U;
    f[0x4DU] = 0x05U;
    f[0x4EU] = (uint8_t)PB_PSYS_ST_MEASURED;
    f[0x4FU] = 1U;
    tlm_seal(f);

    TEST_CHECK(power_board_decode_telemetry(f, &t) == true,
               "new fields: frame stays valid (0x4A-0x4F in xsum)");
    TEST_CHECK(t.chg_real == PB_CHG_REAL_CHARGING, "new: CHG_REAL @0x4A");
    TEST_CHECK(t.bq_yas_ds == 3U, "new: BQ_YAS_DS @0x4B");
    TEST_CHECK(t.bq_err_n == 17U, "new: BQ_ERR_N @0x4C");
    TEST_CHECK(t.panic_cause == 0x05U, "new: PANIC_CAUSE @0x4D");
    TEST_CHECK(t.psys_st == PB_PSYS_ST_MEASURED, "new: PSYS_ST @0x4E");
    TEST_CHECK(t.cal_ver == 1U, "new: CAL_VER @0x4F");

    /* Komsu alanlardan sizma yok */
    put_u16(f, 0x50U, 0xABCDU);
    tlm_seal(f);
    (void)power_board_decode_telemetry(f, &t);
    TEST_CHECK((t.chg_real == PB_CHG_REAL_CHARGING) && (t.bq_vsys_mv == 0xABCDU),
               "new: no bleed into 0x50 BQ_VSYS");
}

/* ------------------------------------------------------------------ */
/*  Guc blogu                                                          */
/* ------------------------------------------------------------------ */

static void test_power_block(void)
{
    uint8_t b[21];
    power_board_power_t p;

    memset(b, 0, 21U);
    put_u32(b, 0x00U, 5000U);                 /* PPV  +5.000 W */
    put_u32(b, 0x08U, 0xFFFFFD58U);           /* PSYS -680 mW  */
    put_u32(b, 0x0CU, 0x000002BCU);           /* PBAT +700 mW  */
    pwr_seal(b);

    TEST_CHECK(power_board_decode_power(b, &p) == true, "pwr: valid frame");
    TEST_CHECK(p.ppv_mw == 5000, "pwr: PPV +5000 mW");
    TEST_CHECK(p.psys_mw == -680, "pwr: PSYS -680 mW (negative)");
    TEST_CHECK(p.pbat_mw == 700, "pwr: PBAT +700 mW");

    b[3] ^= 0x80U;
    TEST_CHECK(power_board_decode_power(b, &p) == false, "pwr: xsum fail");

    TEST_CHECK(power_board_decode_power(b, NULL) == false, "pwr: NULL out");
}

/* ------------------------------------------------------------------ */
/*  Lastgasp                                                           */
/* ------------------------------------------------------------------ */

static void test_lastgasp(void)
{
    uint8_t b[27];
    power_board_lastgasp_t g;

    memset(b, 0, 27U);
    b[0] = 0xB5U;                              /* marker */
    b[1] = 2U;                                 /* reason */
    put_u16(b, 2U, 870U);                      /* SoH 87.0% */
    put_u16(b, 22U, 11800U);                   /* VBAT 11.8 V */
    b[25] = 42U;                               /* SoC 42% */
    lg_seal(b);

    TEST_CHECK(power_board_decode_lastgasp(b, &g) == true, "lg: valid frame");
    TEST_CHECK(g.reason == 2U, "lg: reason");
    TEST_CHECK(g.soh_x10 == 870U, "lg: SoH");
    TEST_CHECK(g.vbat_mv == 11800U, "lg: VBAT");
    TEST_CHECK(g.soc_pct == 42U, "lg: SoC pct");

    b[0] = 0x00U;                              /* marker bozuk */
    TEST_CHECK(power_board_decode_lastgasp(b, &g) == false, "lg: marker fail");

    b[0] = 0xB5U;
    b[10] ^= 0x40U;                            /* xsum boz */
    TEST_CHECK(power_board_decode_lastgasp(b, &g) == false, "lg: xsum fail");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    test_xsum_helper();
    test_telemetry_validity();
    test_telemetry_signed_fields();
    test_telemetry_new_fields();
    test_power_block();
    test_lastgasp();

    printf("\r\n=== power_board_decode tests: %u passed, %u failed ===\r\n",
           test_pass, test_fail);
    return (test_fail == 0U) ? 0 : 1;
}
