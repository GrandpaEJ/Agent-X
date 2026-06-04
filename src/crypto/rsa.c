// Minimal RSA signing for ADB auth.
// Parses PKCS#8 DER private key, performs RSA signature (PKCS#1 v1.5).
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define MAX_LIMBS 64 // 2048 bits = 64 × 32-bit limbs
#define KEY_BYTES 256

typedef struct {
    uint32_t m[MAX_LIMBS]; // modulus n
    uint32_t e[MAX_LIMBS]; // public exponent
    uint32_t d[MAX_LIMBS]; // private exponent
    int bits;
} rsa_key;

// Mult and reduce in one pass (schoolbook + conditional subtract)
static void mul_mod(uint32_t *r, const uint32_t *a, const uint32_t *b,
                    const uint32_t *m, int limbs) {
    uint64_t tmp[MAX_LIMBS * 2] = {0};
    for (int i = 0; i < limbs; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < limbs; j++) {
            tmp[i + j] += (uint64_t)a[i] * b[j] + carry;
            carry = tmp[i + j] >> 32;
            tmp[i + j] &= 0xFFFFFFFF;
        }
        tmp[i + limbs] = carry;
    }
    // Barrett reduction
    for (int i = limbs * 2 - 1; i >= limbs; i--) {
        if (tmp[i] == 0) continue;
        uint64_t q = tmp[i];
        uint64_t borrow = 0;
        for (int j = 0; j < limbs; j++) {
            uint64_t sub = (uint64_t)q * m[j] + borrow;
            if (tmp[i - limbs + j] < (sub & 0xFFFFFFFF)) borrow = 1;
            else borrow = 0;
            tmp[i - limbs + j] -= (uint32_t)sub;
            borrow += sub >> 32;
        }
        tmp[i] -= borrow;
    }
    memcpy(r, tmp, limbs * 4);
}

static void mod_exp(uint32_t *result, const uint32_t *base,
                    const uint32_t *exp, const uint32_t *mod, int limbs) {
    uint32_t r[MAX_LIMBS] = {0}; r[0] = 1;
    uint32_t b[MAX_LIMBS];
    memcpy(b, base, limbs * 4);
    int bits = limbs * 32;
    for (int i = 0; i < bits; i++) {
        if (exp[i >> 5] & (1U << (i & 31))) {
            uint32_t tmp[MAX_LIMBS];
            memcpy(tmp, r, limbs * 4);
            mul_mod(r, tmp, b, mod, limbs);
        }
        uint32_t tmp[MAX_LIMBS];
        memcpy(tmp, b, limbs * 4);
        mul_mod(b, tmp, tmp, mod, limbs);
    }
    memcpy(result, r, limbs * 4);
}

// Parse PKCS#8 DER private key, extract n and d
static int parse_pkcs8_der(const uint8_t *der, size_t len, rsa_key *key) {
    // Skip to the RSA private key inside PKCS#8 wrapper
    // PKCS#8: SEQUENCE { INTEGER(0), SEQUENCE { OID(rsaEncryption), NULL }, OCTET_STRING { RSAPrivateKey } }
    // RSAPrivateKey: SEQUENCE { version, n, e, d, p, q, dp, dq, qinv }
    if (len < 20 || der[0] != 0x30) return -1;
    // Find the OCTET_STRING containing RSAPrivateKey
    size_t off = 4;
    while (off < len) {
        if (der[off] == 0x04) { // OCTET_STRING
            size_t slen = der[off + 1];
            off += 2;
            if (slen > 128) { slen = (slen & 0x7F) << 8 | der[off]; off++; }
            if (off + slen <= len) {
                const uint8_t *rsa = der + off;
                size_t rslen = slen;
                // RSAPrivateKey: SEQUENCE { INTEGER(0), INTEGER(n), INTEGER(e), INTEGER(d), ... }
                if (rsa[0] != 0x30) return -1;
                int idx = 2;
                if (rsa[idx] & 0x80) idx += (rsa[idx] & 0x7F) + 1;
                idx++; // skip version
                // Read n
                if (idx >= (int)rslen || rsa[idx] != 0x02) return -1;
                int nlen = rsa[idx + 1]; idx += 2;
                if (nlen > KEY_BYTES + 1) return -1;
                memset(key->m, 0, sizeof(key->m));
                if (rsa[idx] == 0) { idx++; nlen--; } // strip leading 0
                for (int i = 0; i < nlen; i++) {
                    key->m[(nlen - 1 - i) / 4] |= (uint32_t)rsa[idx + i] << ((i % 4) * 8);
                }
                key->bits = nlen * 8;
                idx += nlen;
                // Skip e
                if (idx >= (int)rslen || rsa[idx] != 0x02) return -1;
                int elen = rsa[idx + 1]; idx += 2 + elen;
                // Read d
                if (idx >= (int)rslen || rsa[idx] != 0x02) return -1;
                int dlen = rsa[idx + 1]; idx += 2;
                if (dlen > KEY_BYTES + 1) return -1;
                memset(key->d, 0, sizeof(key->d));
                if (rsa[idx] == 0) { idx++; dlen--; }
                for (int i = 0; i < dlen; i++) {
                    key->d[(dlen - 1 - i) / 4] |= (uint32_t)rsa[idx + i] << ((i % 4) * 8);
                }
                return 0;
            }
        }
        off++; // minimal skip, good enough for typical PKCS8
        if (off > len) break;
    }
    return -1;
}

