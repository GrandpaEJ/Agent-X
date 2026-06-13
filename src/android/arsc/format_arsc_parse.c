#include "format_arsc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void r16(const uint8_t *p, uint16_t *v) {
    *v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void r32(const uint8_t *p, uint32_t *v) {
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

arsc_ctx *arsc_parse(const uint8_t *data, size_t size) {
    if (!data || size < 12) return NULL;

    uint16_t type, header_size;
    uint32_t chunk_size;
    r16(data, &type);
    r16(data + 2, &header_size);
    r32(data + 4, &chunk_size);

    if (type != RES_TABLE_TYPE || chunk_size > size) {
        return NULL; // Not a valid resources.arsc root chunk
    }

    arsc_ctx *ctx = calloc(1, sizeof(arsc_ctx));
    if (!ctx) return NULL;

    ctx->data = data;
    ctx->size = chunk_size;

    // Scan for RES_STRING_POOL_TYPE chunk directly after the root header
    uint32_t off = header_size;
    while (off + 8 <= chunk_size) {
        uint16_t c_type, c_hdr_size;
        uint32_t c_size;
        r16(data + off, &c_type);
        r16(data + off + 2, &c_hdr_size);
        r32(data + off + 4, &c_size);

        if (c_size < 8 || off + c_size > chunk_size) break;

        if (c_type == RES_STRING_POOL_TYPE) {
            ctx->string_pool_offset = off;
            ctx->string_pool_data = data + off;
            ctx->string_pool_size = c_size;
            
            uint32_t string_count, flags, strings_start;
            r32(data + off + 8, &string_count);
            r32(data + off + 16, &flags);
            r32(data + off + 20, &strings_start);

            ctx->string_count = string_count;
            ctx->is_utf8 = (flags & (1 << 8)) != 0;
            ctx->string_offsets = (const uint32_t *)(data + off + c_hdr_size);

            ctx->patched = calloc(string_count, sizeof(int));
            ctx->patched_strings = calloc(string_count, sizeof(char *));
            ctx->string_cache = calloc(string_count, sizeof(char *));
            break;
        }

        off += c_size;
    }

    return ctx;
}

const char *arsc_get_string(arsc_ctx *ctx, uint32_t index) {
    if (!ctx || index >= ctx->string_count) return NULL;

    if (ctx->patched[index]) {
        return ctx->patched_strings[index];
    }
    
    if (ctx->string_cache[index]) {
        return ctx->string_cache[index];
    }

    uint32_t strings_start;
    r32(ctx->string_pool_data + 20, &strings_start);

    uint32_t off;
    r32((const uint8_t *)&ctx->string_offsets[index], &off);

    const uint8_t *p = ctx->string_pool_data + strings_start + off;

    if (ctx->is_utf8) {
        // UTF-8
        // Skip char length and byte length
        if (*p & 0x80) p += 2; else p += 1;
        if (*p & 0x80) p += 2; else p += 1;
        ctx->string_cache[index] = strdup((const char *)p);
    } else {
        // UTF-16
        uint16_t len;
        if (*p & 0x8000) { p += 4; } else { r16(p, &len); p += 2; }
        
        char *utf8_str = malloc((len * 3) + 1);
        char *out = utf8_str;
        for (int i = 0; i < len; i++) {
            uint16_t wc;
            r16(p + (i * 2), &wc);
            if (wc < 0x80) {
                *out++ = (char)wc;
            } else if (wc < 0x800) {
                *out++ = 0xC0 | (wc >> 6);
                *out++ = 0x80 | (wc & 0x3F);
            } else {
                *out++ = 0xE0 | (wc >> 12);
                *out++ = 0x80 | ((wc >> 6) & 0x3F);
                *out++ = 0x80 | (wc & 0x3F);
            }
        }
        *out = '\0';
        ctx->string_cache[index] = utf8_str;
    }

    return ctx->string_cache[index];
}

void arsc_free(arsc_ctx *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->string_count; i++) {
        if (ctx->patched[i]) {
            free(ctx->patched_strings[i]);
        }
        if (ctx->string_cache[i]) {
            free(ctx->string_cache[i]);
        }
    }
    free(ctx->patched);
    free(ctx->patched_strings);
    free(ctx->string_cache);
    free(ctx);
}
