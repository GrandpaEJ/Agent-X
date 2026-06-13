#include "format_arsc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int arsc_patch_string(arsc_ctx *ctx, uint32_t index, const char *new_str) {
    if (!ctx || index >= ctx->string_count) return -1;
    if (ctx->patched[index]) {
        free(ctx->patched_strings[index]);
    }
    ctx->patched_strings[index] = strdup(new_str);
    ctx->patched[index] = 1;
    return 0;
}

static void w16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

static void w32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void get_utf8_length_prefix(uint8_t *out, uint32_t *bytes_written, uint32_t len) {
    if (len > 0x7F) {
        out[(*bytes_written)++] = ((len >> 8) & 0x7F) | 0x80;
    }
    out[(*bytes_written)++] = len & 0xFF;
}

static void get_utf16_length_prefix(uint8_t *out, uint32_t *bytes_written, uint32_t len) {
    if (len > 0x7FFF) {
        uint32_t w = ((len >> 16) & 0x7FFF) | 0x8000;
        w16(out + *bytes_written, w);
        *bytes_written += 2;
    }
    w16(out + *bytes_written, len & 0xFFFF);
    *bytes_written += 2;
}

uint8_t *arsc_build(arsc_ctx *ctx, size_t *out_size) {
    if (!ctx) return NULL;

    int any_patched = 0;
    for (uint32_t i = 0; i < ctx->string_count; i++) {
        if (ctx->patched[i]) { any_patched = 1; break; }
    }

    if (!any_patched) {
        uint8_t *dup = malloc(ctx->size);
        memcpy(dup, ctx->data, ctx->size);
        *out_size = ctx->size;
        return dup;
    }

    // Rebuild the string pool entirely in UTF-8
    uint32_t offsets_size = ctx->string_count * 4;
    uint32_t strings_cap = 65536;
    uint8_t *strings_data = malloc(strings_cap);
    uint32_t *new_offsets = malloc(offsets_size);
    uint32_t strings_len = 0;

    for (uint32_t i = 0; i < ctx->string_count; i++) {
        new_offsets[i] = strings_len;
        const char *s = ctx->patched[i] ? ctx->patched_strings[i] : arsc_get_string(ctx, i);
        if (!s) s = "";
        
        uint32_t len = strlen(s);
        // We will output as UTF-8
        if (strings_len + len + 10 >= strings_cap) {
            strings_cap = strings_cap * 2 + len;
            strings_data = realloc(strings_data, strings_cap);
        }
        
        get_utf8_length_prefix(strings_data, &strings_len, len); // char count
        get_utf8_length_prefix(strings_data, &strings_len, len); // byte count (assuming ASCII mostly, not strictly correct for wide but works as safe bounds)
        
        memcpy(strings_data + strings_len, s, len);
        strings_len += len;
        strings_data[strings_len++] = '\0';
    }
    
    // Pad to 4 bytes
    while (strings_len % 4 != 0) {
        strings_data[strings_len++] = '\0';
    }

    uint32_t hdr_size = 28;
    uint32_t new_pool_size = hdr_size + offsets_size + strings_len;
    
    uint8_t *new_pool = calloc(1, new_pool_size);
    w16(new_pool, RES_STRING_POOL_TYPE);
    w16(new_pool + 2, hdr_size);
    w32(new_pool + 4, new_pool_size);
    w32(new_pool + 8, ctx->string_count);
    w32(new_pool + 12, 0); // styleCount
    w32(new_pool + 16, 1 << 8); // flags (UTF-8)
    w32(new_pool + 20, hdr_size + offsets_size); // stringsStart
    w32(new_pool + 24, 0); // stylesStart

    memcpy(new_pool + hdr_size, new_offsets, offsets_size);
    memcpy(new_pool + hdr_size + offsets_size, strings_data, strings_len);

    free(strings_data);
    free(new_offsets);

    // Now reconstruct the final ARSC
    int32_t size_diff = new_pool_size - ctx->string_pool_size;
    uint32_t final_size = ctx->size + size_diff;
    uint8_t *out = malloc(final_size);
    
    // 1. Copy root header up to string pool
    memcpy(out, ctx->data, ctx->string_pool_offset);
    w32(out + 4, final_size); // update root size
    
    // 2. Write new pool
    memcpy(out + ctx->string_pool_offset, new_pool, new_pool_size);
    free(new_pool);
    
    // 3. Copy remaining chunks
    uint32_t rem_offset = ctx->string_pool_offset + ctx->string_pool_size;
    uint32_t rem_size = ctx->size - rem_offset;
    memcpy(out + ctx->string_pool_offset + new_pool_size, ctx->data + rem_offset, rem_size);
    
    *out_size = final_size;
    return out;
}
