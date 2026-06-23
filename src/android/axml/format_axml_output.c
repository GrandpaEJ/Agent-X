#include "format_axml_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static char *escape_xml(const char *s) {
    if (!s) return strdup("");
    size_t cap = strlen(s) * 2 + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t pos = 0;
    for (const char *p = s; *p; p++) {
        if (pos + 6 >= cap) {
            cap *= 2;
            char *n = realloc(out, cap);
            if (!n) { free(out); return NULL; }
            out = n;
        }
        switch (*p) {
        case '&': memcpy(out + pos, "&amp;", 5); pos += 5; break;
        case '<': memcpy(out + pos, "&lt;", 4); pos += 4; break;
        case '>': memcpy(out + pos, "&gt;", 4); pos += 4; break;
        case '"': memcpy(out + pos, "&quot;", 6); pos += 6; break;
        case '\'': memcpy(out + pos, "&apos;", 6); pos += 6; break;
        default: out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return out;
}

static int append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t slen = strlen(s);
    if (*len + slen + 1 > *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
    return 0;
}

static int append_fmt(char **buf, size_t *len, size_t *cap,
                      const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return -1;
    if (*len + (size_t)needed + 1 > *cap) {
        while (*cap < *len + (size_t)needed + 1)
            *cap = *cap ? *cap * 2 : 4096;
        char *n = realloc(*buf, *cap);
        if (!n) return -1;
        *buf = n;
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += needed;
    return 0;
}

static void indent(char **buf, size_t *len, size_t *cap, int depth) {
    for (int i = 0; i < depth; i++)
        append(buf, len, cap, "  ");
}

static const char *get_str(axml_ctx *ctx, int idx) {
    if (idx < 0 || idx >= ctx->pool.count) return NULL;
    return ctx->pool.strings[idx];
}

static const char *get_ns_prefix(const char *uri) {
    if (!uri) return NULL;
    if (strstr(uri, "android")) return "android";
    return NULL;
}

// Hardcoded android enum values for common attributes (matches aapt behavior)
typedef struct { const char *attr; int32_t val; const char *name; } ae_t;
static const ae_t android_enums[] = {
    {"orientation", 0, "horizontal"}, {"orientation", 1, "vertical"},
    {"gravity", 0x10, "center_vertical"}, {"gravity", 0x11, "fill_vertical"},
    {"gravity", 0x30, "center"}, {"gravity", 0x31, "fill"},
    {"gravity", 0x03, "top"}, {"gravity", 0x05, "bottom"},
    {"gravity", 0x01, "left"}, {"gravity", 0x05, "right"}, {"gravity", 0x50, "center_horizontal"},
    {"layout_gravity", 0x10, "center_vertical"}, {"layout_gravity", 0x11, "fill_vertical"},
    {"layout_gravity", 0x30, "center"}, {"layout_gravity", 0x31, "fill"},
    {"layout_gravity", 0x03, "top"}, {"layout_gravity", 0x05, "bottom"},
    {"layout_gravity", 0x01, "left"}, {"layout_gravity", 0x05, "right"},
    {"scrollbars", 0x100, "vertical"}, {"scrollbars", 0x200, "horizontal"},
    {"importantForAccessibility", 0, "auto"}, {"importantForAccessibility", 1, "yes"},
    {"importantForAccessibility", 2, "no"}, {"importantForAccessibility", 4, "noHideDescendants"},
    {"visibility", 0, "visible"}, {"visibility", 1, "invisible"}, {"visibility", 2, "gone"},
};

static const char* fmt_typed(uint8_t type, uint32_t data, int raw_idx, const char *an, const arsc_ctx *arsc) {
    static char buf[64];
    if (type == 0x03 || (type == 0x01 && raw_idx >= 0)) return NULL;
    if (type == 0x01) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "@%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "@ref/0x%08x", data);
        return buf;
    }
    if (type == 0x02) {
        if (arsc) {
            const char *resolved = arsc_lookup_id((arsc_ctx*)arsc, data);
            if (resolved) { snprintf(buf, sizeof(buf), "?%s", resolved); return buf; }
        }
        snprintf(buf, sizeof(buf), "?ref/0x%08x", data);
        return buf;
    }
    if (type == 0x04) { float f; memcpy(&f, &data, 4); snprintf(buf, sizeof(buf), "%g", f); return buf; }
    if (type == 0x05) {
        uint32_t m = data >> 8, r = (data >> 4) & 3, u = data & 0xf;
        float v = (float)m / (float)(1 << (r * 2));
        const char *us = (const char*[]){"px","dp","sp","pt","in","mm"}[u < 6 ? u : 0];
        snprintf(buf, sizeof(buf), v == (float)(int)v ? "%.1f%s" : "%g%s", v, us);
        return buf;
    }
    if (type == 0x06) {
        uint32_t m = data >> 8, r = (data >> 4) & 3, u = data & 0xf;
        float v = (float)m / (float)(1 << (r * 2));
        snprintf(buf, sizeof(buf), "%g%s", v, u ? "%p" : "%");
        return buf;
    }
    if (type == 0x10) {
        int32_t s = (int32_t)data;
        if (s == -1 && an && (strstr(an, "width") || strstr(an, "height"))) return "match_parent";
        if (s == -2 && an && (strstr(an, "width") || strstr(an, "height"))) return "wrap_content";
        if (s == -1) return "fill_parent";
        if (an && arsc) {
            // For non-layout attrs, use the enum table
            (void)arsc;
        }
        if (an) {
            for (size_t i = 0; i < sizeof(android_enums)/sizeof(android_enums[0]); i++) {
                if (strcmp(an, android_enums[i].attr) == 0 && s == android_enums[i].val)
                    return android_enums[i].name;
            }
        }
        snprintf(buf, sizeof(buf), "%d", s);
        return buf;
    }
    if (type == 0x12) return data ? "true" : "false";
    if (type >= 0x13 && type <= 0x16) { snprintf(buf, sizeof(buf), "#%08x", data); return buf; }
    if (type == 0x11) {
        // Check for android enums that use hex values (gravity uses hex)
        if (an) {
            for (size_t i = 0; i < sizeof(android_enums)/sizeof(android_enums[0]); i++) {
                if (strcmp(an, android_enums[i].attr) == 0 && (int32_t)data == android_enums[i].val)
                    return android_enums[i].name;
            }
        }
        snprintf(buf, sizeof(buf), "0x%x", data);
        return buf;
    }
    snprintf(buf, sizeof(buf), "0x%x", data);
    return buf;
}

char *axml_get_xml(axml_ctx *ctx) {
    if (!ctx) return NULL;
    if (ctx->xml) return ctx->xml;

    size_t cap = 4096, len = 0;
    ctx->xml = malloc(cap);
    if (!ctx->xml) return NULL;
    ctx->xml[0] = '\0';

    append(&ctx->xml, &len, &cap, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");

    int depth = 0;
    int ns_stack[64];
    int ns_depth = 0;

    for (int i = 0; i < ctx->event_count; i++) {
        switch (ctx->event_types[i]) {
        case 0: // start ns
            if (ns_depth < 64) ns_stack[ns_depth++] = i;
            break;
        case 1: // end ns
            if (ns_depth > 0) ns_depth--;
            break;
        case 2: { // start elem
            indent(&ctx->xml, &len, &cap, depth);
            append(&ctx->xml, &len, &cap, "<");
            const char *name = get_str(ctx, ctx->event_name[i]);
            if (!name) name = "unknown";
            const char *ns = get_str(ctx, ctx->event_ns[i]);
            if (ns && ns[0]) {
                const char *p = get_ns_prefix(ns);
                if (p) {
                    append_fmt(&ctx->xml, &len, &cap, "%s:", p);
                }
            }
            append(&ctx->xml, &len, &cap, name);

            // Check if we need to emit namespaces
            int ns_out = 0;
            for (int j = 0; j < ns_depth; j++) {
                int ei = ns_stack[j];
                const char *p = get_str(ctx, ctx->event_ns[ei]);
                const char *u = get_str(ctx, ctx->event_name[ei]);
                if (p && u) {
                    append_fmt(&ctx->xml, &len, &cap,
                        " xmlns:%s=\"%s\"", p, u);
                    ns_out = 1;
                }
            }
            if (!ns_out && ns && ns[0]) {
                append_fmt(&ctx->xml, &len, &cap,
                    " xmlns=\"%s\"", ns);
            }

            axml_element *e = &ctx->elements[i];
            for (int a = 0; a < e->attr_count; a++) {
                const char *an = get_str(ctx, e->attr_name[a]);
                if (!an) continue;
                append(&ctx->xml, &len, &cap, " ");
                const char *ans = get_str(ctx, e->attr_ns[a]);
                if (ans && ans[0]) {
                    const char *p = get_ns_prefix(ans);
                    if (p) append_fmt(&ctx->xml, &len, &cap, "%s:", p);
                }
                append(&ctx->xml, &len, &cap, an);
                append(&ctx->xml, &len, &cap, "=\"");

                const char *fs = fmt_typed(e->attr_type[a], e->attr_data[a], e->attr_raw[a], an, ctx->arsc);
                if (fs) {
                    char *esc = escape_xml(fs);
                    append(&ctx->xml, &len, &cap, esc ? esc : "");
                    free(esc);
                } else if (e->attr_raw[a] >= 0) {
                    const char *vs = get_str(ctx, e->attr_raw[a]);
                    if (vs) { char *esc = escape_xml(vs); append(&ctx->xml, &len, &cap, esc ? esc : ""); free(esc); }
                }
                append(&ctx->xml, &len, &cap, "\"");
            }

            if (i + 1 < ctx->event_count && ctx->event_types[i + 1] == 3) {
                append(&ctx->xml, &len, &cap, " />\n");
                i++;
            } else {
                append(&ctx->xml, &len, &cap, ">\n");
                depth++;
            }
            break;
        }
        case 3: // end elem
            depth--;
            indent(&ctx->xml, &len, &cap, depth);
            append(&ctx->xml, &len, &cap, "</");
            const char *name = get_str(ctx, ctx->event_name[i]);
            if (!name) name = "unknown";
            const char *ns = get_str(ctx, ctx->event_ns[i]);
            if (ns && ns[0]) {
                const char *p = get_ns_prefix(ns);
                if (p)
                    append_fmt(&ctx->xml, &len, &cap, "%s:", p);
            }
            append_fmt(&ctx->xml, &len, &cap, "%s>\n", name);
            break;
        case 4: { // text
            const char *txt = get_str(ctx, ctx->event_name[i]);
            if (txt) {
                indent(&ctx->xml, &len, &cap, depth);
                char *esc = escape_xml(txt);
                append(&ctx->xml, &len, &cap, esc ? esc : "");
                free(esc);
                append(&ctx->xml, &len, &cap, "\n");
            }
            break;
        }
        }
    }

    return ctx->xml;
}
