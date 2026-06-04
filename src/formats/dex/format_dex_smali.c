#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int dex_decode_insn(const uint16_t *insns, int offset,
    uint32_t *out_op, uint32_t *out_vA, uint32_t *out_vB, uint32_t *out_vC,
    uint32_t *out_ref, int32_t *out_lit);
extern const char *dex_insn_name(uint8_t op);
extern int dex_insn_kind(uint8_t op);
extern int dex_insn_fmt(uint8_t op);

static const char *aflags(uint32_t f) {
    switch (f & 0x1F) { case 1: return "public"; case 2: return "private";
    case 4: return "protected"; case 8: return "static"; case 16: return "final";
    default: return ""; }
}

static char *tsm(const char *d) {
    if (!d) return strdup("V");
    return strdup(d);
}

typedef struct { char **b; size_t *l, *c; } sb;

static int sa(sb *s, const char *str) {
    size_t sl = strlen(str);
    if (*s->l + sl + 1 > *s->c) {
        *s->c = *s->c ? *s->c * 2 : 8192;
        char *n = realloc(*s->b, *s->c);
        if (!n) return -1; *s->b = n;
    }
    memcpy(*s->b + *s->l, str, sl); *s->l += sl; (*s->b)[*s->l] = 0;
    return 0;
}

static int sf(sb *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n < 0) return -1;
    if (*s->l + (size_t)n + 1 > *s->c) {
        while (*s->c < *s->l + (size_t)n + 1) *s->c = *s->c ? *s->c * 2 : 8192;
        char *p = realloc(*s->b, *s->c); if (!p) return -1; *s->b = p;
    }
    va_start(ap, fmt); vsnprintf(*s->b + *s->l, *s->c - *s->l, fmt, ap); va_end(ap);
    *s->l += n; return 0;
}

static const char *res(dex_ctx *ctx, int k, uint32_t i) {
    switch (k) {
    case 1: return i < ctx->string_count ? ctx->sp.strings[i] : "??";
    case 2: return i < ctx->type_count ? ctx->sp.strings[ctx->type_ids[i]] : "??";
    case 3:
        if (i < ctx->field_count) {
            dex_field *f = &ctx->fields[i];
            char *c = tsm(res(ctx,2,f->class_idx)), *t = tsm(res(ctx,2,f->type_idx));
            static char b[256];
            snprintf(b,sizeof(b), "%s->%s:%s", c?:res(ctx,2,f->class_idx), res(ctx,1,f->name_idx), t?:res(ctx,2,f->type_idx));
            free(c); free(t); return b;
        } return "??";
    case 4:
        if (i < ctx->method_count) {
            dex_method_id *m = &ctx->methods[i];
            dex_proto *p = &ctx->protos[m->proto_idx];
            char *c = tsm(res(ctx,2,m->class_idx)), *rt = tsm(res(ctx,2,p->return_type_idx));
            char par[256] = "";
            if (p->params_off && p->params_off+4 <= ctx->size) {
                uint32_t po = p->params_off, pc;
                memcpy(&pc, ctx->data+po, 4); po += 4;
                if (po + pc*2 <= ctx->size) for (uint32_t pi=0; pi<pc && pi<20; pi++) {
                    uint16_t ti; memcpy(&ti, ctx->data+po+pi*2, 2);
                    const char *pt = res(ctx,2,ti);
                    if (pi && strlen(par)<240) strcat(par,",");
                    if (strlen(par)<240) strcat(par, pt?:res(ctx,2,ti));
                }
            }
            static char b[512];
            snprintf(b,sizeof(b), "%s->%s(%s)%s", c?:res(ctx,2,m->class_idx), res(ctx,1,m->name_idx), par, rt?:res(ctx,2,p->return_type_idx));
            free(c); free(rt); return b;
        } return "??";
    default: return "";
    }
}

static void mproto(dex_ctx *ctx, uint32_t mi, char *b, size_t z) {
    if (mi >= ctx->method_count) { snprintf(b,z,"()V"); return; }
    dex_method_id *m = &ctx->methods[mi];
    dex_proto *p = &ctx->protos[m->proto_idx];
    const char *rt = res(ctx,2,p->return_type_idx);
    char pbuf[256] = "";
    if (p->params_off && p->params_off+4 <= ctx->size) {
        uint32_t po=p->params_off, pc;
        memcpy(&pc, ctx->data+po, 4); po += 4;
        if (po+pc*2 <= ctx->size) for (uint32_t i=0; i<pc; i++) {
            uint16_t ti; memcpy(&ti, ctx->data+po+i*2, 2);
            const char *pt = res(ctx,2,ti);
            strncat(pbuf, pt ?: "?", sizeof(pbuf)-strlen(pbuf)-1);
        }
    }
    snprintf(b,z,"(%s)%s", pbuf, rt ?: "V");
}

