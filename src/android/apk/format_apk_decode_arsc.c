#include "formats.h"
#include "../arsc/format_arsc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static int mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    return mkdir(tmp, 0755);
}

static int apk_dec_axml(arsc_ctx *arsc, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 8) { fclose(f); return -1; }
    uint8_t *buf = malloc(sz);
    if (!buf || fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);
    
    // Check AXML magic
    uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                     ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (magic != 0x00080003) { free(buf); return -1; }
    
    axml_ctx *ctx = axml_parse(buf, sz);
    if (!ctx) { free(buf); return -1; }
    axml_set_arsc(ctx, arsc);
    char *xml = axml_get_xml(ctx);
    if (!xml) { axml_free(ctx); free(buf); return -1; }
    
    FILE *out = fopen(path, "w");
    if (out) { fputs(xml, out); fclose(out); }
    
    axml_free(ctx);
    free(buf);
    return out ? 0 : -1;
}

static void dec_dir(arsc_ctx *arsc, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[4096]; snprintf(p, sizeof(p), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
            dec_dir(arsc, p);
        else if (strstr(ent->d_name, ".xml"))
            apk_dec_axml(arsc, p);
    }
    closedir(d);
}

static void esc_xml(FILE *f, const char *s) {
    while (*s) {
        if (*s == '&') fputs("&amp;", f);
        else if (*s == '<') fputs("&lt;", f);
        else if (*s == '>') fputs("&gt;", f);
        else if (*s == '"') fputs("&quot;", f);
        else if (*s == '\'') fputs("&apos;", f);
        else fputc(*s, f);
        s++;
    }
}

static void gen_values_xml(arsc_ctx *ctx, FILE *f, const char *type_name, uint8_t type_id) {
    fprintf(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n");
    for (int p = 0; p < ctx->package_count; p++) {
        if (ctx->packages[p].name[0] == '\0') continue;
        // Find the first type chunk for this type ID (default config, has all entries)
        uint32_t poff = ctx->packages[p].pkg_off + arsc_r16(ctx->data + ctx->packages[p].pkg_off + 2);
        uint32_t pkg_end = ctx->packages[p].pkg_off + arsc_r32(ctx->data + ctx->packages[p].pkg_off + 4);
        const uint32_t *first_offsets = NULL;
        const uint8_t *first_entries = NULL;
        uint32_t first_ec = 0;
        while (poff + 8 <= pkg_end) {
            uint16_t pt = arsc_r16(ctx->data + poff);
            uint32_t pcs = arsc_r32(ctx->data + poff + 4);
            if (pcs < 8 || poff + pcs > pkg_end) break;
            if (pt == RES_TABLE_TYPE_TYPE && ctx->data[poff + 8] == type_id) {
                if (!first_offsets) { // first match only
                    first_offsets = (const uint32_t*)(ctx->data + poff + arsc_r16(ctx->data + poff + 2));
                    first_entries = ctx->data + poff + arsc_r32(ctx->data + poff + 16);
                    first_ec = arsc_r32(ctx->data + poff + 12);
                }
            }
            poff += pcs;
        }
        if (!first_offsets || !first_entries) continue;
        for (uint32_t i = 0; i < first_ec; i++) {
            if (first_offsets[i] == 0xFFFFFFFF) continue;
            const uint8_t *entry = first_entries + first_offsets[i];
            uint16_t eflags = arsc_r16(entry + 2);
            uint32_t key_idx = arsc_r32(entry + 4);
            const char *kname = arsc_sp_string(ctx->packages[p].key_pool, key_idx);
            if ((eflags & 0x0001) || !kname) continue;
            const uint8_t *val = entry + arsc_r16(entry);
            uint8_t dt = val[3];
            uint32_t dv = arsc_r32(val + 4);
            fprintf(f, "  <%s name=\"%s\">", type_name, kname);
            if (dt == 0x03) {
                const char *s = arsc_get_string(ctx, dv);
                esc_xml(f, s ? s : "");
            } else if (dt == 0x01) {
                const char *r = arsc_lookup_id(ctx, dv);
                if (r) fprintf(f, "@%s", r);
                else fprintf(f, "@ref/0x%08x", dv);
            } else if (dt == 0x10 || dt == 0x11) {
                fprintf(f, "%d", (int32_t)dv);
            } else if (dt == 0x12) {
                fputs(dv ? "true" : "false", f);
            } else if (dt >= 0x13 && dt <= 0x16) {
                fprintf(f, "#%08x", dv);
            }
            fprintf(f, "</%s>\n", type_name);
        }
    }
    fprintf(f, "</resources>\n");
}

// Helper: re-encode decoded XML back to binary AXML with ARSC context
static int enc_xml_file(arsc_ctx *arsc, const char *xml_path) {
    char out_path[4096];
    snprintf(out_path, sizeof(out_path), "%s.axml", xml_path);
    axml_assemble_set_arsc(arsc);
    int ret = axml_assemble(xml_path, out_path);
    axml_assemble_set_arsc(NULL);
    if (ret == 0) rename(out_path, xml_path);
    return ret;
}

// Walk directory and re-encode all XML files
static void enc_xml_dir(arsc_ctx *arsc, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[4096]; snprintf(p, sizeof(p), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0) {
            if (S_ISDIR(st.st_mode)) enc_xml_dir(arsc, p);
            else if (strstr(ent->d_name, ".xml")) enc_xml_file(arsc, p);
        }
    }
    closedir(d);
}

