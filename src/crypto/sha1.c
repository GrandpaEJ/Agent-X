// SHA-1 implementation (FIPS 180-4)
#include <string.h>
#include <stdint.h>

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buf[64];
} sha1_ctx;

static void sha1_transform(sha1_ctx *ctx, const uint8_t *block) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROTL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
    uint32_t d = ctx->state[3], e = ctx->state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;          k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;          k = 0xCA62C1D6; }
        uint32_t tmp = ROTL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROTL(b, 30); b = a; a = tmp;
    }
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d; ctx->state[4] += e;
}

void sha1_init(sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void sha1_update(sha1_ctx *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = ctx->count & 63;
    ctx->count += len;
    size_t part = 64 - idx;
    if (len >= part) {
        memcpy(ctx->buf + idx, p, part);
        sha1_transform(ctx, ctx->buf);
        for (p += part, len -= part; len >= 64; p += 64, len -= 64)
            sha1_transform(ctx, p);
        idx = 0;
    }
    memcpy(ctx->buf + idx, p, len);
}

void sha1_final(sha1_ctx *ctx, uint8_t *out) {
    uint64_t bits = ctx->count << 3;
    size_t idx = ctx->count & 63;
    ctx->buf[idx++] = 0x80;
    if (idx > 56) {
        memset(ctx->buf + idx, 0, 64 - idx);
        sha1_transform(ctx, ctx->buf);
        memset(ctx->buf, 0, 56);
    } else {
        memset(ctx->buf + idx, 0, 56 - idx);
    }
    for (int i = 0; i < 8; i++)
        ctx->buf[56 + i] = (bits >> (56 - i*8)) & 0xFF;
    sha1_transform(ctx, ctx->buf);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = ctx->state[i] >> 24;
        out[i*4+1] = ctx->state[i] >> 16;
        out[i*4+2] = ctx->state[i] >> 8;
        out[i*4+3] = ctx->state[i];
    }
}

void sha1(const void *data, size_t len, uint8_t *out) {
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}
