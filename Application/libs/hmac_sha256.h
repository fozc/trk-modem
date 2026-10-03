/*
 * hmac_sha256.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SHA-256/HMAC adapted from sibling bootloader libsha256.
 */
#ifndef HMAC_SHA256_H
#define HMAC_SHA256_H

#include "sha256.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    sha256_ctx inner_ctx;
    uint8_t    o_key_pad[64U];
} hmac_sha256_ctx;

/* Incremental API */
void hmac_sha256_init(hmac_sha256_ctx *ctx, const uint8_t *key, size_t key_len);
void hmac_sha256_update(hmac_sha256_ctx *ctx, const uint8_t *data, size_t len);
void hmac_sha256_final(hmac_sha256_ctx *ctx, uint8_t mac[32U]);

/* One-shot API */
void hmac_sha256(uint8_t mac[32U],
                 const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* HMAC_SHA256_H */
/*** end of file ***/
