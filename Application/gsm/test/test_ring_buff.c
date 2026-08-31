/*
 * test_ring_buff.c
 *
 *  Created on: Aug 31, 2026
 *      Author: fatih
 *
 * rbuff (gsm/ring_buff) host testleri: init redleri, guard davranislari,
 * FIFO/wrap dogrulugu, dolu-bos uclari, peek (1B/cok bayt/skip),
 * clear, zero-copy linear-block seti, deterministik karma stress.
 *
 * Kullanim: make run  (Application/gsm/test altinda)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "ring_buff.h"

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

#define TEST_BUF_SIZE 16U
static uint8_t  backing_store[TEST_BUF_SIZE];
static rbuff_t  rb;

static void reset_buffer(void)
{
    memset(backing_store, 0xAA, sizeof(backing_store));
    (void)rbuff_init(&rb, backing_store, TEST_BUF_SIZE);
}

/* Deterministik LCG (sabit seed) - test her seferinde ayni diziyi uretir */
static uint32_t lcg_state = 0x12345678U;

static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525U + 1013904223U;
    return lcg_state;
}

/* ------------------------------------------------------------------ */
/*  Init ve guard testleri                                             */
/* ------------------------------------------------------------------ */

static void test_init_rejects(void)
{
    rbuff_t tmp;
    uint8_t store[8];

    TEST_CHECK(rbuff_init(NULL, store, 8U) == false, "init rejects NULL instance");
    TEST_CHECK(rbuff_init(&tmp, NULL, 8U) == false, "init rejects NULL backing");
    TEST_CHECK(rbuff_init(&tmp, store, 0U) == false, "init rejects size 0");
    TEST_CHECK(rbuff_init(&tmp, store, 100U) == false, "init rejects non-pow2 (100)");
    TEST_CHECK(rbuff_init(&tmp, store, 1023U) == false, "init rejects non-pow2 (1023)");
    TEST_CHECK(rbuff_init(&tmp, store, 8U) == true, "init accepts pow2 size");
    TEST_CHECK(rbuff_available(&tmp) == 0U, "fresh init is empty");
    TEST_CHECK(rbuff_available_for_write(&tmp) == 7U, "fresh init free = size-1");
}

static void test_guards_on_zeroed_instance(void)
{
    rbuff_t zero = {0};
    uint8_t byte = 0U;
    uint8_t out[4];
    uint8_t *block_data = NULL;
    uint32_t block_len = 999U;

    /* Sifir instance: hicbir cagri crash olmamali, guvenli deger donmeli */
    TEST_CHECK(rbuff_available(&zero) == 0U, "guard: available = 0");
    TEST_CHECK(rbuff_available_for_write(&zero) == 0U, "guard: available_for_write = 0");
    TEST_CHECK(rbuff_write_byte(&zero, 0x55U) == 1U, "guard: write_byte not written");
    TEST_CHECK(rbuff_write_buff(&zero, out, 4U) == 1U, "guard: write_buff rejected");
    TEST_CHECK(rbuff_read_safe(&zero, &byte) == false, "guard: read_safe false");
    TEST_CHECK(rbuff_peek(&zero, &byte) == false, "guard: peek false");
    TEST_CHECK(rbuff_read_buff(&zero, out, 4U) == 0U, "guard: read_buff = 0");
    TEST_CHECK(rbuff_peek_buff(&zero, 0U, out, 4U) == 0U, "guard: peek_buff = 0");
    TEST_CHECK(rbuff_get_read_block(&zero, &block_data, &block_len) == false,
               "guard: get_read_block false");
    TEST_CHECK(block_data == NULL, "guard: read block data NULL");
    TEST_CHECK(block_len == 0U, "guard: read block len 0");
    TEST_CHECK(rbuff_get_write_block(&zero, &block_data, &block_len) == false,
               "guard: get_write_block false");
    TEST_CHECK(rbuff_skip(&zero, 4U) == 0U, "guard: skip = 0");
    TEST_CHECK(rbuff_advance(&zero, 4U) == 0U, "guard: advance = 0");
    rbuff_clear(&zero); /* no-op olmali, crash olmamali */
    test_pass++;
    printf("PASS: guard: clear no-op\r\n");

    /* NULL cikis pointerlari */
    TEST_CHECK(rbuff_get_read_block(&zero, NULL, &block_len) == false,
               "guard: get_read_block NULL out ptr");
    TEST_CHECK(rbuff_get_write_block(&zero, &block_data, NULL) == false,
               "guard: get_write_block NULL len ptr");
    TEST_CHECK(rbuff_peek(&rb, NULL) == false, "guard: peek NULL out");
}

