#include "smali_flow.h"
#include "smali_optab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKS 128
#define MAX_EDGES 384
#define OUT_SZ 65536

typedef struct { char *name; uint32_t idx; } lblmap_t;
typedef struct { uint32_t start, end; } block_t;

static const char *op_name(uint8_t op) {
    for (size_t i = 0; i < sizeof(smali_optab)/sizeof(smali_optab[0]); i++)
        if (smali_optab[i].op == op) return smali_optab[i].name;
    return NULL;
}

static int is_term(const smali_insn_t *ins) {
    if (ins->fmt >= 4 && ins->fmt <= 6) return 1;
    if (ins->fmt == 9 || ins->fmt == 10) return 1;
    if (ins->op >= 0x0E && ins->op <= 0x11) return 1;
    if (ins->op == 0x27) return 1;
    if (ins->op == 0x2B || ins->op == 0x2C) return 1;
    return 0;
}

static int is_code(const smali_insn_t *ins) {
    return ins->fmt <= 26;
}

static void reg_name(char *b, size_t sz, uint32_t r) {
    if (r & 0x80000000)
        snprintf(b, sz, "p%d", r & ~0x80000000);
    else
        snprintf(b, sz, "v%d", r);
}

static void fmt_insn(const smali_insn_t *ins, char *b, size_t sz) {
    const char *nm = op_name(ins->op);
    if (!nm) { snprintf(b, sz, "???"); return; }
    char r0[16], r1[16], r2[16];
    reg_name(r0, sizeof(r0), ins->vA);
    reg_name(r1, sizeof(r1), ins->vB);
    reg_name(r2, sizeof(r2), ins->vC);
    switch (ins->fmt) {
        case 0: snprintf(b, sz, "%s", nm); break;
        case 1: case 23: case 24:
            snprintf(b, sz, "%s %s, %s", nm, r0, r1); break;
        case 2: snprintf(b, sz, "%s %s, #%d", nm, r0, ins->lit); break;
        case 3: snprintf(b, sz, "%s %s", nm, r0); break;
        case 4: case 5: case 6:
            snprintf(b, sz, "%s %s", nm, ins->label_target ? ins->label_target : ""); break;
        case 7: case 25:
            snprintf(b, sz, "%s %s, %s", nm, r0, ins->ref_str ? ins->ref_str : ""); break;
        case 8:
            snprintf(b, sz, "%s %s, %s, %s", nm, r0, r1, ins->ref_str ? ins->ref_str : ""); break;
        case 9:
            snprintf(b, sz, "%s %s, %s", nm, r0, ins->label_target ? ins->label_target : ""); break;
        case 10:
            snprintf(b, sz, "%s %s, %s, %s", nm, r0, r1, ins->label_target ? ins->label_target : ""); break;
        case 11: case 16: case 19:
            snprintf(b, sz, "%s %s, #%d", nm, r0, ins->lit); break;
        case 12:
            snprintf(b, sz, "%s %s, %s, #%d", nm, r0, r1, ins->lit); break;
        case 13:
            snprintf(b, sz, "%s %s, %s, %s", nm, r0, r1, r2); break;
        case 14: {
            char rl[64]; int pos = 0; pos += snprintf(rl+pos, sizeof(rl)-pos, "{");
            for (int i = 0; i < 5 && (i == 0 || ins->regs[i] != 0); i++) {
                if (i > 0) pos += snprintf(rl+pos, sizeof(rl)-pos, ", ");
                char rd[16]; reg_name(rd, sizeof(rd), ins->regs[i]);
                pos += snprintf(rl+pos, sizeof(rl)-pos, "%s", rd);
            }
            snprintf(rl+pos, sizeof(rl)-pos, "}");
            snprintf(b, sz, "%s %s, %s", nm, rl, ins->ref_str ? ins->ref_str : "");
            break;
        }
        case 15:
            snprintf(b, sz, "%s {%s .. v%d}, %s", nm, r2, ins->vC + ins->vA - 1, ins->ref_str ? ins->ref_str : "");
            break;
        case 18:
            snprintf(b, sz, "%s %s, %s", nm, r0, ins->label_target ? ins->label_target : "");
            break;
        case 22:
            snprintf(b, sz, "%s %s, %s, #%d", nm, r0, r1, ins->lit); break;
        case 26:
            snprintf(b, sz, "%s %s, #%lld", nm, r0, (long long)(int64_t)ins->lit); break;
        default: snprintf(b, sz, "%s", nm); break;
    }
}

