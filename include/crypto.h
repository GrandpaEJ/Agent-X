#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>
#include <stdint.h>

// SHA-1
void sha1(const void *data, size_t len, uint8_t *out);

// RSA signing for ADB auth
typedef struct {
    uint32_t m[64]; // modulus n
    uint32_t e[64]; // public exponent
    uint32_t d[64]; // private exponent
    int bits;
} rsa_key;

int rsa_load_key(const char *path, rsa_key *key);
int rsa_sign(const rsa_key *key, const uint8_t *hash, uint8_t *signature);

// Base64
char *base64_encode(const uint8_t *data, size_t input_length, size_t *output_length);

// Signer
struct zip_archive;
char* generate_manifest(struct zip_archive *za, size_t *out_len);
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len);
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key, size_t *out_len);
int apk_sign_v1(const char *in_apk, const char *out_apk, rsa_key *key);

#endif
