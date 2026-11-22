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

static void clear_bytes(void *buffer, size_t len)
{
    volatile uint8_t *bytes = buffer;

    for (size_t index = 0U; index < len; index++)
    {
        bytes[index] = 0U;
    }
}

void hmac_sha256_init(hmac_sha256_ctx *ctx, const uint8_t *key, size_t key_len)
{
    uint8_t key_hash[32U];
    uint8_t inner_pad[64U];

    if (key_len > sizeof(inner_pad))
    {
        sha256(key, key_len, key_hash);
        key = key_hash;
        key_len = sizeof(key_hash);
    }

    memset(inner_pad, 0x36, sizeof(inner_pad));
    memset(ctx->o_key_pad, 0x5C, sizeof(ctx->o_key_pad));
    for (size_t index = 0U; index < key_len; index++)
    {
        inner_pad[index] ^= key[index];
        ctx->o_key_pad[index] ^= key[index];
    }

    sha256_init(&ctx->inner_ctx);
    sha256_update(&ctx->inner_ctx, inner_pad, sizeof(inner_pad));
    clear_bytes(inner_pad, sizeof(inner_pad));
    clear_bytes(key_hash, sizeof(key_hash));
}

void hmac_sha256_update(hmac_sha256_ctx *ctx, const uint8_t *data, size_t data_len)
{
    if (data_len != 0U)
    {
        sha256_update(&ctx->inner_ctx, data, data_len);
    }
}

void hmac_sha256_final(hmac_sha256_ctx *ctx, uint8_t mac[32U])
{
    sha256_ctx outer;
    uint8_t inner_hash[32U];

    sha256_final(&ctx->inner_ctx, inner_hash);
    sha256_init(&outer);
    sha256_update(&outer, ctx->o_key_pad, sizeof(ctx->o_key_pad));
    sha256_update(&outer, inner_hash, sizeof(inner_hash));
    sha256_final(&outer, mac);

    clear_bytes(inner_hash, sizeof(inner_hash));
    clear_bytes(&outer, sizeof(outer));
    clear_bytes(ctx, sizeof(*ctx));
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

bool pbkdf2_hmac_sha256(uint8_t output[32U], const uint8_t *password,
                       size_t password_len, const uint8_t salt[PBKDF2_SHA256_SALT_SIZE],
                       uint32_t iterations)
{
    static const uint8_t block_number[4U] = {0U, 0U, 0U, 1U};
    hmac_sha256_ctx base = {0};
    hmac_sha256_ctx round;
    uint8_t digest[32U];

    if (output == NULL || salt == NULL || iterations == 0U ||
        (password == NULL && password_len != 0U))
    {
        return false;
    }

    hmac_sha256_init(&base, password, password_len);
    round = base;
    hmac_sha256_update(&round, salt, PBKDF2_SHA256_SALT_SIZE);
    hmac_sha256_update(&round, block_number, sizeof(block_number));
    hmac_sha256_final(&round, digest);
    memcpy(output, digest, sizeof(digest));

    for (uint32_t iteration = 1U; iteration < iterations; iteration++)
    {
        round = base;
        hmac_sha256_update(&round, digest, sizeof(digest));
        hmac_sha256_final(&round, digest);
        for (size_t index = 0U; index < sizeof(digest); index++)
        {
            output[index] ^= digest[index];
        }
    }

    clear_bytes(digest, sizeof(digest));
    clear_bytes(&base, sizeof(base));
    return true;
}

/*** end of file ***/
