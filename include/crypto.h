#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

// SHA-1
void sha1(const void *data, size_t len, uint8_t *out);

// SHA-256
typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t *hash);
void sha256(const void *data, size_t len, uint8_t *out);

// RSA signing for ADB auth
typedef struct {
    uint32_t m[64]; // modulus n
    uint32_t e[64]; // public exponent
    uint32_t d[64]; // private exponent
    int bits;
} rsa_key;

int rsa_load_key(const char *path, rsa_key *key);
int rsa_sign(const rsa_key *key, const uint8_t *hash, uint8_t *signature);
int rsa_sign_sha256(const rsa_key *key, const uint8_t *hash, uint8_t *signature);

// Base64
char *base64_encode(const uint8_t *data, size_t input_length, size_t *output_length);

// End of crypto

#endif