/* ------------------------------------------------------------------ */
/*  Temel FIFO ve uc durumlar                                          */
/* ------------------------------------------------------------------ */

static void test_fifo_byte(void)
{
    reset_buffer();

    for (uint32_t i = 0U; i < (TEST_BUF_SIZE - 1U); i++)
    {
        TEST_CHECK(rbuff_write_byte(&rb, (uint8_t)(0x10U + i)) == 0U,
                   "write_byte until full succeeds");
    }
    TEST_CHECK(rbuff_write_byte(&rb, 0xFFU) == 1U, "write_byte full rejected");
    TEST_CHECK(rbuff_available(&rb) == (TEST_BUF_SIZE - 1U), "available = capacity");
    TEST_CHECK(rbuff_available_for_write(&rb) == 0U, "free = 0 when full");

    for (uint32_t i = 0U; i < (TEST_BUF_SIZE - 1U); i++)
    {
        uint8_t byte = 0U;
        TEST_CHECK((rbuff_read_safe(&rb, &byte) == true) &&
                   (byte == (uint8_t)(0x10U + i)),
                   "read_safe FIFO order");
    }
    uint8_t extra = 0U;
    TEST_CHECK(rbuff_read_safe(&rb, &extra) == false, "read_safe empty false");
    TEST_CHECK(rbuff_available(&rb) == 0U, "empty after drain");
}

static void test_write_buff_all_or_nothing(void)
{
    uint8_t src[8];
    uint8_t out[8];

    for (uint32_t i = 0U; i < 8U; i++)
    {
        src[i] = (uint8_t)i;
    }

    reset_buffer();
    TEST_CHECK(rbuff_write_buff(&rb, src, 8U) == 0U, "write_buff 8 into 15 free OK");
    TEST_CHECK(rbuff_available(&rb) == 8U, "available after block write");

    /* 15-8=7 free; 8 bayt istek red edilmeli, hicbir sey yazilmamali */
    TEST_CHECK(rbuff_write_buff(&rb, src, 8U) == 1U, "write_buff over free rejected");
    TEST_CHECK(rbuff_available(&rb) == 8U, "rejected write changes nothing");

    TEST_CHECK(rbuff_write_buff(&rb, src, 0U) == 0U, "write_buff len 0 OK");
    TEST_CHECK(rbuff_write_buff(&rb, NULL, 4U) == 1U, "write_buff NULL data rejected");

    TEST_CHECK(rbuff_read_buff(&rb, out, 8U) == 8U, "read_buff full block");
    TEST_CHECK(memcmp(src, out, 8U) == 0, "block content identical");

    TEST_CHECK(rbuff_read_buff(&rb, out, 8U) == 0U, "read_buff empty = 0");
    TEST_CHECK(rbuff_read_buff(&rb, NULL, 8U) == 0U, "read_buff NULL out = 0");
}

static void test_wrap_split_copies(void)
{
    uint8_t src[12];
    uint8_t out[12];

    for (uint32_t i = 0U; i < 12U; i++)
    {
        src[i] = (uint8_t)(0xA0U + i);
    }

    reset_buffer();

    /* head=12, tail=12 durumuna gel */
    TEST_CHECK(rbuff_write_buff(&rb, src, 12U) == 0U, "wrap: prefill 12");
    TEST_CHECK(rbuff_read_buff(&rb, out, 12U) == 12U, "wrap: drain prefill");
    TEST_CHECK(memcmp(src, out, 12U) == 0, "wrap: prefill content");

    /* head=12'den 8 bayt yazma 16 sinirini asar -> bolunmus memcpy (4+4) */
    TEST_CHECK(rbuff_write_buff(&rb, src, 8U) == 0U, "wrap: crossing write");
    TEST_CHECK(rbuff_available(&rb) == 8U, "wrap: available after crossing write");
    TEST_CHECK(rbuff_read_buff(&rb, out, 8U) == 8U, "wrap: crossing read");
    TEST_CHECK(memcmp(src, out, 8U) == 0, "wrap: crossing content identical");

    /* read_buff kismi okumalar: head=4..10 bandinda 6 yaz, 3+3 oku */
    TEST_CHECK(rbuff_write_buff(&rb, src, 6U) == 0U, "wrap: second prefill");
    TEST_CHECK(rbuff_read_buff(&rb, out, 3U) == 3U, "wrap: partial read 1");
    TEST_CHECK(rbuff_read_buff(&rb, out, 3U) == 3U, "wrap: partial read 2");
    TEST_CHECK(memcmp(&src[3], out, 3U) == 0, "wrap: partial content");
    TEST_CHECK(rbuff_available(&rb) == 0U, "wrap: empty again");
}

