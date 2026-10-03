/*
 * hmac_sha256.c
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SHA-256/HMAC adapted from sibling bootloader libsha256.
 */
#include "hmac_sha256.h"
#include <string.h>

static void prepare_key(const uint8_t *key, size_t key_len,
                        uint8_t padded_key[64U])
{
    uint8_t key_hash[32U];
    if (64U < key_len)
    {
        sha256(key, key_len, key_hash);
        key = key_hash;
        key_len = 32U;
    }
    memset(padded_key, 0U, 64U);
    memcpy(padded_key, key, key_len);
}

void hmac_sha256_init(hmac_sha256_ctx *ctx, const uint8_t *key, size_t key_len)
{
    uint8_t padded_key[64U];
    uint8_t i_key_pad[64U];

    prepare_key(key, key_len, padded_key);

    for (size_t i = 0U; 64U > i; i++)
    {
        i_key_pad[i]       = padded_key[i] ^ 0x36U;
        ctx->o_key_pad[i]  = padded_key[i] ^ 0x5cU;
    }

    sha256_init(&ctx->inner_ctx);
    sha256_update(&ctx->inner_ctx, i_key_pad, 64U);
}

void hmac_sha256_update(hmac_sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    sha256_update(&ctx->inner_ctx, data, len);
}

void hmac_sha256_final(hmac_sha256_ctx *ctx, uint8_t mac[32U])
{
    uint8_t inner_hash[32U];
    sha256_final(&ctx->inner_ctx, inner_hash);

    sha256_ctx outer;
    sha256_init(&outer);
    sha256_update(&outer, ctx->o_key_pad, 64U);
    sha256_update(&outer, inner_hash, 32U);
    sha256_final(&outer, mac);
}

void hmac_sha256(uint8_t mac[32U],
                 const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len)
{
    hmac_sha256_ctx ctx;
    hmac_sha256_init(&ctx, key, key_len);
    hmac_sha256_update(&ctx, data, data_len);
    hmac_sha256_final(&ctx, mac);
}
/*** end of file ***/
