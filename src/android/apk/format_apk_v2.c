#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crypto.h"
#include "formats.h"
#include "cert_der.h"
#include "pubkey_der.h"

static void put_u32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }
static void put_u64(uint8_t **p, uint64_t v) { memcpy(*p, &v, 8); *p += 8; }
static void put_bytes(uint8_t **p, const uint8_t *data, uint32_t len) { memcpy(*p, data, len); *p += len; }
static void put_len_bytes(uint8_t **p, const uint8_t *data, uint32_t len) { put_u32(p, len); put_bytes(p, data, len); }

static void compute_chunked_hash(FILE *fp, size_t start, size_t end, sha256_ctx *main_ctx) {
    size_t len = end - start;
    if (len == 0) return;
    fseek(fp, start, SEEK_SET);
    
    size_t num_chunks = (len + 1048575) / 1048576;
    uint8_t *chunk_buf = malloc(1048576);
    
    for (size_t i = 0; i < num_chunks; i++) {
        size_t chunk_len = len - (i * 1048576);
        if (chunk_len > 1048576) chunk_len = 1048576;
        fread(chunk_buf, 1, chunk_len, fp);
        
        uint8_t prefix[5];
        prefix[0] = 0xa5;
        uint32_t clen = (uint32_t)chunk_len;
        memcpy(prefix + 1, &clen, 4);
        
        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, prefix, 5);
        sha256_update(&ctx, chunk_buf, chunk_len);
        
        uint8_t chunk_hash[32];
        sha256_final(&ctx, chunk_hash);
        sha256_update(main_ctx, chunk_hash, 32);
    }
    free(chunk_buf);
}

