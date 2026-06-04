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

                uint8_t vt = e->attr_type[a];
                uint32_t vd = e->attr_data[a];

                if (vt == 0x03 && e->attr_raw[a] >= 0) {
                    const char *vs = get_str(ctx, e->attr_raw[a]);
                    char *esc = escape_xml(vs);
                    append(&ctx->xml, &len, &cap, esc ? esc : "");
                    free(esc);
                } else if (vt == 0x01) {
                    append_fmt(&ctx->xml, &len, &cap, "@ref/0x%08x", vd);
                } else if (vt == 0x10 || vt == 0x13) {
                    append_fmt(&ctx->xml, &len, &cap, "%d", (int32_t)vd);
                } else if (vt == 0x12) {
                    append(&ctx->xml, &len, &cap, vd ? "true" : "false");
                } else if (vt == 0x11) {
                    float f;
                    memcpy(&f, &vd, 4);
                    append_fmt(&ctx->xml, &len, &cap, "%g", f);
                } else {
                    append_fmt(&ctx->xml, &len, &cap, "0x%x", vd);
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