static uint32_t find_block(block_t *blocks, uint32_t n, uint32_t insn_idx) {
    for (uint32_t i = 0; i < n; i++)
        if (insn_idx >= blocks[i].start && insn_idx <= blocks[i].end) return i;
    return n;
}

char* smali_flow_generate(smali_method_def_t *m, const char *method_name) {
    (void)method_name;
    uint32_t ni = m->insns_count;
    if (ni == 0) return strdup("");

    // Label → instruction index map
    lblmap_t *lbl = malloc(m->labels_count * sizeof(lblmap_t));
    uint32_t nl = 0;
    for (uint32_t i = 0; i < m->labels_count; i++)
        if (m->labels[i].offset < ni) {
            lbl[nl].name = m->labels[i].name;
            lbl[nl].idx = m->labels[i].offset;
            nl++;
        }

    uint8_t *leader = calloc(ni, 1);
    leader[0] = 1;
    for (uint32_t i = 0; i < nl; i++)
        if (is_code(&m->insns[lbl[i].idx]))
            leader[lbl[i].idx] = 1;
    for (uint32_t i = 0; i < ni; i++)
        if (is_term(&m->insns[i]) && i + 1 < ni && is_code(&m->insns[i+1]))
            leader[i+1] = 1;

    block_t blocks[MAX_BLOCKS];
    uint32_t bc = 0;
    for (uint32_t i = 0; i < ni; ) {
        if (!is_code(&m->insns[i]) || !leader[i]) { i++; continue; }
        blocks[bc].start = i;
        uint32_t j = i + 1;
        while (j < ni && (!leader[j] || !is_code(&m->insns[j]))) {
            if (is_code(&m->insns[j])) j++;
            else j++;
        }
        blocks[bc].end = j - 1;
        bc++;
        i = j;
        if (bc >= MAX_BLOCKS) break;
    }

    // Build edges
    typedef struct { uint32_t to; const char *lbl; int has_lbl; } edge_t;
    edge_t edges[MAX_BLOCKS][8];
    uint32_t ec[MAX_BLOCKS];
    memset(ec, 0, sizeof(ec));

    for (uint32_t bi = 0; bi < bc; bi++) {
        const smali_insn_t *last = &m->insns[blocks[bi].end];
        int last_is_term = is_term(last);

        if (last_is_term) {
            if (last->fmt >= 4 && last->fmt <= 6 && last->label_target) {
                // Unconditional branch
                for (uint32_t j = 0; j < nl; j++)
                    if (strcmp(lbl[j].name, last->label_target) == 0) {
                        uint32_t tb = find_block(blocks, bc, lbl[j].idx);
                        if (tb < bc) { edges[bi][ec[bi]].to = tb; edges[bi][ec[bi]].has_lbl = 0; ec[bi]++; }
                        break;
                    }
            } else if (last->fmt == 9 || last->fmt == 10) {
                // Conditional branch: edge to target + fallthrough to next block
                if (bi + 1 < bc) { edges[bi][ec[bi]].to = bi + 1; edges[bi][ec[bi]].has_lbl = 1; edges[bi][ec[bi]].lbl = "fallthrough"; ec[bi]++; }
                for (uint32_t j = 0; j < nl; j++)
                    if (strcmp(lbl[j].name, last->label_target) == 0) {
                        uint32_t tb = find_block(blocks, bc, lbl[j].idx);
                        if (tb < bc) { edges[bi][ec[bi]].to = tb; edges[bi][ec[bi]].has_lbl = 1; edges[bi][ec[bi]].lbl = op_name(last->op); ec[bi]++; }
                        break;
                    }
            } else if (last->op == 0x2B || last->op == 0x2C) {
                // Switch: look up payload targets via label
                for (uint32_t j = 0; j < nl; j++)
                    if (last->label_target && strcmp(lbl[j].name, last->label_target) == 0) {
                        uint32_t p_insn = lbl[j].idx;
                        if (p_insn < ni && m->insns[p_insn].fmt >= 101 && m->insns[p_insn].fmt <= 102) {
                            for (uint32_t t = 0; t < m->insns[p_insn].payload_targets_count; t++) {
                                for (uint32_t k = 0; k < nl; k++)
                                    if (strcmp(lbl[k].name, m->insns[p_insn].payload_targets[t]) == 0) {
                                        uint32_t tb = find_block(blocks, bc, lbl[k].idx);
                                        if (tb < bc) {
                                            int dup = 0;
                                            for (uint32_t e = 0; e < ec[bi]; e++)
                                                if (edges[bi][e].to == tb) { dup = 1; break; }
                                            if (!dup) { edges[bi][ec[bi]].to = tb; edges[bi][ec[bi]].has_lbl = 1; edges[bi][ec[bi]].lbl = "switch"; ec[bi]++; }
                                        }
                                        break;
                                    }
                            }
                        }
                        break;
                    }
            }
            // return/throw: no outgoing edges
        } else {
            // Fallthrough to next block
            if (bi + 1 < bc) { edges[bi][ec[bi]].to = bi + 1; edges[bi][ec[bi]].has_lbl = 0; ec[bi]++; }
        }
    }

    // Try/catch edges
    for (uint32_t ci = 0; ci < m->catches_count; ci++) {
        smali_catch_t *ctch = &m->catches[ci];
        // Find handler block
        uint32_t hb = MAX_BLOCKS;
        for (uint32_t j = 0; j < nl; j++)
            if (ctch->handler_label && strcmp(lbl[j].name, ctch->handler_label) == 0) {
                hb = find_block(blocks, bc, lbl[j].idx);
                break;
            }
        if (hb >= bc) continue;
        // Find start/end instruction indices
        uint32_t s_insn = ni, e_insn = 0;
        for (uint32_t j = 0; j < nl; j++) {
            if (ctch->start_label && strcmp(lbl[j].name, ctch->start_label) == 0) s_insn = lbl[j].idx;
            if (ctch->end_label && strcmp(lbl[j].name, ctch->end_label) == 0) e_insn = lbl[j].idx;
        }
        if (e_insn <= s_insn || e_insn >= ni) continue;
        // Add edge from every block within the try range
        for (uint32_t b = 0; b < bc; b++) {
            if (blocks[b].start >= s_insn && blocks[b].start < e_insn) {
                int dup = 0;
                for (uint32_t e = 0; e < ec[b]; e++)
                    if (edges[b][e].to == hb) { dup = 1; break; }
                if (!dup && ec[b] < 8) {
                    edges[b][ec[b]].to = hb;
                    edges[b][ec[b]].has_lbl = 1;
                    edges[b][ec[b]].lbl = ctch->type ? ctch->type : "catchall";
                    ec[b]++;
                }
            }
        }
    }

    // Generate Mermaid output
    char *out = malloc(OUT_SZ);
    int pos = 0;
    pos += snprintf(out+pos, OUT_SZ-pos, "flowchart TD\n");

    for (uint32_t bi = 0; bi < bc; bi++) {
        pos += snprintf(out+pos, OUT_SZ-pos, "    B%d[", bi);
        int first = 1;
        for (uint32_t ii = blocks[bi].start; ii <= blocks[bi].end; ii++) {
            if (!is_code(&m->insns[ii])) continue;
            if (!first) pos += snprintf(out+pos, OUT_SZ-pos, "<br/>");
            first = 0;
            // Check if this instruction has a label
            for (uint32_t j = 0; j < nl; j++)
                if (lbl[j].idx == ii) {
                    pos += snprintf(out+pos, OUT_SZ-pos, "%s ", lbl[j].name);
                    break;
                }
            char ibuf[128];
            fmt_insn(&m->insns[ii], ibuf, sizeof(ibuf));
            // Escape for Mermaid: replace " with '
            for (char *p = ibuf; *p; p++) if (*p == '"') *p = '\'';
            pos += snprintf(out+pos, OUT_SZ-pos, "%s", ibuf);
            if (pos >= OUT_SZ - 100) break;
        }
        pos += snprintf(out+pos, OUT_SZ-pos, "]\n");

        for (uint32_t e = 0; e < ec[bi]; e++) {
            if (edges[bi][e].has_lbl)
                pos += snprintf(out+pos, OUT_SZ-pos, "    B%d -- \"%s\" --> B%d\n", bi, edges[bi][e].lbl, edges[bi][e].to);
            else
                pos += snprintf(out+pos, OUT_SZ-pos, "    B%d --> B%d\n", bi, edges[bi][e].to);
            if (pos >= OUT_SZ - 100) break;
        }
    }

    free(leader);
    free(lbl);
    return out;
}
