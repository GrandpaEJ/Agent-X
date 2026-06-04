#include "formats.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *my_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

#define LFH_SIG  0x04034b50
#define CDFH_SIG 0x02014b50
#define EOCD_SIG 0x06054b50

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t ver_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
} lfh_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t ver_by;
    uint16_t ver_needed;
    uint16_t flags;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t lh_offset;
} cdfh_t;

typedef struct __attribute__((packed)) {
    uint32_t sig;
    uint16_t disk_num;
    uint16_t disk_start;
    uint16_t num_on_disk;
    uint16_t num_total;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
} eocd_t;

struct zip_entry_w {
    char *name;
    uint16_t name_len;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t method;
    uint32_t lh_offset;
};

struct zip_writer {
    FILE *fp;
    int num_entries;
    int cap_entries;
    struct zip_entry_w *entries;
};

static uint32_t crc32_table[256];
static int crc_initialized;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc_initialized = 1;
}

static uint32_t crc32_buf(const void *buf, size_t len) {
    if (!crc_initialized) crc32_init();
    uint32_t c = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        c = crc32_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}


zip_writer *zip_writer_open(const char *path) {
    zip_writer *zw = calloc(1, sizeof(zip_writer));
    if (!zw) return NULL;
    zw->fp = fopen(path, "wb");
    if (!zw->fp) { free(zw); return NULL; }
    zw->cap_entries = 64;
    zw->entries = calloc(zw->cap_entries, sizeof(struct zip_entry_w));
    if (!zw->entries) { fclose(zw->fp); free(zw); return NULL; }
    return zw;
}

int zip_writer_add(zip_writer *zw, const char *name,
                   const void *data, size_t size, int compress) {
    (void)compress; // We always store for now
    if (!zw || !name || !data) return -1;

    if (zw->num_entries >= zw->cap_entries) {
        zw->cap_entries *= 2;
        struct zip_entry_w *tmp = realloc(zw->entries,
            zw->cap_entries * sizeof(struct zip_entry_w));
        if (!tmp) return -1;
        zw->entries = tmp;
    }

    uint16_t name_len = (uint16_t)strlen(name);
    uint32_t crc = crc32_buf(data, size);
    long off = ftell(zw->fp);

    // Write LFH
    lfh_t lfh;
    memset(&lfh, 0, sizeof(lfh));
    lfh.sig = LFH_SIG;
    lfh.ver_needed = 20;
    lfh.method = 0;
    lfh.crc32 = crc;
    lfh.comp_size = (uint32_t)size;
    lfh.uncomp_size = (uint32_t)size;
    lfh.name_len = name_len;

    fwrite(&lfh, sizeof(lfh), 1, zw->fp);
    fwrite(name, 1, name_len, zw->fp);
    fwrite(data, 1, size, zw->fp);

    struct zip_entry_w *e = &zw->entries[zw->num_entries++];
    e->name = my_strdup(name);
    e->name_len = name_len;
    e->crc32 = crc;
    e->comp_size = (uint32_t)size;
    e->uncomp_size = (uint32_t)size;
    e->method = 0;
    e->lh_offset = (uint32_t)off;
    return 0;
}

int zip_writer_close(zip_writer *zw) {
    if (!zw || !zw->fp) return -1;
    long cd_start = ftell(zw->fp);

    // Write Central Directory
    for (int i = 0; i < zw->num_entries; i++) {
        struct zip_entry_w *e = &zw->entries[i];
        cdfh_t cdfh;
        memset(&cdfh, 0, sizeof(cdfh));
        cdfh.sig = CDFH_SIG;
        cdfh.ver_by = 20;
        cdfh.ver_needed = 20;
        cdfh.method = e->method;
        cdfh.crc32 = e->crc32;
        cdfh.comp_size = e->comp_size;
        cdfh.uncomp_size = e->uncomp_size;
        cdfh.name_len = e->name_len;
        cdfh.lh_offset = e->lh_offset;
        fwrite(&cdfh, sizeof(cdfh), 1, zw->fp);
        fwrite(e->name, 1, e->name_len, zw->fp);
    }

    long cd_end = ftell(zw->fp);
    long cd_size = cd_end - cd_start;

    // Write EOCD
    eocd_t eocd;
    memset(&eocd, 0, sizeof(eocd));
    eocd.sig = EOCD_SIG;
    eocd.num_on_disk = (uint16_t)zw->num_entries;
    eocd.num_total = (uint16_t)zw->num_entries;
    eocd.cd_size = (uint32_t)cd_size;
    eocd.cd_offset = (uint32_t)cd_start;
    fwrite(&eocd, sizeof(eocd), 1, zw->fp);

    fclose(zw->fp);
    for (int i = 0; i < zw->num_entries; i++)
        free(zw->entries[i].name);
    free(zw->entries);
    free(zw);
    return 0;
}

int zipalign_file(const char *in_path, const char *out_path, int alignment) {
    zip_archive *za = zip_open(in_path);
    if (!za) return -1;
    zip_writer *zw = zip_writer_open(out_path);
    if (!zw) { zip_close(za); return -1; }

    int n = zip_get_num_entries(za);
    for (int i = 0; i < n; i++) {
        size_t sz;
        void *data = zip_extract_entry(za, i, &sz);
        if (!data) { zip_close(za); zip_writer_close(zw); return -1; }

        const char *name = zip_get_entry_name(za, i);
        int is_stored = !zip_entry_is_compressed(za, i);

        // For stored entries, pad to alignment
        if (is_stored) {
            long offset = ftell(zw->fp);
            long pad = (alignment - (offset % alignment)) % alignment;
            for (long j = 0; j < pad; j++)
                fputc(0, zw->fp);
        }

        zip_writer_add(zw, name, data, sz, !is_stored);
        free(data);
    }

    zip_close(za);
    return zip_writer_close(zw);
}