/* ------------------------------------------------------------------ */
/*  Peek / clear                                                       */
/* ------------------------------------------------------------------ */

static void test_peek_variants(void)
{
    uint8_t src[6];
    uint8_t more[12];
    uint8_t expect[8];
    uint8_t got[8];
    uint8_t out[6];
    uint8_t byte = 0U;

    for (uint32_t i = 0U; i < 6U; i++)
    {
        src[i] = (uint8_t)(0x30U + i);
    }
    for (uint32_t i = 0U; i < 12U; i++)
    {
        more[i] = (uint8_t)(0x40U + i);
    }

    reset_buffer();
    TEST_CHECK(rbuff_peek(&rb, &byte) == false, "peek empty false");

    (void)rbuff_write_buff(&rb, src, 6U);

    TEST_CHECK((rbuff_peek(&rb, &byte) == true) && (byte == 0x30U),
               "peek oldest byte");
    TEST_CHECK(rbuff_available(&rb) == 6U, "peek does not consume");

    TEST_CHECK(rbuff_peek_buff(&rb, 0U, got, 6U) == 6U, "peek_buff full");
    TEST_CHECK(memcmp(src, got, 6U) == 0, "peek_buff content");
    TEST_CHECK(rbuff_peek_buff(&rb, 2U, got, 4U) == 4U, "peek_buff with skip");
    TEST_CHECK(memcmp(&src[2], got, 4U) == 0, "peek_buff skip content");
    TEST_CHECK(rbuff_peek_buff(&rb, 2U, got, 100U) == 4U,
               "peek_buff partial beyond avail");
    TEST_CHECK(rbuff_peek_buff(&rb, 6U, got, 4U) == 0U, "peek_buff skip==avail = 0");
    TEST_CHECK(rbuff_peek_buff(&rb, 7U, got, 4U) == 0U, "peek_buff skip>avail = 0");
    TEST_CHECK(rbuff_peek_buff(&rb, 0U, got, 0U) == 0U, "peek_buff len 0 = 0");
    TEST_CHECK(rbuff_available(&rb) == 6U, "peek_buff does not consume");

    /* Sarmali peek: tail=4'e cek, 12 bayt daha yaz (head 6 -> 2 sarar).
     * Akis: src[4], src[5], more[0..11]  (14 bayt, buffer sonunu asiyor) */
    TEST_CHECK(rbuff_read_buff(&rb, out, 4U) == 4U, "peek wrap: shift tail to 4");
    TEST_CHECK(rbuff_write_buff(&rb, more, 12U) == 0U, "peek wrap: extend over end");
    TEST_CHECK(rbuff_available(&rb) == 14U, "peek wrap: avail 14");

    expect[0] = src[4];
    expect[1] = src[5];
    for (uint32_t i = 0U; i < 6U; i++)
    {
        expect[2U + i] = more[i];
    }
    TEST_CHECK(rbuff_peek_buff(&rb, 0U, got, 8U) == 8U, "peek wrap: 8 bytes");
    TEST_CHECK(memcmp(expect, got, 8U) == 0, "peek wrap: stream head");

    /* skip=9, len=8 -> 5 bayt kalir: more[7..11], kopya 3+2 seklinde bolunur
     * (buff[13..15] + buff[0..1]) */
    TEST_CHECK(rbuff_peek_buff(&rb, 9U, got, 8U) == 5U, "peek wrap: clamped to 5");
    TEST_CHECK(memcmp(got, &more[7], 5U) == 0, "peek wrap: split copy content");
    TEST_CHECK(rbuff_available(&rb) == 14U, "peek wrap: still no consume");

    rbuff_clear(&rb);
    TEST_CHECK(rbuff_available(&rb) == 0U, "clear empties buffer");
    TEST_CHECK(rbuff_available_for_write(&rb) == (TEST_BUF_SIZE - 1U),
               "clear restores full capacity");
    TEST_CHECK(rbuff_read_safe(&rb, &byte) == false, "read after clear false");

    /* clear sonrasi dongu devam etmeli */
    TEST_CHECK(rbuff_write_buff(&rb, src, 6U) == 0U, "write after clear OK");
    TEST_CHECK(rbuff_read_buff(&rb, out, 6U) == 6U, "read after clear OK");
    TEST_CHECK(memcmp(src, out, 6U) == 0, "post-clear content identical");
}

/* ------------------------------------------------------------------ */
/*  Zero-copy linear-block seti                                        */
/* ------------------------------------------------------------------ */