static int dm(sb *s, dex_ctx *ctx, dex_method_enc *me) {
    if (me->method_idx >= ctx->method_count || me->code_off+16 > ctx->size)
        return sf(s, ".method unknown\n.end method\n");
    dex_method_id *mi = &ctx->methods[me->method_idx];
    const char *mn = mi->name_idx < (uint32_t)ctx->sp.count ? ctx->sp.strings[mi->name_idx] : "??";
    uint32_t fl = me->access_flags;
    char protobuf[512];
    mproto(ctx, me->method_idx, protobuf, sizeof(protobuf));
    if (me->code_off == 0)
        return sf(s, ".method %s%s %s%s\n.end method\n", aflags(fl), (fl&8)?"":"", mn, protobuf);

    const uint8_t *p = ctx->data + me->code_off;
    uint16_t regs, ins, tries; uint32_t isz;
    memcpy(&regs, p, 2); memcpy(&ins, p+2, 2);
    memcpy(&tries, p+6, 2); memcpy(&isz, p+12, 4);
    const uint16_t *insns = (const uint16_t *)(p + 16);
    sf(s, ".method %s%s %s%s\n", aflags(fl), (fl&8)?"":"", mn, protobuf);
    sf(s, "    .registers %u\n    .locals %u\n", (unsigned)regs, (unsigned)(regs-ins));

    #define NL 256
    uint32_t lo[NL]; int lc = 0;
    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) break;
        int fm = dex_insn_fmt((uint8_t)op);
        int bo = (fm==4||fm==5||fm==6) ? lt : ((fm==9||fm==10) ? (int32_t)(int16_t)rf : 0);
        if (bo) { int t = i+bo; if (t>=0 && (uint32_t)t<isz && lc<NL) lo[lc++] = (uint32_t)t; }
        i += w;
    }
    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) { sa(s, "    ; invalid\n"); break; }
        const char *nm = dex_insn_name((uint8_t)op);
        if (!nm) { sf(s, "    ; unknown 0x%02x\n", op); i+=w; continue; }
        int k = dex_insn_kind((uint8_t)op), fm = dex_insn_fmt((uint8_t)op);

        for (int li = 0; li < lc; li++) if (lo[li] == (uint32_t)i) sf(s, "    :cond_%d\n", li);
        sa(s, "    "); sa(s, nm);

        #define R(r) (unsigned)(r)
        switch (fm) {
        case 0: break;
        case 1: case 21: sf(s, " v%u, v%u", R(vA), R(vB)); break;
        case 2: sf(s, " v%u, #%d", R(vA), lt); break;
        case 3: sf(s, " v%u", R(vA)); break;
        case 4: case 5: case 6: { int t=i+lt, fl=-1; for(int li=0;li<lc;li++) if(lo[li]==(uint32_t)t){fl=li;break;} sf(s,fl>=0?" :cond_%d":" +%d",fl>=0?fl:lt); break; }
        case 7: case 19:
            if (k==1) sf(s, " v%u, \"%s\"", R(vA), res(ctx,k,rf));
            else if (k>=2) { const char *r = res(ctx,k,rf); if (k==2) {char*t=tsm(r);sf(s," v%u, %s",R(vA),t?:r);free(t);} else sf(s," v%u, %s",R(vA),r); }
            else sf(s, " v%u, #%d", R(vA), (int)(int16_t)rf);
            break;
        case 8:
            if (k>=2) { const char *r=res(ctx,k,rf); if(k==2){char*t=tsm(r);sf(s," v%u, v%u, %s",R(vA),R(vB),t?:r);free(t);} else sf(s," v%u, v%u, %s",R(vA),R(vB),r); }
            else sf(s, " v%u, v%u, #%x", R(vA), R(vB), rf);
            break;
        case 9: { int br=(int32_t)(int16_t)rf, t=i+br, fl=-1; for(int li=0;li<lc;li++) if(lo[li]==(uint32_t)t){fl=li;break;} sf(s," v%u, ",R(vA)); sf(s,fl>=0?":cond_%d":"+%d",fl>=0?fl:br); break; }
        case 10: { int br=(int32_t)(int16_t)rf, t=i+br, fl=-1; for(int li=0;li<lc;li++) if(lo[li]==(uint32_t)t){fl=li;break;} sf(s," v%u, v%u, ",R(vA),R(vB)); sf(s,fl>=0?":cond_%d":"+%d",fl>=0?fl:br); break; }
        case 11: case 20: sf(s, " v%u, #%d", R(vA), (int)(int16_t)rf); break;
        case 12: sf(s, " v%u, v%u, #%d", R(vA), R(vB), lt); break;
        case 13: sf(s, " v%u, v%u, v%u", R(vA), R(vB), R(vC)); break;
        case 14: {
            const char *r=res(ctx,k,rf); sa(s," {");
            uint32_t regs[5] = {vB & 0xF, (vB>>4)&0xF, (uint32_t)lt & 0xF, ((uint32_t)lt>>4)&0xF, vC};
            for(uint32_t ai=0; ai<vA && ai<5; ai++) {
                if(ai) sa(s,", "); sf(s,"v%u",R(regs[ai]));
            }
            sf(s, "}, %s", r); break;
        }
        case 15: sf(s, " {v%u..v%u}, %s", R(vC), R(vC+vA-1), res(ctx,k,rf)); break;
        case 16: sf(s, " v%u, #%d", R(vA), lt); break;
        case 17: { uint64_t v=0; for(int wi=0;wi<4;wi++) v|=(uint64_t)insns[i+1+wi]<<(wi*16); sf(s," v%u, #%lld",R(vA),(long long)v); break; }
        case 18: sf(s, " v%u, +%x", R(vA), rf); break;
        default: sf(s, " v%u", R(vA)); break;
        }
        sa(s, "\n"); i += w;
    }
    (void)tries;
    return sa(s, ".end method\n");
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
    const char *src = c->source_file_idx != 0xFFFFFFFF ? ctx->sp.strings[c->source_file_idx] : NULL;
    char *c2 = tsm(cn), *s2 = sp ? tsm(sp) : NULL;
    size_t cap = 4096, len = 0; char *buf = malloc(cap);
    if (!buf) { free(c2); free(s2); return NULL; }
    buf[0] = 0;
    sb sb = {&buf, &len, &cap};
    sf(&sb, ".class %s %s\n", aflags(c->access_flags), c2 ?: cn);
    if (s2) sf(&sb, ".super %s\n", s2);
    if (src) sf(&sb, ".source \"%s\"\n", src);
    if (c->interfaces_off && c->interfaces_off+4 <= ctx->size) {
        uint32_t ic; memcpy(&ic, ctx->data+c->interfaces_off, 4);
        for (uint32_t ii=0; ii<ic; ii++) {
            if (c->interfaces_off+4+ii*4+4 > ctx->size) break;
            uint32_t ti2; memcpy(&ti2, ctx->data+c->interfaces_off+4+ii*4, 4);
            if (ti2 < ctx->type_count) {
                const char *iface = ctx->sp.strings[ctx->type_ids[ti2]];
                char *i2 = tsm(iface);
                sf(&sb, ".implements %s\n", i2 ?: iface);
                free(i2);
            }
        }
    }
    for (int fi = 0; fi < c->static_count; fi++) {
        dex_field_enc *fe = &c->static_fields[fi];
        if (fe->field_idx < ctx->field_count) {
            dex_field *f = &ctx->fields[fe->field_idx];
            char *ft = tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
            sf(&sb, ".field %s %s:%s\n", aflags(fe->access_flags),
                ctx->sp.strings[f->name_idx], ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
            free(ft);
        }
    }
    for (int fi = 0; fi < c->instance_count; fi++) {
        dex_field_enc *fe = &c->instance_fields[fi];
        if (fe->field_idx < ctx->field_count) {
            dex_field *f = &ctx->fields[fe->field_idx];
            char *ft = tsm(ctx->sp.strings[ctx->type_ids[f->type_idx]]);
            sf(&sb, ".field %s %s:%s\n", aflags(fe->access_flags),
                ctx->sp.strings[f->name_idx], ft ?: ctx->sp.strings[ctx->type_ids[f->type_idx]]);
            free(ft);
        }
    }
    for (int mi = 0; mi < c->direct_count; mi++) dm(&sb, ctx, &c->direct[mi]);
    for (int mi = 0; mi < c->virtual_count; mi++) dm(&sb, ctx, &c->virtual[mi]);
    free(c2); free(s2);
    return buf;
}
