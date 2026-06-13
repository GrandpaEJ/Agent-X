#ifndef FORMATS_H
#define FORMATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct zip_archive zip_archive;
typedef struct axml_ctx axml_ctx;
typedef struct dex_ctx dex_ctx;

// ZIP
zip_archive *zip_open(const char *path);
void zip_close(zip_archive *za);
int zip_get_num_entries(zip_archive *za);
const char *zip_get_entry_name(zip_archive *za, int index);
size_t zip_get_entry_size(zip_archive *za, int index);
int zip_entry_is_compressed(zip_archive *za, int index);
void *zip_extract_entry(zip_archive *za, int index, size_t *out_size);
int zip_extract_all(zip_archive *za, const char *out_dir);
typedef struct zip_writer zip_writer;
zip_writer *zip_writer_open(const char *path);
int zip_writer_add(zip_writer *zw, const char *name, const void *data, size_t size, int compress);
int zip_writer_close(zip_writer *zw);
int zipalign_file(const char *in_path, const char *out_path, int alignment);

// AXML
axml_ctx *axml_parse(const uint8_t *data, size_t size);
char *axml_get_xml(axml_ctx *ctx);
void axml_free(axml_ctx *ctx);
int axml_assemble(const char *src_xml, const char *out_axml);

// DEX (reader)
dex_ctx *dex_parse(const uint8_t *data, size_t size);
char *dex_dump(dex_ctx *ctx);
char *dex_to_smali_class(dex_ctx *ctx, uint32_t class_idx);
void dex_free(dex_ctx *ctx);

// Smali assembler
int smali_assemble(const char *src_dir, const char *out_dex);

// APK
#include "crypto.h"
int apk_sign_v1(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3);
int apk_sign_v2_v3(const char *in_apk, const char *out_apk, rsa_key *key, int do_v2, int do_v3, const uint8_t *custom_cert, uint32_t custom_cert_len);
char* generate_manifest(struct zip_archive *za, size_t *out_len);
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len, int do_v2, int do_v3);
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key, size_t *out_len);
char *apk_analyze(const char *path);
void apk_free(char *result);

// ADB
int adb_install(const char *apk_path);
int adb_uninstall(const char *package);
char *adb_shell(const char *command);
char *adb_devices(void);
int adb_push_file(const char *local_path, const char *remote_path);
int adb_pull_file(const char *remote_path, const char *local_path);

#endif