int rsa_load_key(const char *path, rsa_key *key) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return -1; }
    fread(buf, 1, sz, fp);
    fclose(fp);

    // Find base64 data between PEM headers
    const char *start = (const char *)buf;
    const char *b64 = strstr(start, "\n");
    if (!b64) { free(buf); return -1; }
    b64++;
    const char *end = strstr(b64, "-----");
    if (!end) { free(buf); return -1; }
    size_t b64len = end - b64;

    // Decode base64 to DER
    static const char b64t[256] = {
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
        ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
        ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
        ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
        ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
    };
    size_t der_len = b64len / 4 * 3;
    uint8_t *der = malloc(der_len + 4);
    int di = 0, val = 0, bits = -1;
    for (size_t i = 0; i < b64len; i++) {
        char c = b64[i];
        if (c == '=') { val <<= 6; bits += 6; break; }
        int v = b64t[(unsigned char)c];
        if (v == 0 && c != 'A') continue; // skip whitespace/newline
        if (bits == -1) { val = v; bits = 0; }
        else { val = (val << 6) | v; bits += 6; }
        if (bits == 12) { der[di++] = val >> 4; val &= 0xF; bits = 4; }
        if (bits == 18) { der[di++] = val >> 10; der[di++] = (val >> 2) & 0xFF; val &= 3; bits = 2; }
        if (bits == 24) { der[di++] = val >> 16; der[di++] = (val >> 8) & 0xFF; der[di++] = val & 0xFF; bits = -1; }
    }
    if (bits >= 0 && di < (int)der_len) der[di++] = val >> (bits - 2);

    int ret = parse_pkcs8_der(der, di, key);
    free(der);
    free(buf);
    return ret;
}

// PKCS#1 v1.5 signature: DER(SHA-1 digest)
static const uint8_t pkcs1_sha1_prefix[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a,
    0x05, 0x00, 0x04, 0x14
};

int rsa_sign(const rsa_key *key, const uint8_t *hash, uint8_t *signature) {
    int k = key->bits / 8;
    uint8_t *em = malloc(k);
    memset(em, 0, k);
    em[0] = 0x00; em[1] = 0x01;
    int ps_len = k - 3 - (int)sizeof(pkcs1_sha1_prefix) - 20;
    memset(em + 2, 0xFF, ps_len);
    em[2 + ps_len] = 0x00;
    memcpy(em + 2 + ps_len + 1, pkcs1_sha1_prefix, sizeof(pkcs1_sha1_prefix));
    memcpy(em + 2 + ps_len + 1 + sizeof(pkcs1_sha1_prefix), hash, 20);

    int limbs = (k + 3) / 4;
    uint32_t base[MAX_LIMBS], result[MAX_LIMBS];
    memset(base, 0, sizeof(base));
    for (int i = 0; i < k; i++)
        base[(k - 1 - i) / 4] |= (uint32_t)em[i] << ((i % 4) * 8);

    mod_exp(result, base, key->d, key->m, limbs);
    memset(signature, 0, k);
    for (int i = 0; i < k; i++)
        signature[i] = (result[(k - 1 - i) / 4] >> ((i % 4) * 8)) & 0xFF;

    free(em);
    return 0;
}
