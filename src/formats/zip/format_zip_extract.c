#include "format_zip_internal.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int make_dirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    return mkdir(tmp, 0755);
}

void *zip_extract_entry(zip_archive *za, int index, size_t *out_size) {
    if (!za || index < 0 || index >= za->num_entries) { printf("invalid za or index\n"); return NULL; }
    struct zip_entry *e = &za->entries[index];
    if (e->lh_offset + sizeof(lfh_t) > za->size) { printf("lh_offset out of bounds\n"); return NULL; }

    lfh_t *lfh = (lfh_t *)(za->data + e->lh_offset);
    if (lfh->sig != LFH_SIG) { printf("lfh sig mismatch %x\n", lfh->sig); return NULL; }

    uint32_t data_off = e->lh_offset + sizeof(lfh_t) + lfh->name_len + lfh->extra_len;
    if (data_off + lfh->comp_size > za->size) { printf("data_off + comp_size > za->size\n"); return NULL; }
    uint8_t *comp_data = za->data + data_off;

    if (e->method == 0) {
        void *out = malloc(e->uncomp_size > 0 ? e->uncomp_size : 1);
        if (!out) { printf("malloc failed\n"); return NULL; }
        if (e->uncomp_size > 0) {
            memcpy(out, comp_data, e->uncomp_size);
        }
        *out_size = e->uncomp_size;
        return out;
    } else if (e->method == 8) {
        if (e->uncomp_size == 0) {
            *out_size = 0;
            return malloc(1);
        }
        void* out = zip_inflate(comp_data, e->comp_size, out_size);
        if (!out) { return NULL; }
        return out;
    }
    return NULL;
}

int zip_extract_all(zip_archive *za, const char *out_dir) {
    if (!za || !out_dir) return -1;
    make_dirs(out_dir);

    for (int i = 0; i < za->num_entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        if (!name) continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", out_dir, name);
        size_t nlen = strlen(name);
        if (nlen > 0 && name[nlen - 1] == '/') {
            mkdir(full, 0755);
            continue;
        }

        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", full);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = '\0'; make_dirs(dir); }

        size_t sz;
        void *data = zip_extract_entry(za, i, &sz);
        if (!data) continue;
        FILE *fp = fopen(full, "wb");
        if (fp) { fwrite(data, 1, sz, fp); fclose(fp); }
        free(data);
    }
    return 0;
}
