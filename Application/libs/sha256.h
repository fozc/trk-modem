/*
 * sha256.h
 *
 *  Created on: Oct 03, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SHA-256/HMAC adapted from sibling bootloader libsha256.
 */
#ifndef SHA256_H
#define SHA256_H

/*
 *    1) (one-shot):
 *   uint8_t hash[32];
 *   sha256((const uint8_t *)"abc", 3, hash);
 *
 *   2) incremental — UART/DMA etc
 *   sha256_ctx ctx;
 *   sha256_init(&ctx);
 *   sha256_update(&ctx, buf1, len1);
 *   sha256_update(&ctx, buf2, len2);
 *   sha256_final(&ctx, hash);
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint32_t h[8U];
    uint8_t  block[64U];
    uint8_t  block_len;
    uint64_t total_len;
} sha256_ctx;

/* Incremental API  */
void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t hash[32U]);

/* One-shot API  */
void sha256(const uint8_t *data, size_t len, uint8_t hash[32U]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
/*** end of file ***/
