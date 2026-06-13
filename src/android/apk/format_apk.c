#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *apk_analyze(const char *path) {
    if (!path) return NULL;

    zip_archive *za = zip_open(path);
    if (!za) return NULL;

    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) { zip_close(za); return NULL; }
    buf[0] = '\0';

    int entries = zip_get_num_entries(za);
    len += snprintf(buf + len, cap - len,
        "APK: %s  (%d entries)\n", path, entries);

    // Decode AndroidManifest.xml
    int manifest_idx = -1;
    for (int i = 0; i < entries; i++)
        if (strcmp(zip_get_entry_name(za, i), "AndroidManifest.xml") == 0)
            { manifest_idx = i; break; }

    if (manifest_idx >= 0) {
        size_t msize = 0;
        void *mdata = zip_extract_entry(za, manifest_idx, &msize);
        if (mdata && msize > 0) {
            axml_ctx *ax = axml_parse(mdata, msize);
            if (ax) {
                char *xml = axml_get_xml(ax);
                if (xml) {
                    size_t needed = len + strlen(xml) + 50;
                    if (needed > cap) {
                        while (cap < needed) cap *= 2;
                        char *n = realloc(buf, cap);
                        if (n) buf = n;
                    }
                    len += snprintf(buf + len, cap - len,
                        "=== Manifest ===\n%s\n", xml);
                }
                axml_free(ax);
            }
            free(mdata);
        }
    }

    // Decode classes.dex
    int dex_idx = -1;
    for (int i = 0; i < entries; i++)
        if (strcmp(zip_get_entry_name(za, i), "classes.dex") == 0)
            { dex_idx = i; break; }

    if (dex_idx >= 0) {
        size_t dsize = 0;
        void *ddata = zip_extract_entry(za, dex_idx, &dsize);
        if (ddata && dsize > 0) {
            dex_ctx *dx = dex_parse(ddata, dsize);
            if (dx) {
                char *dump = dex_dump(dx);
                if (dump) {
                    size_t needed = len + strlen(dump) + 50;
                    if (needed > cap) {
                        while (cap < needed) cap *= 2;
                        char *n = realloc(buf, cap);
                        if (n) buf = n;
                    }
                    len += snprintf(buf + len, cap - len,
                        "=== Classes ===\n%s", dump);
                }
                free(dump);
                dex_free(dx);
            }
            free(ddata);
        }
    }

    // List ZIP entries
    size_t needed = len + 50;
    if (needed > cap) {
        while (cap < needed) cap *= 2;
        char *n = realloc(buf, cap);
        if (n) buf = n;
    }
    len += snprintf(buf + len, cap - len, "=== Files ===\n");
    for (int i = 0; i < entries; i++) {
        const char *name = zip_get_entry_name(za, i);
        size_t esize = zip_get_entry_size(za, i);
        int comp = zip_entry_is_compressed(za, i);
        needed = len + strlen(name) + 50;
        if (needed > cap) {
            while (cap < needed) cap *= 2;
            char *n = realloc(buf, cap);
            if (n) buf = n;
        }
        len += snprintf(buf + len, cap - len,
            "  %c %7zu  %s\n", comp ? 'D' : ' ', esize, name);
    }

    zip_close(za);
    return buf;
}

void apk_free(char *result) {
    free(result);
}
