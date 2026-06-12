#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int smali_uleb(const uint8_t *data, uint32_t *val, const uint8_t **next);

static int skip_encoded_value(const uint8_t **pp, const uint8_t *end) {
    const uint8_t *p = *pp;
    if (p >= end) return -1;
    uint8_t header = *p++;
    int type = header & 0x1f;
    int arg = (header >> 5) + 1;
    if (p + arg > end) return -1;
    p += arg;
    if (type == 0x0c || type == 0x0d) {
        /* encoded_array or encoded_annotation - skip recursively */
        uint32_t sz;
        if (p + dex_read_uleb128(p, &sz) > end) return -1;
        p += dex_read_uleb128(p, &sz);
        for (uint32_t i = 0; i < sz && p < end; i++) {
            if (skip_encoded_value(&p, end) < 0) return -1;
        }
        if (type == 0x0d) {
            /* annotation has name=value pairs */
            for (uint32_t i = 0; i < sz && p < end; i++) {
                p += dex_read_uleb128(p, &(uint32_t){0});
                if (skip_encoded_value(&p, end) < 0) return -1;
            }
        }
    }
    *pp = p;
    return 0;
}

static int format_encoded_value(const uint8_t **pp, const uint8_t *end, char *buf, size_t bufsz, dex_ctx *ctx, const char *field_type) {
    const uint8_t *p = *pp;
    if (p >= end) { buf[0] = 0; return -1; }
    uint8_t header = *p++;
    int type = header & 0x1f;
    int arg = (header >> 5) + 1;
    if (p + arg > end) { buf[0] = 0; return -1; }
    uint64_t val = 0;
    for (int i = 0; i < arg; i++)
        val |= (uint64_t)p[i] << (i * 8);
    p += arg;
    *pp = p;
    int is_int_field = field_type && (strcmp(field_type, "I") == 0 || strcmp(field_type, "[I") == 0);
    int is_long_field = field_type && (strcmp(field_type, "J") == 0 || strcmp(field_type, "[J") == 0);
    /* sign-extend val based on arg size */
    if (arg < 8) {
        uint64_t sign_bit = 1ULL << (arg * 8 - 1);
        if (val & sign_bit)
            val |= ~((1ULL << (arg * 8)) - 1);
    }
    switch (type) {
    case 0x00: snprintf(buf, bufsz, "%d", (int8_t)val); break;
    case 0x01: snprintf(buf, bufsz, "%d", (int16_t)val); break;
    case 0x02: snprintf(buf, bufsz, "%u", (uint16_t)val); break;
    case 0x03: if ((int32_t)val < 0) snprintf(buf, bufsz, "-0x%x", -(int32_t)val);
               else snprintf(buf, bufsz, "0x%x", (uint32_t)val); break;
    case 0x04: if (is_int_field) { if ((int32_t)val < 0) snprintf(buf, bufsz, "-0x%x", -(int32_t)val); else snprintf(buf, bufsz, "0x%x", (uint32_t)val); }
               else if (is_long_field) snprintf(buf, bufsz, "0x%llxL", (unsigned long long)val);
               else snprintf(buf, bufsz, "0x%llxL", (unsigned long long)val); break;
    case 0x05: { float f; uint32_t fv = (uint32_t)val; memcpy(&f, &fv, 4);
                snprintf(buf, bufsz, "%gf", f); break; }
    case 0x06: { double d; uint64_t dv = val; memcpy(&d, &dv, 8);
                snprintf(buf, bufsz, "%gd", d); break; }
    case 0x07: case 0x17: { uint32_t si = (uint32_t)val;
                if (si < (uint32_t)ctx->sp.count) {
                    const char *str = ctx->sp.strings[si];
                    size_t sl = strlen(str);
                    size_t pos = 0;
                    buf[pos++] = '"';
                    for (size_t j = 0; j < sl && pos < bufsz - 4; j++) {
                        char c = str[j];
                        if (c == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; }
                        else if (c == '\r') { buf[pos++] = '\\'; buf[pos++] = 'r'; }
                        else if (c == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; }
                        else if (c == '\0') { buf[pos++] = '\\'; buf[pos++] = '0'; }
                        else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
                        else if (c == '"') { buf[pos++] = '\\'; buf[pos++] = '"'; }
                        else if (c == '\'') { buf[pos++] = '\\'; buf[pos++] = '\''; }
                        else buf[pos++] = c;
                    }
                    buf[pos++] = '"'; buf[pos] = 0;
                } else buf[0] = 0;
                break; }
    case 0x08: { uint32_t ti = (uint32_t)val;
                if (ti < ctx->type_count) {
                    const char *ts = ctx->sp.strings[ctx->type_ids[ti]];
                    char *t = smali_tsm(ts);
                    snprintf(buf, bufsz, "%s", t ?: ts);
                    free(t);
                } else buf[0] = 0;
                break; }
    case 0x0b: if ((int32_t)val < 0) snprintf(buf, bufsz, "-0x%x", -(int32_t)val);
               else snprintf(buf, bufsz, "0x%x", (uint32_t)val); break;
    case 0x1f: snprintf(buf, bufsz, "null"); break;
    default: buf[0] = 0; break;
    }
    return 0;
}

