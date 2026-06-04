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

void axml_free(axml_ctx *ctx);

int axml_add_event(axml_ctx *ctx, int type, int line, int ns, int name) {
    if (ctx->event_count >= ctx->event_cap) {
        int nc = ctx->event_cap ? ctx->event_cap * 2 : 64;
        int *nt = realloc(ctx->event_types, nc * sizeof(int));
        int *nl = realloc(ctx->event_lines, nc * sizeof(int));
        int *nn = realloc(ctx->event_ns, nc * sizeof(int));
        int *nm = realloc(ctx->event_name, nc * sizeof(int));
        axml_element *ne = realloc(ctx->elements, nc * sizeof(axml_element));
        if (!nt || !nl || !nn || !nm || !ne) return -1;
        ctx->event_types = nt;
        ctx->event_lines = nl;
        ctx->event_ns = nn;
        ctx->event_name = nm;
        ctx->elements = ne;
        ctx->event_cap = nc;
    }
    int i = ctx->event_count++;
    ctx->event_types[i] = type;
    ctx->event_lines[i] = line;
    ctx->event_ns[i] = ns;
    ctx->event_name[i] = name;
    memset(&ctx->elements[i], 0, sizeof(axml_element));
    return 0;
}

static int parse_start_elem(axml_ctx *ctx, const uint8_t *data, uint32_t size) {
    // Chunk header: type(2) + header_size(2) + size(4) = 8 bytes
    // Then: line(4) + comment(4) + ns(4) + name(4) + attr_start(2)
    //       + attr_size(2) + attr_count(2) + id(2) + class(2) + style(2)
    // Attributes start at offset 36
    if (size < 36) return -1;
    uint32_t line, ns_idx, name_idx;
    r32(data + 8, &line);
    r32(data + 16, &ns_idx);
    r32(data + 20, &name_idx);

    uint16_t attr_count;
    r16(data + 28, &attr_count);

    int idx = ctx->event_count;
    if (axml_add_event(ctx, 2, (int)line, (int)ns_idx, (int)name_idx)) return -1;
    axml_element *e = &ctx->elements[idx];
    e->attr_count = attr_count;

    if (attr_count == 0) return 0;

    e->attr_ns = calloc(attr_count, sizeof(int));
    e->attr_name = calloc(attr_count, sizeof(int));
    e->attr_raw = calloc(attr_count, sizeof(int));
    e->attr_type = calloc(attr_count, sizeof(uint8_t));
    e->attr_data = calloc(attr_count, sizeof(uint32_t));
    if (!e->attr_ns || !e->attr_name || !e->attr_raw || !e->attr_type || !e->attr_data)
        return -1;

    for (uint16_t i = 0; i < attr_count; i++) {
        uint32_t a_off = 36 + i * 20;
        if (a_off + 20 > size) break;
        uint32_t ans, aname, raw_val;
        r32(data + a_off, &ans);
        r32(data + a_off + 4, &aname);
        r32(data + a_off + 8, &raw_val);
        uint8_t val_type = data[a_off + 15];
        uint32_t val_data;
        r32(data + a_off + 16, &val_data);
        e->attr_ns[i] = (int)ans;
        e->attr_name[i] = (int)aname;
        e->attr_raw[i] = (int)raw_val;
        e->attr_type[i] = val_type;
        e->attr_data[i] = val_data;
    }
    return 0;
}

axml_ctx *axml_parse(const uint8_t *data, size_t size) {
    if (!data || size < 8) return NULL;

    uint32_t magic, file_size;
    r32(data, &magic);
    r32(data + 4, &file_size);
    if (magic != AXML_MAGIC) return NULL;
    if (file_size > size) return NULL;

    axml_ctx *ctx = calloc(1, sizeof(axml_ctx));
    if (!ctx) return NULL;

    uint32_t off = 8;
    while (off + 8 <= file_size) {
        uint16_t type, hdr_size;
        uint32_t chunk_size;
        r16(data + off, &type);
        r16(data + off + 2, &hdr_size);
        r32(data + off + 4, &chunk_size);

        if (chunk_size < 8 || off + chunk_size > file_size) break;
        const uint8_t *chunk_data = data + off;

        switch (type) {
        case CHUNK_STRING_POOL:
            if (axml_parse_strpool(ctx, chunk_data, chunk_size))
                { axml_free(ctx); return NULL; }
            break;
        case CHUNK_RESOURCE_MAP:
            axml_parse_resmap(ctx, chunk_data, chunk_size);
            break;
        case CHUNK_START_NS: {
            uint32_t line, prefix, uri;
            r32(chunk_data + 8, &line);
            r32(chunk_data + 16, &prefix);
            r32(chunk_data + 20, &uri);
            axml_add_event(ctx, 0, (int)line, (int)prefix, (int)uri);
            break;
        }
        case CHUNK_END_NS: {
            uint32_t line, prefix, uri;
            r32(chunk_data + 8, &line);
            r32(chunk_data + 16, &prefix);
            r32(chunk_data + 20, &uri);
            axml_add_event(ctx, 1, (int)line, (int)prefix, (int)uri);
            break;
        }
        case CHUNK_START_ELEM:
            parse_start_elem(ctx, chunk_data, chunk_size);
            break;
        case CHUNK_END_ELEM: {
            uint32_t line, ns, name;
            r32(chunk_data + 8, &line);
            r32(chunk_data + 16, &ns);
            r32(chunk_data + 20, &name);
            axml_add_event(ctx, 3, (int)line, (int)ns, (int)name);
            break;
        }
        case CHUNK_TEXT: {
            uint32_t line, val_idx;
            r32(chunk_data + 8, &line);
            r32(chunk_data + 16, &val_idx);
            if (val_idx == 0xFFFFFFFF) break;
            axml_add_event(ctx, 4, (int)line, 0, (int)val_idx);
            break;
        }
        }
        off += chunk_size;
    }

    return ctx;
}

void axml_free(axml_ctx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->pool.count; i++)
        free(ctx->pool.strings[i]);
    free(ctx->pool.strings);
    free(ctx->resmap.ids);
    for (int i = 0; i < ctx->event_count; i++) {
        free(ctx->elements[i].attr_ns);
        free(ctx->elements[i].attr_name);
        free(ctx->elements[i].attr_raw);
        free(ctx->elements[i].attr_type);
        free(ctx->elements[i].attr_data);
    }
    free(ctx->event_types);
    free(ctx->event_lines);
    free(ctx->event_ns);
    free(ctx->event_name);
    free(ctx->elements);
    free(ctx->xml);
    free(ctx);
}
