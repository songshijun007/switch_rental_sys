#pragma once
#include <stdint.h>
#include <stddef.h>

// NOTE: libnx's own <switch.h> indirectly includes a crypto/sha256.h that
// also defines SHA256_BLOCK_SIZE (as 0x40). To avoid the "redefined" warning
// and any potential clash, we use our own prefixed macro names here.
#ifndef RENTAL_SHA256_DIGEST_SIZE
#define RENTAL_SHA256_DIGEST_SIZE 32
#endif
#ifndef RENTAL_SHA256_BLOCK_SIZE
#define RENTAL_SHA256_BLOCK_SIZE  64
#endif

// Keep old names as aliases only if not already defined by another header
// (e.g. libnx's crypto/sha256.h), to preserve compatibility with existing
// call sites in this project without triggering a macro redefinition warning.
#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE RENTAL_SHA256_DIGEST_SIZE
#endif
#ifndef SHA256_BLOCK_SIZE
#define SHA256_BLOCK_SIZE RENTAL_SHA256_BLOCK_SIZE
#endif

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t out[RENTAL_SHA256_DIGEST_SIZE]);

// Convenience: single-shot SHA-256.
void sha256_hash(const void *data, size_t len, uint8_t out[RENTAL_SHA256_DIGEST_SIZE]);

// HMAC-SHA256.
void hmac_sha256(const void *key, size_t key_len,
                 const void *msg, size_t msg_len,
                 uint8_t out[RENTAL_SHA256_DIGEST_SIZE]);
