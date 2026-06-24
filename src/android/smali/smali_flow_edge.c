#include "smali_flow_internal.h"
#include "smali_optab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int label_lookup(flow_method_t *fm, const char *name, uint32_t *out_idx) {
    for (uint32_t i = 0; i < fm->label_count; i++)
        if (strcmp(fm->label_names[i], name) == 0) {
            *out_idx = fm->label_idxs[i];
            return 1;
        }
    return 0;
}

uint32_t block_containing(flow_method_t *fm, uint32_t insn_idx) {
    for (uint32_t i = 0; i < fm->block_count; i++)
        if (insn_idx >= fm->blocks[i].start && insn_idx <= fm->blocks[i].end) return i;
    return fm->block_count;
}

void flow_build_blocks(flow_method_t *fm) {
    smali_method_def_t *m = fm->m;
    uint32_t ni = fm->insn_count;
    if (ni == 0) return;

    fm->label_count = 0;
    for (uint32_t i = 0; i < m->labels_count; i++)
        if (m->labels[i].offset < ni) fm->label_count++;
    fm->label_names = (const char **)calloc(fm->label_count, sizeof(const char *));
    fm->label_idxs  = (uint32_t *)calloc(fm->label_count, sizeof(uint32_t));
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->labels_count; i++)
        if (m->labels[i].offset < ni) {
            fm->label_names[k] = m->labels[i].name;
            fm->label_idxs[k]  = m->labels[i].offset;
            k++;
        }

    uint8_t *leader = (uint8_t *)calloc(ni, 1);
    if (ni > 0) leader[0] = 1;
    for (uint32_t i = 0; i < fm->label_count; i++)
        if (flow_insn_is_code(&m->insns[fm->label_idxs[i]]))
            leader[fm->label_idxs[i]] = 1;
    for (uint32_t i = 0; i < ni; i++)
        if (flow_insn_is_term(&m->insns[i]) && i + 1 < ni && flow_insn_is_code(&m->insns[i + 1]))
            leader[i + 1] = 1;
    for (uint32_t ci = 0; ci < m->catches_count; ci++) {
        uint32_t idx;
        if (label_lookup(fm, m->catches[ci].handler_label, &idx) && idx < ni)
            leader[idx] = 1;
    }

    for (uint32_t i = 0; i < ni;) {
        if (!flow_insn_is_code(&m->insns[i]) || !leader[i]) { i++; continue; }
        if (fm->block_count >= FLOW_MAX_BLOCKS) break;
        flow_block_t *b = &fm->blocks[fm->block_count++];
        b->start = i;
        b->end = i;
        b->kind = (fm->block_count == 1) ? FLOW_KIND_ENTRY : FLOW_KIND_NORMAL;
        b->label = NULL;
        b->edge_count = 0;
        uint32_t j = i + 1;
        while (j < ni) {
            if (leader[j]) break;
            if (!flow_insn_is_code(&m->insns[j])) { j++; continue; }
            b->end = j;
            j++;
        }
        i = j;
    }

    free(leader);
}

static void add_edge(flow_block_t *b, uint32_t to, const char *label, int kind, int dashed) {
    if (b->edge_count >= FLOW_MAX_EDGES_PER_BLOCK) return;
    for (uint32_t e = 0; e < b->edge_count; e++)
        if (b->edges[e].to == to && b->edges[e].kind == kind) return;
    b->edges[b->edge_count].to     = to;
    b->edges[b->edge_count].label  = label;
    b->edges[b->edge_count].kind   = kind;
    b->edges[b->edge_count].dashed = dashed;
    b->edge_count++;
}

void flow_build_edges(flow_method_t *fm) {
    smali_method_def_t *m = fm->m;
    uint32_t bc = fm->block_count;

    for (uint32_t bi = 0; bi < bc; bi++) {
        flow_block_t *b = &fm->blocks[bi];
        const smali_insn_t *last = &m->insns[b->end];
        int last_is_term = flow_insn_is_term(last);
        const char *nm = flow_op_name(last->op);

        if (last_is_term && flow_insn_is_return(last)) {
            b->kind = FLOW_KIND_RETURN;
            b->label = nm;
            continue;
        }
        if (last_is_term && flow_insn_is_throw(last)) {
            b->kind = FLOW_KIND_THROW;
            b->label = "throw";
            continue;
        }
        if (last_is_term && flow_insn_is_switch(last)) {
            b->kind = FLOW_KIND_SWITCH;
            b->label = nm;
        } else if (last_is_term && (last->fmt == 9 || last->fmt == 10)) {
            b->kind = FLOW_KIND_COND;
            b->label = nm;
        } else if (last_is_term && last->fmt >= 4 && last->fmt <= 6) {
            b->kind = FLOW_KIND_NORMAL;
            b->label = nm;
        }

        if (last_is_term && last->fmt >= 4 && last->fmt <= 6 && last->label_target) {
            uint32_t ti;
            if (label_lookup(fm, last->label_target, &ti)) {
                uint32_t tb = block_containing(fm, ti);
                if (tb < bc) add_edge(b, tb, nm, 1, 0);
            }
        } else if (last_is_term && (last->fmt == 9 || last->fmt == 10)) {
            if (bi + 1 < bc) add_edge(b, bi + 1, "false", 1, 0);
            if (last->label_target) {
                uint32_t ti;
                if (label_lookup(fm, last->label_target, &ti)) {
                    uint32_t tb = block_containing(fm, ti);
                    if (tb < bc) add_edge(b, tb, nm, 1, 0);
                }
            }
        } else if (last_is_term && flow_insn_is_switch(last) && last->label_target) {
            uint32_t pi;
            if (label_lookup(fm, last->label_target, &pi) && pi < fm->insn_count &&
                m->insns[pi].fmt >= 101 && m->insns[pi].fmt <= 102) {
                for (uint32_t t = 0; t < m->insns[pi].payload_targets_count; t++) {
                    uint32_t ti;
                    if (!label_lookup(fm, m->insns[pi].payload_targets[t], &ti)) continue;
                    uint32_t tb = block_containing(fm, ti);
                    if (tb < bc) add_edge(b, tb, "case", 1, 0);
                }
            }
        } else if (!last_is_term) {
            if (bi + 1 < bc) add_edge(b, bi + 1, NULL, 0, 0);
        }
    }

    for (uint32_t ci = 0; ci < m->catches_count; ci++) {
        smali_catch_t *ctch = &m->catches[ci];
        uint32_t hi;
        if (!label_lookup(fm, ctch->handler_label, &hi)) continue;
        uint32_t hb = block_containing(fm, hi);
        if (hb >= bc) continue;
        uint32_t s = fm->insn_count, e = 0;
        for (uint32_t j = 0; j < fm->label_count; j++) {
            if (strcmp(fm->label_names[j], ctch->start_label) == 0) s = fm->label_idxs[j];
            if (strcmp(fm->label_names[j], ctch->end_label)   == 0) e = fm->label_idxs[j];
        }
        if (e <= s || e >= fm->insn_count) continue;
        for (uint32_t bx = 0; bx < bc; bx++) {
            if (fm->blocks[bx].start < s || fm->blocks[bx].start >= e) continue;
            add_edge(&fm->blocks[bx], hb, ctch->type ? ctch->type : "catch", 2, 1);
        }
    }
}