static void test_linear_blocks(void)
{
    uint8_t  extra[7];
    uint8_t  out[8];
    uint8_t  *data = NULL;
    uint32_t len = 0U;
    uint32_t content_ok = 1U;

    for (uint32_t i = 0U; i < 7U; i++)
    {
        extra[i] = (uint8_t)(0x70U + i);
    }

    reset_buffer();

    /* Bos buffer: read block yok, write block tum kapasiteyi gormeli */
    TEST_CHECK(rbuff_get_read_block(&rb, &data, &len) == false, "read block empty false");
    TEST_CHECK(rbuff_get_write_block(&rb, &data, &len) == true, "write block empty true");
    TEST_CHECK(len == (TEST_BUF_SIZE - 1U), "write block len = capacity");
    TEST_CHECK(data == &backing_store[0], "write block points at buff[0]");

    /* Zero-copy write: blok uzerine dogrudan yaz, advance ile yayinla */
    for (uint32_t i = 0U; i < len; i++)
    {
        data[i] = (uint8_t)(0x60U + i);
    }
    TEST_CHECK(rbuff_available(&rb) == 0U, "data invisible before advance");
    TEST_CHECK(rbuff_advance(&rb, len) == len, "advance publishes block");
    TEST_CHECK(rbuff_available(&rb) == len, "data visible after advance");
    TEST_CHECK(rbuff_get_write_block(&rb, &data, &len) == false, "full: write block false");

    /* Zero-copy read: blok [0..14], icerik dogrula */
    TEST_CHECK(rbuff_get_read_block(&rb, &data, &len) == true, "read block full true");
    TEST_CHECK(len == (TEST_BUF_SIZE - 1U), "read block len = 15");
    TEST_CHECK(data == &backing_store[0], "read block at buff[0]");
    content_ok = 1U;
    for (uint32_t i = 0U; i < len; i++)
    {
        if (data[i] != (uint8_t)(0x60U + i))
        {
            content_ok = 0U;
        }
    }
    TEST_CHECK(content_ok == 1U, "read block content");

    TEST_CHECK(rbuff_skip(&rb, 7U) == 7U, "skip 7");
    TEST_CHECK(rbuff_available(&rb) == 8U, "available after skip");
    /* durum: tail=7, head=15, avail=8, free=7 */

    /* Kalan 7 free'yi tam doldur: head 15 -> 6 sarar */
    TEST_CHECK(rbuff_write_buff(&rb, extra, 7U) == 0U, "linear: fill to wrap");
    TEST_CHECK(rbuff_available(&rb) == 15U, "full after fill");

    /* Sarmali read block: tail=7'den baslar, buffer sonunda kapalanir (9) */
    TEST_CHECK(rbuff_get_read_block(&rb, &data, &len) == true, "wrapped read block true");
    TEST_CHECK(len == 9U, "read block capped at buffer end");
    TEST_CHECK(data == &backing_store[7], "read block at buff[7]");
    content_ok = 1U;
    for (uint32_t i = 0U; i < 8U; i++)
    {
        if (data[i] != (uint8_t)(0x60U + 7U + i)) /* 0x67..0x6E */
        {
            content_ok = 0U;
        }
    }
    if (data[8] != 0x70U) /* extra[0] buff[15]'e dustu */
    {
        content_ok = 0U;
    }
    TEST_CHECK(content_ok == 1U, "wrapped block content");

    TEST_CHECK(rbuff_skip(&rb, 9U) == 9U, "skip wrapped block");
    /* durum: tail=0, avail=6: extra[1..6] buff[0..5]'te */

    TEST_CHECK(rbuff_get_read_block(&rb, &data, &len) == true, "post-wrap read block");
    TEST_CHECK(len == 6U, "post-wrap block len");
    TEST_CHECK(data == &backing_store[0], "post-wrap block at buff[0]");

    /* Kalani klasik API ile bosalt ve akisi dogrula */
    TEST_CHECK(rbuff_read_buff(&rb, out, 8U) == 6U, "drain rest via read_buff");
    TEST_CHECK(memcmp(out, &extra[1], 6U) == 0, "post-wrap content identical");
}

