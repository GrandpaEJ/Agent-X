#include "crypto.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Phase 2: Generate MANIFEST.MF
char* generate_manifest(zip_archive *za, size_t *out_len) {
    int num_entries = zip_get_num_entries(za);
    size_t buf_cap = 256 + num_entries * 256;
    char *manifest = malloc(buf_cap);
    if (!manifest) return NULL;
    
    strcpy(manifest, "Manifest-Version: 1.0\r\nBuilt-By: Agent-X\r\n\r\n");
    size_t len = strlen(manifest);

    for (int i = 0; i < num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        // Skip directories and existing META-INF files
        if (name[strlen(name) - 1] == '/') continue;
        if (strncmp(name, "META-INF/", 9) == 0) continue;

        size_t size = 0;
        void *data = zip_extract_entry(za, i, &size);
        if (!data) continue;

        uint8_t hash[20];
        sha1(data, size, hash);
        free(data);

        size_t b64_len = 0;
        char *b64 = base64_encode(hash, 20, &b64_len);
        
        int written = snprintf(manifest + len, buf_cap - len, 
                               "Name: %s\r\nSHA1-Digest: %s\r\n\r\n", name, b64);
        if (written > 0) len += written;
        free(b64);
    }
    *out_len = len;
    return manifest;
}

// Phase 3: Generate CERT.SF
char* generate_signature_file(const char *manifest, size_t manifest_len, size_t *out_len) {
    size_t buf_cap = manifest_len + 512;
    char *sf = malloc(buf_cap);
    if (!sf) return NULL;

    // Hash the entire manifest
    uint8_t man_hash[20];
    sha1(manifest, manifest_len, man_hash);
    size_t b64_len = 0;
    char *man_b64 = base64_encode(man_hash, 20, &b64_len);

    strcpy(sf, "Signature-Version: 1.0\r\nCreated-By: Agent-X\r\n");
    size_t len = strlen(sf);
    
    len += snprintf(sf + len, buf_cap - len, "SHA1-Digest-Manifest: %s\r\n\r\n", man_b64);
    free(man_b64);

    // Parse the manifest line by line to hash individual sections
    // A section in MANIFEST.MF looks like:
    // Name: AndroidManifest.xml\r\nSHA1-Digest: ...\r\n\r\n
    // We need to hash exactly this string (including \r\n\r\n)
    
    const char *p = strstr(manifest, "Name: ");
    while (p) {
        const char *end = strstr(p, "\r\n\r\n");
        if (!end) break;
        end += 4; // Include the \r\n\r\n
        
        size_t section_len = end - p;
        uint8_t sec_hash[20];
        sha1(p, section_len, sec_hash);
        
        char *sec_b64 = base64_encode(sec_hash, 20, &b64_len);
        
        // Extract the name to write to SF
        const char *nl = strstr(p, "\r\n");
        size_t name_line_len = nl - p;
        char name_line[512] = {0};
        if (name_line_len < sizeof(name_line)) {
            strncpy(name_line, p, name_line_len);
        }
        
        len += snprintf(sf + len, buf_cap - len, "%s\r\nSHA1-Digest: %s\r\n\r\n", name_line, sec_b64);
        free(sec_b64);
        
        p = strstr(end, "Name: ");
    }

    *out_len = len;
    return sf;
}

#include "pkcs7_template.h" // contains pkcs7_template array (926 bytes)

// Phase 4: Generate CERT.RSA (PKCS#7 block)
char* generate_cert_rsa(const char *sf_data, size_t sf_len, rsa_key *key, size_t *out_len) {
    uint8_t sf_hash[20];
    sha1(sf_data, sf_len, sf_hash);

    uint8_t signature[256];
    if (rsa_sign(key, sf_hash, signature) != 0) {
        return NULL;
    }

    size_t total_len = sizeof(pkcs7_template) + 256;
    char *cert_rsa = malloc(total_len);
    if (!cert_rsa) return NULL;

    memcpy(cert_rsa, pkcs7_template, sizeof(pkcs7_template));
    memcpy(cert_rsa + sizeof(pkcs7_template), signature, 256);

    *out_len = total_len;
    return cert_rsa;
}