// Generate APK Signing Block v2 and/or v3
int apk_sign_v2_v3(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3) {
    if (!do_v2 && !do_v3) return -1;
    FILE *in = fopen(in_apk, "rb");
    if (!in) return -1;
    
    fseek(in, 0, SEEK_END);
    long file_size = ftell(in);
    
    // Find EOCD
    long eocd_offset = -1;
    uint8_t eocd[22];
    for (long i = file_size - 22; i >= 0 && i >= file_size - 65536; i--) {
        fseek(in, i, SEEK_SET);
        fread(eocd, 1, 22, in);
        if (eocd[0] == 0x50 && eocd[1] == 0x4b && eocd[2] == 0x05 && eocd[3] == 0x06) {
            eocd_offset = i;
            break;
        }
    }
    if (eocd_offset < 0) { fclose(in); return -1; }
    
    uint32_t cd_offset, cd_size;
    memcpy(&cd_size, eocd + 12, 4);
    memcpy(&cd_offset, eocd + 16, 4);
    
    // Calculate total number of 1MB chunks across the three sections
    size_t s1 = cd_offset;
    size_t s2 = cd_size;
    size_t s3 = file_size - eocd_offset;
    size_t num_chunks = ((s1 + 1048575) / 1048576) + ((s2 + 1048575) / 1048576) + ((s3 + 1048575) / 1048576);
    
    sha256_ctx main_ctx;
    sha256_init(&main_ctx);
    
    uint8_t top_prefix[5];
    top_prefix[0] = 0x5a;
    uint32_t nc = (uint32_t)num_chunks;
    memcpy(top_prefix + 1, &nc, 4);
    sha256_update(&main_ctx, top_prefix, 5);
    
    compute_chunked_hash(in, 0, cd_offset, &main_ctx);
    compute_chunked_hash(in, cd_offset, cd_offset + cd_size, &main_ctx);
    
    // EOCD section needs CD offset updated for hashing
    // Since we don't know the exact block size yet, wait! We DO know it.
    // The block size is fixed for a given certificate and key.
    
    uint32_t digests_len = 4 + 4 + 4 + 32; // 44
    uint32_t certs_len = 4 + cert_der_len; // 4 + cert
    uint32_t attr_len = 0; // 0
    
    uint32_t signed_data_v2_len = (4 + digests_len) + (4 + certs_len) + (4 + attr_len);
    uint32_t signed_data_v3_len = (4 + digests_len) + (4 + certs_len) + 4 + 4 + (4 + attr_len);
    
    uint32_t sig_algo_id = 0x0103;
    uint32_t sig_len = 256;
    uint32_t signatures_seq_len = 4 + 4 + 4 + sig_len; // 268
    
    uint32_t pubkey_item_len = 4 + pubkey_der_len;
    
    uint32_t signer_v2_len = (4 + signed_data_v2_len) + (4 + signatures_seq_len) + pubkey_item_len;
    uint32_t signers_seq_v2_len = 4 + signer_v2_len;
    uint64_t v2_id = 0x7109871a;
    uint64_t v2_pair_len = 8 + 4 + 4 + signers_seq_v2_len; // 8 bytes len + 4 bytes ID + 4 bytes seq_len + sequence payload
    
    uint32_t signer_v3_len = (4 + signed_data_v3_len) + 4 + 4 + (4 + signatures_seq_len) + pubkey_item_len;
    uint32_t signers_seq_v3_len = 4 + signer_v3_len;
    uint64_t v3_id = 0xf05368c0;
    uint64_t v3_pair_len = 8 + 4 + 4 + signers_seq_v3_len;
    
    uint64_t block_len = 8 + 8 + 16; // base size (size_field, size_field, magic)
    if (do_v2) block_len += v2_pair_len;
    if (do_v3) block_len += v3_pair_len;
    
    uint64_t block_size_field = block_len - 8;
    
    // Hash EOCD with ORIGINAL CD offset (as it was before the block)
    // The EOCD we read from 'in' already has the original CD offset.
    uint8_t eocd_prefix[5];
    eocd_prefix[0] = 0xa5;
    uint32_t elen = 22; // EOCD size (assuming no zip comment)
    memcpy(eocd_prefix + 1, &elen, 4);
    
    sha256_ctx eocd_ctx;
    sha256_init(&eocd_ctx);
    sha256_update(&eocd_ctx, eocd_prefix, 5);
    sha256_update(&eocd_ctx, eocd, 22); // Use ORIGINAL eocd for hashing!
    uint8_t eocd_chunk_hash[32];
    sha256_final(&eocd_ctx, eocd_chunk_hash);
    sha256_update(&main_ctx, eocd_chunk_hash, 32);
    
    uint8_t final_apk_digest[32];
    sha256_final(&main_ctx, final_apk_digest);
    
    // Generate signed data
    uint8_t *sd_v2 = NULL, *sd_v3 = NULL;
    uint8_t signature_v2[256], signature_v3[256];
    
    if (do_v2) {
        sd_v2 = malloc(signed_data_v2_len);
        uint8_t *p2 = sd_v2;
        put_u32(&p2, digests_len);
        put_u32(&p2, 4 + 4 + 32);
        put_u32(&p2, sig_algo_id);
        put_len_bytes(&p2, final_apk_digest, 32);
        
        put_u32(&p2, certs_len);
        put_len_bytes(&p2, cert_der, cert_der_len);
        
        put_u32(&p2, attr_len);
        
        uint8_t rsa_hash[32];
        sha256(sd_v2, signed_data_v2_len, rsa_hash);
        rsa_sign_sha256(key, rsa_hash, signature_v2);
    }
    
    if (do_v3) {
        sd_v3 = malloc(signed_data_v3_len);
        uint8_t *p3 = sd_v3;
        put_u32(&p3, digests_len);
        put_u32(&p3, 4 + 4 + 32);
        put_u32(&p3, sig_algo_id);
        put_len_bytes(&p3, final_apk_digest, 32);
        
        put_u32(&p3, certs_len);
        put_len_bytes(&p3, cert_der, cert_der_len);
        
        put_u32(&p3, 24); // minSdk (Android 7.0)
        put_u32(&p3, 0x7fffffff); // maxSdk
        
        put_u32(&p3, attr_len);
        
        uint8_t rsa_hash[32];
        sha256(sd_v3, signed_data_v3_len, rsa_hash);
        rsa_sign_sha256(key, rsa_hash, signature_v3);
    }
    
    // Generate the block
    uint8_t *blk = malloc(block_len);
    uint8_t *p = blk;
    put_u64(&p, block_size_field); // size of block excluding this field
    
    if (do_v2) {
        put_u64(&p, v2_pair_len - 8); // size of pair value (excluding size field)
        put_u32(&p, v2_id);
        put_u32(&p, signers_seq_v2_len);
        put_u32(&p, signer_v2_len);
        put_len_bytes(&p, sd_v2, signed_data_v2_len);
        put_u32(&p, signatures_seq_len);
        put_u32(&p, 4 + 4 + 256);
        put_u32(&p, sig_algo_id);
        put_len_bytes(&p, signature_v2, 256);
        put_len_bytes(&p, pubkey_der, pubkey_der_len);
    }
    
    if (do_v3) {
        put_u64(&p, v3_pair_len - 8);
        put_u32(&p, v3_id);
        put_u32(&p, signers_seq_v3_len);
        put_u32(&p, signer_v3_len);
        put_len_bytes(&p, sd_v3, signed_data_v3_len);
        
        put_u32(&p, 24); // minSdk outside signed_data
        put_u32(&p, 0x7fffffff); // maxSdk outside signed_data
        
        put_u32(&p, signatures_seq_len);
        put_u32(&p, 4 + 4 + 256);
        put_u32(&p, sig_algo_id);
        put_len_bytes(&p, signature_v3, 256);
        put_len_bytes(&p, pubkey_der, pubkey_der_len);
    }
    
    put_u64(&p, block_size_field);
    put_bytes(&p, (const uint8_t*)"APK Sig Block 42", 16);
    
    if (sd_v2) free(sd_v2);
    if (sd_v3) free(sd_v3);
    
    // Write out the new APK
    FILE *out = fopen(out_apk, "wb");
    fseek(in, 0, SEEK_SET);
    
    uint8_t *copy_buf = malloc(1048576);
    size_t rem = cd_offset;
    while (rem > 0) {
        size_t c = rem > 1048576 ? 1048576 : rem;
        fread(copy_buf, 1, c, in);
        fwrite(copy_buf, 1, c, out);
        rem -= c;
    }
    
    fwrite(blk, 1, block_len, out);
    free(blk);
    
    rem = cd_size;
    while (rem > 0) {
        size_t c = rem > 1048576 ? 1048576 : rem;
        fread(copy_buf, 1, c, in);
        fwrite(copy_buf, 1, c, out);
        rem -= c;
    }
    
    uint8_t eocd_mod[22];
    memcpy(eocd_mod, eocd, 22);
    uint32_t new_cd_offset = cd_offset + block_len;
    memcpy(eocd_mod + 16, &new_cd_offset, 4);
    
    fwrite(eocd_mod, 1, 22, out); // We ignore ZIP comments for now
    
    free(copy_buf);
    fclose(in);
    fclose(out);
    return 0;
}
