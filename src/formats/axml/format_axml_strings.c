#include "format_axml_internal.h"
#include <stdlib.h>
#include <string.h>

static void r16(const uint8_t *p, uint16_t *v) {
    *v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void r32(const uint8_t *p, uint32_t *v) {
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_utf8_str(const uint8_t *data, uint32_t data_len,
                         uint32_t off, char **out) {
    if (off >= data_len) return -1;
    const uint8_t *p = data + off;
    const uint8_t *end = data + data_len;
    if (p + 2 > end) return -1;
    if (*p & 0x80) {
        uint16_t h = ((uint16_t)(*p & 0x7F)) << 8;
        if (p + 2 > end) return -1;
        h |= *(p + 1);
        p += 2;
    } else {
        p += 1;
    }
    uint16_t b_len;
    if (p + 2 > end) return -1;
    if (*p & 0x80) {
        uint16_t h = ((uint16_t)(*p & 0x7F)) << 8;
        if (p + 2 > end) return -1;
        h |= *(p + 1);
        b_len = h;
        p += 2;
    } else {
        b_len = *p;
        p += 1;
    }
    if ((size_t)(b_len) > (size_t)(end - p)) return -1;
    *out = malloc(b_len + 1);
    if (!*out) return -1;
    memcpy(*out, p, b_len);
    (*out)[b_len] = '\0';
    return 0;
}

static int read_utf16_str(const uint8_t *data, uint32_t data_len,
                          uint32_t off, char **out) {
    if (off + 2 > data_len) return -1;
    uint16_t uc_len;
    r16(data + off, &uc_len);
    off += 2;
    uint32_t byte_len = (uint32_t)uc_len * 2;
    if (off + byte_len > data_len) return -1;

    // Skip null terminator if present
    if (byte_len >= 2 && data[off + byte_len - 2] == 0 && data[off + byte_len - 1] == 0)
        byte_len -= 2;

    *out = malloc(uc_len * 3 + 1);
    if (!*out) return -1;
    int pos = 0;
    for (uint16_t i = 0; i < uc_len; i++) {
        uint16_t cp;
        r16(data + off + i * 2, &cp);
        if (cp < 0x80) {
            (*out)[pos++] = (char)cp;
        } else if (cp < 0x800) {
            (*out)[pos++] = 0xC0 | (cp >> 6);
            (*out)[pos++] = 0x80 | (cp & 0x3F);
        } else {
            (*out)[pos++] = 0xE0 | (cp >> 12);
            (*out)[pos++] = 0x80 | ((cp >> 6) & 0x3F);
            (*out)[pos++] = 0x80 | (cp & 0x3F);
        }
    }
    (*out)[pos] = '\0';
    return 0;
}

static int strpool_grow(axml_strpool *p) {
    int new_cap = p->cap ? p->cap * 2 : 64;
    char **ns = realloc(p->strings, new_cap * sizeof(char *));
    if (!ns) return -1;
    p->strings = ns;
    p->cap = new_cap;
    return 0;
}

int axml_parse_strpool(axml_ctx *ctx, const uint8_t *data, uint32_t size) {
    if (size < 28) return -1;
    uint32_t str_count, flags, strs_start;
    r32(data + 8, &str_count);
    // skip style_count at +12
    r32(data + 16, &flags);
    r32(data + 20, &strs_start);

    int is_utf8 = (flags & (1 << 8)) != 0;

    // Read string offsets
    uint32_t *offsets = NULL;
    if (str_count > 0) {
        offsets = malloc(str_count * sizeof(uint32_t));
        if (!offsets) return -1;
        for (uint32_t i = 0; i < str_count; i++)
            r32(data + 28 + i * 4, &offsets[i]);
    }

    ctx->pool.count = 0;
    ctx->pool.cap = 0;
    ctx->pool.strings = NULL;

    for (uint32_t i = 0; i < str_count; i++) {
        if (ctx->pool.count >= ctx->pool.cap && strpool_grow(&ctx->pool))
            { free(offsets); return -1; }
        uint32_t off = strs_start + offsets[i];
        char *s = NULL;
        int ret;
        if (is_utf8)
            ret = read_utf8_str(data, size, off, &s);
        else
            ret = read_utf16_str(data, size, off, &s);
        if (ret || !s) {
            s = strdup("");
            if (!s) { free(offsets); return -1; }
        }
        ctx->pool.strings[ctx->pool.count++] = s;
    }

    free(offsets);
    return 0;
}

int axml_parse_resmap(axml_ctx *ctx, const uint8_t *data, uint32_t size) {
    if (size < 8) return -1;
    uint32_t count = (size - 8) / 4;
    ctx->resmap.count = count;
    ctx->resmap.ids = NULL;
    ctx->has_resmap = 1;
    if (count > 0) {
        ctx->resmap.ids = malloc(count * sizeof(uint32_t));
        if (!ctx->resmap.ids) return -1;
        for (uint32_t i = 0; i < count; i++)
            r32(data + 8 + i * 4, &ctx->resmap.ids[i]);
    }
    return 0;
}