static void test_skip_advance_clamp(void)
{
    uint8_t src[4] = {0x11U, 0x22U, 0x33U, 0x44U};
    uint8_t *data = NULL;
    uint32_t len = 0U;

    reset_buffer();
    TEST_CHECK(rbuff_skip(&rb, 10U) == 0U, "skip empty = 0");

    /* advance bos bufferda free kadar ilerler (zero-copy protokolu:
     * blok al - yaz - advance; clamp davranisi burada olculuyor) */
    TEST_CHECK(rbuff_advance(&rb, 10U) == 10U, "advance limited by free");
    TEST_CHECK(rbuff_available(&rb) == 10U, "advance claims pending bytes");
    TEST_CHECK(rbuff_available_for_write(&rb) == 5U, "free shrinks after advance");

    /* skip eldekiyle sinirli */
    reset_buffer();
    (void)rbuff_write_buff(&rb, src, 4U);
    TEST_CHECK(rbuff_skip(&rb, 10U) == 4U, "skip clamped to available");
    TEST_CHECK(rbuff_available(&rb) == 0U, "buffer empty after clamped skip");

    /* advance free'ye clamp */
    reset_buffer();
    (void)rbuff_write_buff(&rb, src, 4U);
    TEST_CHECK(rbuff_advance(&rb, 20U) == 11U, "advance clamped to free");
    TEST_CHECK(rbuff_available(&rb) == 15U, "buffer full after max advance");

    /* write block wrap sinirinde kapanir: head=14, tail=3 -> free=4,
     * contiguous = min(4, 16-14) = 2 */
    reset_buffer();
    for (uint32_t i = 0U; i < 14U; i++)
    {
        (void)rbuff_write_byte(&rb, (uint8_t)i);
    }
    (void)rbuff_read_buff(&rb, src, 3U);
    TEST_CHECK(rbuff_get_write_block(&rb, &data, &len) == true, "block near end true");
    TEST_CHECK(len == 2U, "write block capped at buffer end");
    TEST_CHECK(data == &backing_store[14], "write block at buff[14]");
}

/* ------------------------------------------------------------------ */
/*  Deterministik karma stress                                         */
/* ------------------------------------------------------------------ */

static void test_mixed_stress(void)
{
    uint8_t  store[32];
    rbuff_t  stress;
    uint8_t  src[32];
    uint8_t  out[32];
    uint32_t stream_wr = 0U; /* yazilan bayt sayisi (mod 256 pattern) */
    uint32_t stream_rd = 0U; /* okunan bayt sayisi */
    uint32_t mismatch = 0U;

    (void)rbuff_init(&stress, store, 32U);

    for (uint32_t iter = 0U; (iter < 20000U) && (mismatch == 0U); iter++)
    {
        uint32_t r = lcg_next();
        uint32_t op = (r >> 16) & 3U;
        uint32_t amount = (r & 15U) + ((r >> 8) & 15U); /* 0..30 */

        if ((op == 0U) || (op == 1U))
        {
            uint32_t n = amount;
            for (uint32_t i = 0U; i < n; i++)
            {
                src[i] = (uint8_t)(stream_wr + i);
            }
            if (rbuff_write_buff(&stress, src, n) == 0U)
            {
                stream_wr += n;
            }
        }
        else
        {
            uint32_t got = rbuff_read_buff(&stress, out, amount);
            for (uint32_t i = 0U; i < got; i++)
            {
                if (out[i] != (uint8_t)(stream_rd + i))
                {
                    mismatch = 1U;
                    break;
                }
            }
            stream_rd += got;
        }

        /* Tek thread SPSC: available + free == kapasite-1 ve akis sayilari
         * bufferdaki veri ile birebir uyusmali */
        uint32_t a = rbuff_available(&stress);
        uint32_t f = rbuff_available_for_write(&stress);
        if ((a + f) != 31U)
        {
            mismatch = 2U;
        }
        if ((stream_wr - stream_rd) != a)
        {
            mismatch = 3U;
        }
    }

    TEST_CHECK(mismatch == 0U, "stress: no mismatch in 20000 iterations");
    if (mismatch != 0U)
    {
        printf("stress mismatch kind: %u\r\n", (unsigned)mismatch);
    }
    else
    {
        printf("stress: %u bytes written, %u read, %u pending\r\n",
               (unsigned)stream_wr, (unsigned)stream_rd,
               (unsigned)rbuff_available(&stress));
    }
}

/* ------------------------------------------------------------------ */

int main(void)
{
    test_init_rejects();
    test_guards_on_zeroed_instance();
    test_fifo_byte();
    test_write_buff_all_or_nothing();
    test_wrap_split_copies();
    test_peek_variants();
    test_linear_blocks();
    test_skip_advance_clamp();
    test_mixed_stress();

    printf("\r\n=== ring_buff tests: %u passed, %u failed ===\r\n",
           test_pass, test_fail);
    return (test_fail == 0U) ? 0 : 1;
}