int arsc_decode_apk(const char *out_dir) {
    char arsc_path[4096];
    snprintf(arsc_path, sizeof(arsc_path), "%s/resources.arsc", out_dir);
    
    FILE *af = fopen(arsc_path, "rb");
    if (!af) {
        // No resources.arsc, just decode AXML files
        dec_dir(NULL, out_dir);
        return 0;
    }
    fseek(af, 0, SEEK_END); long asz = ftell(af); fseek(af, 0, SEEK_SET);
    uint8_t *adata = malloc(asz);
    if (!adata || fread(adata, 1, asz, af) != (size_t)asz) { free(adata); fclose(af); return -1; }
    fclose(af);
    
    arsc_ctx *arsc = arsc_parse(adata, asz);
    if (!arsc) { free(adata); return -1; }
    
    // 1. Decode all AXML files with ARSC context
    dec_dir(arsc, out_dir);
    
    // 2. Generate res/values/ XML files from ARSC
    char values_dir[4096];
    snprintf(values_dir, sizeof(values_dir), "%s/res/values", out_dir);
    mkdir_p(values_dir);
    
    // Generate strings.xml for type 0x03 (string)
    char spath[4096];
    snprintf(spath, sizeof(spath), "%s/strings.xml", values_dir);
    FILE *sf = fopen(spath, "w");
    if (sf) { gen_values_xml(arsc, sf, "string", 3); fclose(sf); }
    
    // Check for type IDs in the ARSC and generate appropriate files
    for (int p = 0; p < arsc->package_count; p++) {
        for (int t = 0; t < arsc->packages[p].type_count; t++) {
            uint8_t tid = arsc->packages[p].types[t].id;
            const char *tn = arsc->packages[p].types[t].name;
            if (!tn) continue;
            // Map type IDs to common names
            char fname[64];
            if (strcmp(tn, "string") == 0) continue; // already done
            snprintf(fname, sizeof(fname), "%s.xml", tn);
            // Check if file already exists
            char fpath[4096];
            snprintf(fpath, sizeof(fpath), "%s/%s", values_dir, fname);
            FILE *tf = fopen(fpath, "r");
            if (tf) { fclose(tf); continue; } // skip existing
            tf = fopen(fpath, "w");
            if (tf) { gen_values_xml(arsc, tf, tn, tid); fclose(tf); }
        }
    }
    
    // 3. Generate public.xml with resource ID mapping
    char ppath[4096];
    snprintf(ppath, sizeof(ppath), "%s/public.xml", values_dir);
    FILE *pf = fopen(ppath, "w");
    if (pf) {
        fprintf(pf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n");
        for (int p = 0; p < arsc->package_count; p++) {
            // Collect last type chunk per type ID
            uint32_t poff = arsc->packages[p].pkg_off + arsc_r16(arsc->data + arsc->packages[p].pkg_off + 2);
            uint32_t pkg_end = arsc->packages[p].pkg_off + arsc_r32(arsc->data + arsc->packages[p].pkg_off + 4);
            // Map type_id → {offsets, entries, ec, type_name}
            struct { const uint32_t *offs; const uint8_t *ents; uint32_t ec; char name[32]; } last[256];
            memset(last, 0, sizeof(last));
            while (poff + 8 <= pkg_end) {
                uint16_t pt = arsc_r16(arsc->data + poff);
                uint32_t pcs = arsc_r32(arsc->data + poff + 4);
                if (pcs < 8 || poff + pcs > pkg_end) break;
                if (pt == RES_TABLE_TYPE_TYPE) {
                    uint8_t tid = arsc->data[poff + 8];
                    if (!last[tid].offs) { // first match only for each type
                        last[tid].offs = (const uint32_t*)(arsc->data + poff + arsc_r16(arsc->data + poff + 2));
                        last[tid].ents = arsc->data + poff + arsc_r32(arsc->data + poff + 16);
                        last[tid].ec = arsc_r32(arsc->data + poff + 12);
                        const char *tn = arsc_sp_string(arsc->packages[p].type_pool, tid - 1);
                        if (tn) snprintf(last[tid].name, sizeof(last[tid].name), "%s", tn);
                    }
                }
                poff += pcs;
            }
            for (int ti = 0; ti < 256; ti++) {
                if (!last[ti].offs || !last[ti].ents || !last[ti].name[0]) continue;
                for (uint32_t i = 0; i < last[ti].ec; i++) {
                    if (last[ti].offs[i] == 0xFFFFFFFF) continue;
                    const uint8_t *entry = last[ti].ents + last[ti].offs[i];
                    uint32_t key_idx = arsc_r32(entry + 4);
                    const char *kname = arsc_sp_string(arsc->packages[p].key_pool, key_idx);
                    if (kname)
                        fprintf(pf, "  <public type=\"%s\" name=\"%s\" id=\"0x%02x%02x%04x\" />\n",
                            last[ti].name, kname, arsc->packages[p].id, ti, i);
                }
            }
        }
        fprintf(pf, "</resources>\n");
        fclose(pf);
    }
    
    // 4. Create original/ directory with binary copies
    char orig_dir[4096];
    snprintf(orig_dir, sizeof(orig_dir), "%s/original", out_dir);
    mkdir_p(orig_dir);
    
    // Copy AndroidManifest.xml binary to original/
    char am_bin[4096], am_orig[4096];
    snprintf(am_bin, sizeof(am_bin), "%s/AndroidManifest.xml", out_dir);
    snprintf(am_orig, sizeof(am_orig), "%s/AndroidManifest.xml", orig_dir);
    // 5. Re-encode all decoded XML files back to binary AXML for round-trip
    enc_xml_file(arsc, am_bin);
    enc_xml_dir(arsc, out_dir);  // catches res/ subdirs
    
    arsc_free(arsc);
    free(adata);
    return 0;
}
