#include "format_dex_internal.h"
#include <stdlib.h>
#include <string.h>

extern int dex_decode_insn(const uint16_t *insns, int offset,
    uint32_t *out_op, uint32_t *out_vA, uint32_t *out_vB, uint32_t *out_vC,
    uint32_t *out_ref, int32_t *out_lit);
extern const char *dex_insn_name(uint8_t op);
extern int dex_insn_kind(uint8_t op);
extern int dex_insn_fmt(uint8_t op);

#define NL 256

int smali_dm(smali_sb *s, dex_ctx *ctx, dex_method_enc *me) {
    if (me->method_idx >= ctx->method_count)
        return smali_sf(s, ".method unknown\n.end method\n");

    dex_method_id *mi = &ctx->methods[me->method_idx];
    uint32_t fl = me->access_flags;
    const char *mn = mi->name_idx < (uint32_t)ctx->sp.count
                     ? ctx->sp.strings[mi->name_idx] : "??";
    char protobuf[512];
    smali_mproto(ctx, me->method_idx, protobuf, sizeof(protobuf));

    if (me->code_off == 0 || me->code_off + 16 > ctx->size) {
        smali_sf(s, ".method %s %s%s\n.end method\n",
                 smali_aflags(fl), mn, protobuf);
        return 0;
    }

    const uint8_t *p = ctx->data + me->code_off;
    uint16_t regs, ins, tries; uint32_t isz;
    memcpy(&regs, p, 2); memcpy(&ins, p+2, 2);
    memcpy(&tries, p+6, 2); memcpy(&isz, p+12, 4);
    const uint16_t *insns = (const uint16_t *)(p + 16);

    /* method signature */
    const char *aflg = smali_aflags(fl);
    if (strcmp(mn, "<clinit>") == 0 || strcmp(mn, "<init>") == 0) {
        if (aflg[0])
            smali_sf(s, ".method %s constructor %s%s\n", aflg, mn, protobuf);
        else
            smali_sf(s, ".method constructor %s%s\n", mn, protobuf);
    } else {
        if (aflg[0])
            smali_sf(s, ".method %s %s%s\n", aflg, mn, protobuf);
        else
            smali_sf(s, ".method %s%s\n", mn, protobuf);
    }
    smali_sf(s, "    .registers %u\n\n", (unsigned)regs);

    /* pass 1: collect branch targets */
    uint32_t lo[NL]; int lc = 0;
    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) { i += 1; continue; }
        int fm = dex_insn_fmt((uint8_t)op);
        int bo = (fm==4||fm==5||fm==6) ? lt :
                 ((fm==9||fm==10) ? (int32_t)(int16_t)rf :
                 (fm==18 ? (int32_t)rf : 0));
        if (bo) {
            int t = i + bo;
            if (t >= 0 && (uint32_t)t < isz && lc < NL) {
                int dup = 0;
                for (int di = 0; di < lc; di++)
                    if (lo[di] == (uint32_t)t) { dup = 1; break; }
                if (!dup) lo[lc++] = (uint32_t)t;
            }
        }
        i += w;
    }

    /* pass 2: emit all branch source types for each label target */
    /* store label-kinds per target: bit0=cond bit1=goto */
    uint8_t lkinds[NL];
    for (int li = 0; li < lc; li++) lkinds[li] = 0;
    for (int bi = 0; (uint32_t)bi < isz; ) {
        uint32_t bop, bvA, bvB, bvC, brf; int32_t blt;
        int bw = dex_decode_insn(insns, bi, &bop, &bvA, &bvB, &bvC, &brf, &blt);
        if (bw <= 0) { bi++; continue; }
        int bfm = dex_insn_fmt((uint8_t)bop);
        int bbo = (bfm==4||bfm==5||bfm==6) ? blt :
                  ((bfm==9||bfm==10) ? (int32_t)(int16_t)brf :
                  (bfm==18 ? (int32_t)brf : 0));
        if (bbo) {
            int t = bi + bbo;
            for (int li = 0; li < lc; li++) {
                if ((int)lo[li] == t) {
                    const char *bnm = dex_insn_name((uint8_t)bop);
                    if (bnm && strncmp(bnm, "goto", 4) == 0)
                        lkinds[li] |= 2;
                    else
                        lkinds[li] |= 1;
                }
            }
        }
        bi += bw;
    }

    /* pass 3: emit instructions */
    for (int i = 0; (uint32_t)i < isz; ) {
        uint32_t op, vA, vB, vC, rf; int32_t lt;
        int w = dex_decode_insn(insns, i, &op, &vA, &vB, &vC, &rf, &lt);
        if (w <= 0) { i += 1; continue; }
        const char *nm = dex_insn_name((uint8_t)op);
        if (!nm) { i += w; continue; }
        int k = dex_insn_kind((uint8_t)op), fm = dex_insn_fmt((uint8_t)op);

        /* emit labels at this offset */
        for (int li = 0; li < lc; li++) {
            if (lo[li] == (uint32_t)i) {
                if (lkinds[li] & 1)
                    smali_sf(s, "    :cond_%x\n", i);
                if (lkinds[li] & 2)
                    smali_sf(s, "    :goto_%x\n", i);
            }
        }

        smali_sa(s, "    "); smali_sa(s, nm);

        #define RN(r) smali_reg_name(r, (uint32_t)regs, (uint32_t)ins)
        switch (fm) {
        case 0: break;
        case 1: case 21: smali_sf(s, " %s, %s", RN(vA), RN(vB)); break;
        case 2: smali_sf(s, " %s, 0x%x", RN(vA), lt); break;
        case 3: smali_sf(s, " %s", RN(vA)); break;
        case 4: case 5: case 6: smali_sf(s, " :goto_%x", i+lt); break;
        case 7: case 19:
            if (k==1) smali_sf(s," %s, \"%s\"", RN(vA), smali_res(ctx,k,rf));
            else if (k>=2) { const char *r=smali_res(ctx,k,rf);
                if (k==2){char*tt=smali_tsm(r);smali_sf(s," %s, %s",RN(vA),tt);free(tt);}
                else smali_sf(s," %s, %s",RN(vA),r); }
            else smali_sf(s, " %s, 0x%x", RN(vA), rf);
            break;
        case 8:
            if (k>=2) { const char *r=smali_res(ctx,k,rf);
                if(k==2){char*tt=smali_tsm(r);
                    smali_sf(s," %s, %s, %s",RN(vA),RN(vB),tt);free(tt);}
                else smali_sf(s," %s, %s, %s",RN(vA),RN(vB),r); }
            else smali_sf(s, " %s, %s, 0x%x", RN(vA), RN(vB), rf);
            break;
        case 9: {
            int br=(int32_t)(int16_t)rf;
            smali_sf(s," %s, :cond_%x", RN(vA), i+br); break; }
        case 10: {
            int br=(int32_t)(int16_t)rf;
            smali_sf(s," %s, %s, :cond_%x", RN(vA), RN(vB), i+br); break; }
        case 11: smali_sf(s," %s, 0x%x", RN(vA), (int16_t)rf); break;
        case 20: smali_sf(s," %s, %s, 0x%x", RN(vA), RN(vB),(int16_t)rf); break;
        case 24: smali_sf(s," %s, %s", RN(vA), RN(vB)); break;
        case 25: smali_sf(s," %s, %s", RN(vA), RN(vB)); break;
        case 12: smali_sf(s," %s, %s, 0x%x", RN(vA), RN(vB), lt); break;
        case 13: smali_sf(s," %s, %s, %s", RN(vA), RN(vB), RN(vC)); break;
        case 14: {
            const char *r=smali_res(ctx,k,rf); smali_sa(s," {");
            uint32_t r5[5]={vB&0xF, (vB>>4)&0xF, (uint32_t)lt&0xF,
                            ((uint32_t)lt>>4)&0xF, vC};
            for(uint32_t ai=0; ai<vA && ai<5; ai++){
                if(ai)smali_sa(s,", ");
                smali_sf(s,"%s", RN(r5[ai]));
            }
            smali_sf(s, "}, %s", r); break;
        }
        case 15: smali_sf(s," {%s .. %s}, %s", RN(vC), RN(vC+vA-1),
                          smali_res(ctx,k,rf)); break;
        case 16: smali_sf(s," %s, 0x%x", RN(vA), lt); break;
        case 17: {
            uint64_t v=0; for(int wi=0;wi<4;wi++)
                v|=(uint64_t)insns[i+1+wi]<<(wi*16);
            smali_sf(s," %s, 0x%llx",RN(vA),(unsigned long long)v); break;
        }
        case 18: smali_sf(s," %s, +%x", RN(vA), rf); break;
        default: smali_sf(s," %s", RN(vA)); break;
        }

        int is_last = ((uint32_t)(i + w) >= isz) ? 1 : 0;
        smali_sa(s, is_last ? "\n" : "\n\n");
        i += w;
    }
    (void)tries;
    return smali_sa(s, ".end method\n");
}
