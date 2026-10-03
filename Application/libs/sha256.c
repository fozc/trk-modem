/*
 * sha256.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SHA-256/HMAC adapted from sibling bootloader libsha256.
 */
#include "sha256.h"
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  /* Big-endian: SHA-256 is natively big-endian, no swap needed */
  #define BSWAP32(x) (x)
#elif defined(__GNUC__) || defined(__clang__)
  #define BSWAP32(x) __builtin_bswap32(x)
#elif defined(_MSC_VER)
  #include <stdlib.h>
  #define BSWAP32(x) _byteswap_ulong(x)
#else
  static inline uint32_t BSWAP32(uint32_t x)
  {
      return ((x >> 24U) & 0x000000FFU) |
             ((x >>  8U) & 0x0000FF00U) |
             ((x <<  8U) & 0x00FF0000U) |
             ((x << 24U) & 0xFF000000U);
  }
#endif

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32U - (n))))

static const uint32_t round_constants[64U] =
{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};


static void sha256_process_block(uint32_t h[8U], const uint8_t block[64U])
{
    uint32_t w[16U];
    uint32_t a = h[0U], b = h[1U], c = h[2U], d = h[3U],
             e = h[4U], f = h[5U], g = h[6U], hh = h[7U];

    for (size_t i = 0U; 64U > i; i++)
    {
        uint32_t wi;
        if (16U > i)
        {
            /* Big-endian load: native word load followed by byte swap */
            uint32_t tmp;
            memcpy(&tmp, &block[i * 4U], 4U);
            wi = BSWAP32(tmp);
            w[i] = wi;
        }
        else
        {
            uint32_t w15 = w[(i - 15U) & 0xFU];
            uint32_t w2  = w[(i -  2U) & 0xFU];
            uint32_t s0  = ROR32(w15, 7U)  ^ ROR32(w15, 18U) ^ (w15 >> 3U);
            uint32_t s1  = ROR32(w2,  17U) ^ ROR32(w2,  19U) ^ (w2  >> 10U);
            wi = w[(i - 16U) & 0xFU] + s0 + w[(i - 7U) & 0xFU] + s1;
            w[i & 0xFU] = wi;
        }

        uint32_t sum1    = ROR32(e, 6U)  ^ ROR32(e, 11U) ^ ROR32(e, 25U);
        uint32_t ch    = g ^ (e & (f ^ g));
        uint32_t temp1 = hh + sum1 + ch + round_constants[i] + wi;
        uint32_t sum0    = ROR32(a, 2U)  ^ ROR32(a, 13U) ^ ROR32(a, 22U);
        uint32_t maj   = (a & b) ^ (c & (a ^ b));
        uint32_t temp2 = sum0 + maj;

        hh = g; g = f; f = e; e = d + temp1;
        d  = c; c = b; b = a; a = temp1 + temp2;
    }

    h[0U] += a; h[1U] += b; h[2U] += c; h[3U] += d;
    h[4U] += e; h[5U] += f; h[6U] += g; h[7U] += hh;
}


static void sha256_write_be32(uint8_t *dst, const uint32_t *src, size_t count)
{
    for (size_t i = 0U; count > i; i++)
    {
        uint32_t tmp = BSWAP32(src[i]);
        memcpy(&dst[i * 4U], &tmp, 4U);
    }
}


void sha256_init(sha256_ctx *ctx)
{
    ctx->h[0U] = 0x6a09e667U; ctx->h[1U] = 0xbb67ae85U;
    ctx->h[2U] = 0x3c6ef372U; ctx->h[3U] = 0xa54ff53aU;
    ctx->h[4U] = 0x510e527fU; ctx->h[5U] = 0x9b05688cU;
    ctx->h[6U] = 0x1f83d9abU; ctx->h[7U] = 0x5be0cd19U;
    ctx->block_len = 0U;
    ctx->total_len = 0U;
}

void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->total_len += len;

    if (0U < ctx->block_len)
    {
        size_t need = 64U - ctx->block_len;
        if (len < need)
        {
            memcpy(ctx->block + ctx->block_len, data, len);
            ctx->block_len = (uint8_t)(ctx->block_len + len);
            return;
        }
        memcpy(ctx->block + ctx->block_len, data, need);
        sha256_process_block(ctx->h, ctx->block);
        data += need;
        len  -= need;
        ctx->block_len = 0U;
    }

    while (64U <= len)
    {
        sha256_process_block(ctx->h, data);
        data += 64U;
        len  -= 64U;
    }

    if (0U < len)
    {
        memcpy(ctx->block, data, len);
        ctx->block_len = (uint8_t)len;
    }
}

void sha256_final(sha256_ctx *ctx, uint8_t hash[32U])
{
    uint8_t *block = ctx->block;
    size_t remaining = ctx->block_len;

    block[remaining++] = 0x80U;
    memset(block + remaining, 0U, 64U - remaining);

    if (56U < remaining)
    {
        sha256_process_block(ctx->h, block);
        memset(block, 0U, 64U);
    }

    uint64_t bit_len = ctx->total_len * 8U;
    block[56U] = (uint8_t)(bit_len >> 56U);
    block[57U] = (uint8_t)(bit_len >> 48U);
    block[58U] = (uint8_t)(bit_len >> 40U);
    block[59U] = (uint8_t)(bit_len >> 32U);
    block[60U] = (uint8_t)(bit_len >> 24U);
    block[61U] = (uint8_t)(bit_len >> 16U);
    block[62U] = (uint8_t)(bit_len >>  8U);
    block[63U] = (uint8_t)(bit_len);

    sha256_process_block(ctx->h, block);

    /* (big-endian) */
    sha256_write_be32(hash, ctx->h, 8U);

    memset(ctx, 0U, sizeof(*ctx));
}

void sha256(const uint8_t *data, size_t len, uint8_t hash[32U])
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}
/*** end of file ***/