static void format_static_value(const uint8_t *p, const uint8_t *end, int idx, char *buf, size_t bufsz, dex_ctx *ctx, const char *field_type) {
    buf[0] = 0;
    if (!p || p >= end) return;
    uint32_t size;
    int adv = dex_read_uleb128(p, &size);
    p += adv;
    if (idx < 0 || idx >= (int)size) return;
    for (int i = 0; i < idx; i++) {
        if (skip_encoded_value(&p, end) < 0) return;
    }
    format_encoded_value(&p, end, buf, bufsz, ctx, field_type);
}

char *dex_to_smali_class(dex_ctx *ctx, uint32_t ci) {
    if (ci >= ctx->class_count) return NULL;
    dex_class *c = &ctx->classes[ci];
    uint32_t ti = ctx->type_ids[c->class_idx];
    const char *cn = ti < (uint32_t)ctx->sp.count ? ctx->sp.strings[ti] : "??";
    const char *sp = NULL;
    if (c->superclass_idx != 0xFFFFFFFF && c->superclass_idx < ctx->type_count) {
        uint32_t si = ctx->type_ids[c->superclass_idx];
        if (si < (uint32_t)ctx->sp.count) sp = ctx->sp.strings[si];
    }
    int has_src = c->source_file_idx != 0xFFFFFFFF
                  && c->source_file_idx < (uint32_t)ctx->sp.count;
    const char *src = has_src ? ctx->sp.strings[c->source_file_idx] : NULL;

    char *c2 = smali_tsm(cn), *s2 = sp ? smali_tsm(sp) : NULL;
    size_t cap = 65536, len = 0; char *buf = malloc(cap);
    if (!buf) { free(c2); free(s2); return NULL; }
    buf[0] = 0;
    smali_sb sb = {&buf, &len, &cap};

    /* class header */
    const char *af = smali_aflags(c->access_flags);
    if (af[0])
        smali_sf(&sb, ".class %s %s\n", af, c2 ?: cn);
    else
        smali_sf(&sb, ".class %s\n", c2 ?: cn);
    if (s2) smali_sf(&sb, ".super %s\n", s2);
    if (src) smali_sf(&sb, ".source \"%s\"\n", src);

    /* interfaces */
    if (c->interfaces_off && c->interfaces_off + 4 <= ctx->size) {
        uint32_t ic; memcpy(&ic, ctx->data + c->interfaces_off, 4);
        if (ic > 0) {
            smali_sa(&sb, "\n# interfaces\n");
            for (uint32_t ii = 0; ii < ic; ii++) {
                if (c->interfaces_off + 4 + ii * 2 + 2 > ctx->size) break;
                uint16_t ti2;
                memcpy(&ti2, ctx->data + c->interfaces_off + 4 + ii * 2, 2);
                if (ti2 < ctx->type_count) {
                    const char *iface = ctx->sp.strings[ctx->type_ids[ti2]];
                    char *i2 = smali_tsm(iface);
                    smali_sf(&sb, ".implements %s\n", i2 ?: iface);
                    free(i2);
                }
            }
        }
    }

        /* class-level annotations */
    smali_write_annotations(&sb, ctx, c, 0, 0);

    /* static fields */
    if (c->static_count > 0) {
        smali_sa(&sb, "\n\n# static fields\n");
        for (int fi = 0; fi < c->static_count; fi++) {
            dex_field_enc *fe = &c->static_fields[fi];
            if (fe->field_idx < ctx->field_count) {
                dex_field *f = &ctx->fields[fe->field_idx];
                char *ft = smali_tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                const char *af = smali_aflags(fe->access_flags);
                smali_write_annotations(&sb, ctx, c, 1, fe->field_idx);
                char init_val[1024] = {0};
                if (c->static_values_off && (fe->access_flags & 0x0008) && (fe->access_flags & 0x0010)) {
                    const uint8_t *sv = ctx->data + c->static_values_off;
                    const char *raw_type = ctx->sp.strings[ctx->type_ids[f->type_idx]];
                    if (sv < ctx->data + ctx->size)
                        format_static_value(sv, ctx->data + ctx->size, fi, init_val, sizeof(init_val), ctx, raw_type);
                }
                if (af[0])
                    smali_sf(&sb, ".field %s %s:%s%s%s\n",
                        af, ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]],
                        init_val[0] ? " = " : "", init_val[0] ? init_val : "");
                else
                    smali_sf(&sb, ".field %s:%s%s%s\n",
                        ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]],
                        init_val[0] ? " = " : "", init_val[0] ? init_val : "");
                free(ft);
                smali_sa(&sb, "\n");
            }
        }
    }

    /* instance fields */
    if (c->instance_count > 0) {
        smali_sa(&sb, c->static_count > 0 ? "\n# instance fields\n" : "\n\n# instance fields\n");
        for (int fi = 0; fi < c->instance_count; fi++) {
            dex_field_enc *fe = &c->instance_fields[fi];
            if (fe->field_idx < ctx->field_count) {
                dex_field *f = &ctx->fields[fe->field_idx];
                char *ft = smali_tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                const char *af = smali_aflags(fe->access_flags);
                smali_write_annotations(&sb, ctx, c, 1, fe->field_idx);
                if (af[0])
                    smali_sf(&sb, ".field %s %s:%s\n",
                        af, ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                else
                    smali_sf(&sb, ".field %s:%s\n",
                        ctx->sp.strings[f->name_idx],
                        ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
                free(ft);
                smali_sa(&sb, "\n");
            }
        }
    }

    int had_fields = (c->static_count + c->instance_count) > 0;

    /* direct methods */
    if (c->direct_count > 0) {
        smali_sa(&sb, had_fields ? "\n# direct methods\n" : "\n\n# direct methods\n");
        for (int mi = 0; mi < c->direct_count; mi++) {
            smali_dm(&sb, ctx, &c->direct[mi]);
            if (mi < c->direct_count - 1) smali_sa(&sb, "\n");
        }
    }

    /* virtual methods */
    if (c->virtual_count > 0) {
        int prev = had_fields || c->direct_count > 0;
        smali_sa(&sb, prev ? "\n\n# virtual methods\n" : "\n\n# virtual methods\n");
        for (int mi = 0; mi < c->virtual_count; mi++) {
            smali_dm(&sb, ctx, &c->virtual[mi]);
            if (mi < c->virtual_count - 1) smali_sa(&sb, "\n");
        }
    }

    free(c2); free(s2);
    return buf;
}
