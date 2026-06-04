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
    static char buf[128]; int pos = 0;
    buf[0] = 0;
    if (f & 0x0001) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"public"); }
    if (f & 0x0002) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"private"); }
    if (f & 0x0004) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"protected"); }
    if (f & 0x0008) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"static"); }
    if (f & 0x0010) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"final"); }
    if (f & 0x0020) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"synchronized"); }
    if (f & 0x0040) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"bridge"); }
    if (f & 0x0080) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"varargs"); }
    if (f & 0x0100) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"native"); }
    if (f & 0x0200) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"interface"); }
    if (f & 0x0400) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"abstract"); }
    if (f & 0x0800) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"strictfp"); }
    if (f & 0x1000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"synthetic"); }
    if (f & 0x2000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"annotation"); }
    if (f & 0x4000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"enum"); }
    if (f & 0x8000) { if (pos) buf[pos++]=' '; /* constructor */ }
    return buf;
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
            char par[1024] = "";
            if (p->params_off && p->params_off+4 <= ctx->size) {
                uint32_t po = p->params_off, pc;
                memcpy(&pc, ctx->data+po, 4); po += 4;
                if (po + pc*2 <= ctx->size) for (uint32_t pi=0; pi<pc && pi<40; pi++) {
                    uint16_t ti; memcpy(&ti, ctx->data+po+pi*2, 2);
                    const char *pt = res(ctx,2,ti);
                    if (strlen(par)<240) strncat(par, pt?:res(ctx,2,ti), sizeof(par)-strlen(par)-1);
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
    char pbuf[1024] = "";
    if (p->params_off && p->params_off+4 <= ctx->size) {
        uint32_t po=p->params_off, pc;
        memcpy(&pc, ctx->data+po, 4); po += 4;
        if (po+pc*2 <= ctx->size) for (uint32_t i=0; i<pc && i<40; i++) {
            uint16_t ti; memcpy(&ti, ctx->data+po+i*2, 2);
            const char *pt = res(ctx,2,ti);
            strncat(pbuf, pt ?: "?", sizeof(pbuf)-strlen(pbuf)-1);
        }
    }
    snprintf(b,z,"(%s)%s", pbuf, rt ?: "V");
}

static const char *reg_name(uint32_t r, uint32_t regs, uint32_t ins_size) {
    static char buf[4][16];
    static int idx = 0;
    int i = idx;
    idx = (idx + 1) & 3;
    uint32_t locals = regs - ins_size;
    if (r >= locals)
        snprintf(buf[i], sizeof(buf[i]), "p%u", r - locals);
    else
        snprintf(buf[i], sizeof(buf[i]), "v%u", r);
    return buf[i];
}

/* ---- annotation parsing ---- */

static int uleb(const uint8_t *data, uint32_t *val, const uint8_t **next) {
    *val = 0; int shift = 0, i = 0;
    while (1) {
        *val |= (uint32_t)(data[i] & 0x7F) << shift;
        if (!(data[i++] & 0x80)) break;
        shift += 7;
    }
    if (next) *next = data + i;
    return i;
}

static int write_encoded_annotation(sb *s, dex_ctx *ctx, const uint8_t *data, const uint8_t *end,
                                     int visibility) {
    const uint8_t *p = data;
    uint32_t type_idx; uleb(p, &type_idx, &p);
    uint32_t elem_count; uleb(p, &elem_count, &p);
    const char *vis_str = visibility == 1 ? "runtime" : visibility == 2 ? "system" : "build";
    
    sf(s, ".annotation %s %s\n", vis_str, res(ctx, 2, type_idx));
    for (uint32_t ei = 0; ei < elem_count; ei++) {
        uint32_t name_idx; uleb(p, &name_idx, &p);
        if (p >= end) break;
        sf(s, "        %s = ", res(ctx, 1, name_idx));
        
        uint8_t head = *p;
        int vtype = head & 0x1F;
        int varg = (head >> 5) & 0x07;
        const uint8_t *vp = p + 1;
        int width = varg + 1;
        p = vp + width;
        
        switch (vtype) {
        case 0x17: { /* STRING */
            uint32_t idx = 0;
            for (int i = 0; i < width; i++) idx |= (uint32_t)vp[i] << (i * 8);
            sf(s, "\"%s\"\n", res(ctx, 1, idx));
            break;
        }
        case 0x18: { /* TYPE */
            uint32_t idx = 0;
            for (int i = 0; i < width; i++) idx |= (uint32_t)vp[i] << (i * 8);
            sf(s, "%s\n", res(ctx, 2, idx));
            break;
        }
        case 0x19: { /* FIELD */
            uint32_t idx = 0;
            for (int i = 0; i < width; i++) idx |= (uint32_t)vp[i] << (i * 8);
            sf(s, ".enum %s\n", res(ctx, 3, idx));
            break;
        }
        case 0x1b: { /* ENUM */
            uint32_t idx = 0;
            for (int i = 0; i < width; i++) idx |= (uint32_t)vp[i] << (i * 8);
            sf(s, ".enum %s\n", res(ctx, 3, idx));
            break;
        }
        case 0x1a: { /* METHOD */
            uint32_t idx = 0;
            for (int i = 0; i < width; i++) idx |= (uint32_t)vp[i] << (i * 8);
            sf(s, "%s\n", res(ctx, 4, idx));
            break;
        }
        case 0x04: { /* INT */
            int32_t v = 0;
            for (int i = 0; i < width && i < 4; i++) v |= (int32_t)vp[i] << (i * 8);
            if (width == 1) v = (int32_t)(int8_t)vp[0];
            else if (width == 2) v = (int32_t)(int16_t)(vp[0] | (vp[1] << 8));
            sf(s, "0x%x\n", v);
            break;
        }
        case 0x00: { /* BYTE */
            sf(s, "0x%xt\n", (int)(int8_t)vp[0]);
            break;
        }
        case 0x02: { /* SHORT */
            uint16_t v = vp[0] | (vp[1] << 8);
            sf(s, "0x%xs\n", (int)(int16_t)v);
            break;
        }
        case 0x1e: /* NULL */
            sa(s, "null\n");
            break;
        case 0x1f: /* BOOLEAN */
            sf(s, "%s\n", varg ? "true" : "false");
            break;
        case 0x1c: { /* ARRAY */
            uint32_t arr_size;
            if (varg > 0) {
                arr_size = 0;
                for (int i = 0; i < width && i < 4; i++) arr_size |= (uint32_t)vp[i] << (i * 8);
            } else arr_size = 0;
            sa(s, "{\n");
            const uint8_t *ap = p;
            for (uint32_t ai = 0; ai < arr_size; ai++) {
                if (ap >= end) break;
                uint8_t ah = *ap; int aw = ((ah >> 5) & 7) + 1;
                /* FIXME: array element values are not parsed inline */
                sf(s, "            0x0\n");
                ap += 1 + aw;
            }
            sa(s, "        }\n");
            p = ap;
            break;
        }
        case 0x1d: { /* nested ANNOTATION */
            uint32_t sub_type_idx; uleb(p, &sub_type_idx, &p);
            uint32_t sub_elems; uleb(p, &sub_elems, &p);
            sf(s, ".%s(", res(ctx, 2, sub_type_idx));
            for (uint32_t si = 0; si < sub_elems; si++) {
                if (si) sa(s, ", ");
                uint32_t sname_idx; uleb(p, &sname_idx, &p);
                sf(s, "%s=", res(ctx, 1, sname_idx));
                uint8_t sh = *p; int sw = ((sh >> 5) & 7) + 1;
                if (sh == 0x04) { /* INT */
                    int32_t sv = 0;
                    for (int i = 0; i < sw && i < 4; i++) sv |= (int32_t)p[i+1] << (i*8);
                    if (sw == 1) sv = (int32_t)(int8_t)p[1];
                    sf(s, "0x%x", sv);
                } else {
                    sf(s, "0x%x", (int)sh);
                }
                p += 1 + sw;
            }
            sa(s, ")\n");
            break;
        }
        default: {
            /* unknown value type, skip it */
            sf(s, "<0x%02x>\n", vtype);
            break;
        }
        }
    }
    sa(s, "    .end annotation\n");
    return 0;
}

static int write_annotation_set_ref(sb *s, dex_ctx *ctx, uint32_t offset) {
    if (offset == 0 || offset + 4 > ctx->size) return 0;
    uint32_t size; memcpy(&size, ctx->data + offset, 4);
    if (offset + 4 + size * 4 > ctx->size) return 0;
    for (uint32_t i = 0; i < size; i++) {
        uint32_t ann_item_off;
        memcpy(&ann_item_off, ctx->data + offset + 4 + i * 4, 4);
        if (ann_item_off == 0 || ann_item_off + 1 >= ctx->size) continue;
        int visibility = (int)ctx->data[ann_item_off];
        write_encoded_annotation(s, ctx, ctx->data + ann_item_off + 1, ctx->data + ctx->size, visibility);
    }
    return 0;
}

static void write_annotations(sb *s, dex_ctx *ctx, dex_class *c,
                               int target_type, uint32_t target_idx) {
    if (c->annotations_off == 0 || c->annotations_off + 16 > ctx->size) return;
    const uint8_t *p = ctx->data + c->annotations_off;
    uint32_t class_annot_off, fields_sz, methods_sz, params_sz;
    memcpy(&class_annot_off, p, 4); p += 4;
    memcpy(&fields_sz, p, 4); p += 4;
    memcpy(&methods_sz, p, 4); p += 4;
    memcpy(&params_sz, p, 4); p += 4;

    /* class-level annotations */
    if (target_type == 0 && class_annot_off != 0) {
        sa(s, "\n\n# annotations\n");
        write_annotation_set_ref(s, ctx, class_annot_off);
    }

    /* field annotations */
    for (uint32_t i = 0; i < fields_sz; i++) {
        if ((const uint8_t *)(p + 8) > ctx->data + ctx->size) break;
        uint32_t fidx, fannot_off;
        memcpy(&fidx, p, 4); p += 4;
        memcpy(&fannot_off, p, 4); p += 4;
        if (target_type == 1 && fidx == target_idx && fannot_off != 0) {
            sa(s, "    .annotation build unknown\n");
            write_annotation_set_ref(s, ctx, fannot_off);
            sa(s, "    .end annotation\n");
        }
    }

    /* method annotations */
    for (uint32_t i = 0; i < methods_sz; i++) {
        if ((const uint8_t *)(p + 8) > ctx->data + ctx->size) break;
        uint32_t midx, mannot_off;
        memcpy(&midx, p, 4); p += 4;
        memcpy(&mannot_off, p, 4); p += 4;
        if (target_type == 2 && midx == target_idx && mannot_off != 0) {
            sa(s, "\n    .annotation build unknown\n");
            write_annotation_set_ref(s, ctx, mannot_off);
            sa(s, "    .end annotation\n");
        }
    }
    (void)params_sz; // TODO: parameter annotations
}

static int dm(sb *s, dex_ctx *ctx, dex_method_enc *me) {
    if (me->method_idx >= ctx->method_count || me->code_off+16 > ctx->size)
        return sf(s, ".method unknown\n.end method\n");
    dex_method_id *mi = &ctx->methods[me->method_idx];
    const char *mn = mi->name_idx < (uint32_t)ctx->sp.count ? ctx->sp.strings[mi->name_idx] : "??";
    uint32_t fl = me->access_flags;
    char protobuf[512];
    mproto(ctx, me->method_idx, protobuf, sizeof(protobuf));

    if (me->code_off == 0) {
        sf(s, ".method %s %s%s\n.end method\n", aflags(fl), mn, protobuf);
        return 0;
    }

    const uint8_t *p = ctx->data + me->code_off;
    uint16_t regs, ins, tries; uint32_t isz;
    memcpy(&regs, p, 2); memcpy(&ins, p+2, 2);
    memcpy(&tries, p+6, 2); memcpy(&isz, p+12, 4);
    const uint16_t *insns = (const uint16_t *)(p + 16);

    /* method signature */
    const char *aflg = aflags(fl);
    if (strcmp(mn, "<clinit>") == 0 || strcmp(mn, "<init>") == 0) {
        if (aflg[0])
            sf(s, ".method %s constructor %s%s\n", aflg, mn, protobuf);
        else
            sf(s, ".method constructor %s%s\n", mn, protobuf);
    } else {
        if (aflg[0])
            sf(s, ".method %s %s%s\n", aflg, mn, protobuf);
        else
            sf(s, ".method %s%s\n", mn, protobuf);
    }
    sf(s, "    .registers %u\n\n", (unsigned)regs);

    /* collect label targets (use offset-based labels) */
    #define NL 256
    uint32_t lo[NL]; int lc = 0;
    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) { i += 1; continue; }
        int fm = dex_insn_fmt((uint8_t)op);
        int bo = (fm==4||fm==5||fm==6) ? lt :
                 ((fm==9||fm==10) ? (int32_t)(int16_t)rf :
                 (fm==18 ? (int32_t)rf : 0));
        if (bo) { int t=i+bo; if (t>=0 && (uint32_t)t<isz && lc<NL) {
            int dup=0; for(int di=0;di<lc;di++) if(lo[di]==(uint32_t)t){dup=1;break;}
            if(!dup) lo[lc++] = (uint32_t)t;
        }}
        i += w;
    }

    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) { i += 1; continue; }
        const char *nm = dex_insn_name((uint8_t)op);
        if (!nm) { i+=w; continue; }
        int k = dex_insn_kind((uint8_t)op), fm = dex_insn_fmt((uint8_t)op);

        /* label if target of a branch */
        for (int li = 0; li < lc; li++) if (lo[li] == (uint32_t)i) {
            int bo = 0;
            /* find which branch instruction targets this offset */
            for (int bi = 0; (uint32_t)bi < isz; ) {
                uint32_t bop, bvA, bvB, bvC, brf; int32_t blt;
                int bw = dex_decode_insn(insns, bi, &bop, &bvA, &bvB, &bvC, &brf, &blt);
                if (bw <= 0) { bi++; continue; }
                int bfm = dex_insn_fmt((uint8_t)bop);
                int bbo = (bfm==4||bfm==5||bfm==6) ? blt :
                          ((bfm==9||bfm==10) ? (int32_t)(int16_t)brf :
                          (bfm==18 ? (int32_t)brf : 0));
                if (bbo && bi + bbo == (int)i) {
                    const char *bnm = dex_insn_name((uint8_t)bop);
                    int bkind = dex_insn_kind((uint8_t)bop);
                    (void)bkind;
                    if (bnm && strncmp(bnm, "goto", 4) == 0)
                        sf(s, "    :goto_%x\n", i);
                    else
                        sf(s, "    :cond_%x\n", i);
                    bo = 1; break;
                }
                bi += bw;
            }
            if (!bo) sf(s, "    :cond_%x\n", i);
        }

        sa(s, "    "); sa(s, nm);

        #define RN(r) reg_name(r, (uint32_t)regs, (uint32_t)ins)
        switch (fm) {
        case 0: break;
        case 1: case 21: sf(s, " %s, %s", RN(vA), RN(vB)); break;
        case 2: sf(s, " %s, 0x%x", RN(vA), lt); break;
        case 3: sf(s, " %s", RN(vA)); break;
        case 4: case 5: case 6: {
            int t=i+lt;
            sf(s, " :goto_%x", t);
            break;
        }
        case 7: case 19:
            if (k==1) sf(s, " %s, \"%s\"", RN(vA), res(ctx,k,rf));
            else if (k>=2) { const char *r = res(ctx,k,rf); if (k==2) {char*tt=tsm(r);sf(s," %s, %s",RN(vA),tt?:r);free(tt);} else sf(s," %s, %s",RN(vA),r); }
            else sf(s, " %s, 0x%x", RN(vA), rf);
            break;
        case 8:
            if (k>=2) { const char *r=res(ctx,k,rf); if(k==2){char*tt=tsm(r);sf(s," %s, %s, %s",RN(vA),RN(vB),tt?:r);free(tt);} else sf(s," %s, %s, %s",RN(vA),RN(vB),r); }
            else sf(s, " %s, %s, 0x%x", RN(vA), RN(vB), rf);
            break;
        case 9: {
            int br=(int32_t)(int16_t)rf, t=i+br;
            sf(s," %s, :cond_%x", RN(vA), t);
            break;
        }
        case 10: {
            int br=(int32_t)(int16_t)rf, t=i+br;
            sf(s," %s, %s, :cond_%x", RN(vA), RN(vB), t);
            break;
        }
        case 11: sf(s, " %s, 0x%x", RN(vA), (int16_t)rf); break;
        case 20: sf(s, " %s, %s, 0x%x", RN(vA), RN(vB), (int16_t)rf); break;
        case 24: sf(s, " %s, %s", RN(vA), RN(vB)); break;
        case 25: sf(s, " %s, %s", RN(vA), RN(vB)); break;
        case 12: sf(s, " %s, %s, 0x%x", RN(vA), RN(vB), lt); break;
        case 13: sf(s, " %s, %s, %s", RN(vA), RN(vB), RN(vC)); break;
        case 14: {
            const char *r=res(ctx,k,rf); sa(s," {");
            uint32_t regs5[5] = {vB & 0xF, (vB>>4)&0xF, (uint32_t)lt & 0xF, ((uint32_t)lt>>4)&0xF, vC};
            for(uint32_t ai=0; ai<vA && ai<5; ai++) {
                if(ai) sa(s,", ");
                sf(s,"%s", RN(regs5[ai]));
            }
            sf(s, "}, %s", r); break;
        }
        case 15: sf(s, " {%s .. %s}, %s", RN(vC), RN(vC+vA-1), res(ctx,k,rf)); break;
        case 16: sf(s, " %s, 0x%x", RN(vA), lt); break;
        case 17: { uint64_t v=0; for(int wi=0;wi<4;wi++) v|=(uint64_t)insns[i+1+wi]<<(wi*16); sf(s," %s, 0x%llx",RN(vA),(unsigned long long)v); break; }
        case 18: sf(s, " %s, +%x", RN(vA), rf); break;
        default: sf(s, " %s", RN(vA)); break;
        }
        /* check if this is the last instruction */
        int is_last = ((uint32_t)(i + w) >= isz) ? 1 : 0;
        if (is_last)
            sa(s, "\n");
        else
            sa(s, "\n\n");
        i += w;
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
    const char *src = (c->source_file_idx != 0xFFFFFFFF && c->source_file_idx < (uint32_t)ctx->sp.count) ? ctx->sp.strings[c->source_file_idx] : NULL;
    char *c2 = tsm(cn), *s2 = sp ? tsm(sp) : NULL;
    size_t cap = 65536, len = 0; char *buf = malloc(cap);
    if (!buf) { free(c2); free(s2); return NULL; }
    buf[0] = 0;
    sb sb = {&buf, &len, &cap};
    const char *af = aflags(c->access_flags);
    if (af[0])
        sf(&sb, ".class %s %s\n", af, c2 ?: cn);
    else
        sf(&sb, ".class %s\n", c2 ?: cn);
    if (s2) sf(&sb, ".super %s\n", s2);
    if (src) sf(&sb, ".source \"%s\"\n", src);

    /* interfaces */
    if (c->interfaces_off && c->interfaces_off+4 <= ctx->size) {
        uint32_t ic; memcpy(&ic, ctx->data+c->interfaces_off, 4);
        if (ic > 0) {
            sa(&sb, "\n# interfaces\n");
            for (uint32_t ii=0; ii<ic; ii++) {
                if (c->interfaces_off+4+ii*2+2 > ctx->size) break;
                uint16_t ti2; memcpy(&ti2, ctx->data+c->interfaces_off+4+ii*2, 2);
                if (ti2 < ctx->type_count) {
                    const char *iface = ctx->sp.strings[ctx->type_ids[ti2]];
                    char *i2 = tsm(iface);
                    sf(&sb, ".implements %s\n", i2 ?: iface);
                    free(i2);
                }
            }
        }
    }

    /* class-level annotations (before fields) */
    write_annotations(&sb, ctx, c, 0, 0);

    /* static fields */
    if (c->static_count > 0) {
        sa(&sb, "\n\n# static fields\n");
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
    }

    /* instance fields */
    if (c->instance_count > 0) {
        sa(&sb, "\n\n# instance fields\n");
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
    }

    /* direct methods */
    if (c->direct_count > 0) {
        sa(&sb, "\n\n# direct methods\n");
        for (int mi = 0; mi < c->direct_count; mi++) {
            dm(&sb, ctx, &c->direct[mi]);
            if (mi < c->direct_count - 1) sa(&sb, "\n");
        }
    }

    /* virtual methods */
    if (c->virtual_count > 0) {
        sa(&sb, "\n\n# virtual methods\n");
        for (int mi = 0; mi < c->virtual_count; mi++) {
            dm(&sb, ctx, &c->virtual[mi]);
            if (mi < c->virtual_count - 1) sa(&sb, "\n");
        }
    }

    free(c2); free(s2);
    return buf;
}
