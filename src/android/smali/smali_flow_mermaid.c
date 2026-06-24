#include "smali_flow_internal.h"
#include "smali_optab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_name(smali_flow_mode_t m) {
    switch (m) {
        case SMALI_FLOW_BASIC:   return "basic";
        case SMALI_FLOW_ADVANCE: return "advance";
        case SMALI_FLOW_FULL:    return "full";
    }
    return "advance";
}

static int is_skip_in_basic(const smali_insn_t *ins) {
    if (flow_insn_is_branch(ins)) return 0;
    if (flow_insn_is_switch(ins)) return 0;
    if (flow_insn_is_return(ins)) return 0;
    if (flow_insn_is_throw(ins))  return 0;
    if (flow_insn_is_invoke(ins)) return 0;
    if (ins->op == 0x0E || (ins->op >= 0x0F && ins->op <= 0x11)) return 0;
    if (ins->op == 0x27) return 0;
    if (ins->op == 0x2B || ins->op == 0x2C) return 0;
    if (ins->op == 0x0D) return 0;
    return 1;
}

static void escape_mermaid(const char *in, char *out, size_t sz) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 6 < sz; i++) {
        char c = in[i];
        if (c == '"')       { out[j++] = '\\'; out[j++] = '"'; }
        else if (c == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
        else if (c == '[' || c == ']' || c == '{' || c == '}' || c == '|')
                            { out[j++] = '\\'; out[j++] = c; }
        else if (c == '<')  { out[j++] = '<'; }
        else if (c == '>')  { out[j++] = '>'; }
        else                { out[j++] = c; }
    }
    out[j] = 0;
}

static void node_open(flow_buf_t *out, uint32_t id, flow_kind_t kind) {
    switch (kind) {
        case FLOW_KIND_ENTRY:
            fb_appendf(out, "    B%d([\"", id); break;
        case FLOW_KIND_RETURN:
            fb_appendf(out, "    B%d((", id); break;
        case FLOW_KIND_THROW:
            fb_appendf(out, "    B%d{{", id); break;
        case FLOW_KIND_SWITCH:
            fb_appendf(out, "    B%d>", id); break;
        case FLOW_KIND_INVOKE:
            fb_appendf(out, "    B%d[/", id); break;
        case FLOW_KIND_COND:
            fb_appendf(out, "    B%d{", id); break;
        default:
            fb_appendf(out, "    B%d[", id); break;
    }
}

static void node_close(flow_buf_t *out, flow_kind_t kind) {
    switch (kind) {
        case FLOW_KIND_RETURN: fb_append(out, "))\n"); break;
        case FLOW_KIND_THROW:  fb_append(out, "}}\n"); break;
        case FLOW_KIND_SWITCH: fb_append(out, "]\n"); break;
        case FLOW_KIND_INVOKE: fb_append(out, "/]\n"); break;
        case FLOW_KIND_ENTRY:  break;
        case FLOW_KIND_COND:   fb_append(out, "}\n"); break;
        default:               fb_append(out, "]\n"); break;
    }
}

static void emit_insn(flow_buf_t *out, const smali_insn_t *ins, smali_flow_mode_t mode) {
    char raw[256];
    flow_fmt_insn(ins, raw, sizeof(raw), mode == SMALI_FLOW_FULL);
    char esc[320];
    escape_mermaid(raw, esc, sizeof(esc));
    fb_append(out, esc);
}

static void emit_block(flow_buf_t *out, flow_method_t *fm, uint32_t bi, smali_flow_mode_t mode) {
    flow_block_t *b = &fm->blocks[bi];
    smali_method_def_t *m = fm->m;

    node_open(out, bi, b->kind);

    int first = 1;
    if (b->kind == FLOW_KIND_ENTRY) {
        fb_append(out, "<b>entry</b>");
        first = 0;
    }
    for (uint32_t ii = b->start; ii <= b->end; ii++) {
        if (!flow_insn_is_code(&m->insns[ii])) continue;
        if (mode == SMALI_FLOW_BASIC && is_skip_in_basic(&m->insns[ii])) continue;
        if (!first) fb_append(out, "<br/>");
        first = 0;

        for (uint32_t j = 0; j < fm->label_count; j++) {
            if (fm->label_idxs[j] == ii) {
                fb_appendf(out, "<b>:%s</b> ", fm->label_names[j]);
                break;
            }
        }
        emit_insn(out, &m->insns[ii], mode);
    }

    if (b->kind == FLOW_KIND_RETURN) {
        if (!first) fb_append(out, "<br/>");
        fb_appendf(out, "<b>%s</b>", b->label ? b->label : "return");
    } else if (b->kind == FLOW_KIND_THROW) {
        if (!first) fb_append(out, "<br/>");
        fb_append(out, "<b>throw</b>");
    } else if (b->kind == FLOW_KIND_SWITCH && b->label) {
        if (!first) fb_append(out, "<br/>");
        fb_appendf(out, "<b>%s</b>", b->label);
    }

    node_close(out, b->kind);
}

static const char *edge_arrow(const flow_edge_t *e) {
    if (e->dashed) return e->kind == 2 ? "-.->" : "-.->";
    if (e->kind == 2) return "-.->";
    if (e->kind == 1) return "-->";
    return "-->";
}

void flow_render_mermaid(flow_method_t *fm, smali_flow_mode_t mode, flow_buf_t *out) {
    smali_method_def_t *m = fm->m;

    fb_appendf(out,
        "%%{init: {'theme':'base','flowchart':{'htmlLabels':true,'curve':'basis'}}}%%\n"
        "%% mode: %s | method: %s%s\n"
        "flowchart TD\n",
        mode_name(mode),
        m->name ? m->name : "?",
        m->signature ? m->signature : "");

    fb_append(out,
        "    classDef entry   fill:#7bed9f,stroke:#27ae60,color:#000;\n"
        "    classDef returnB fill:#ff6b6b,stroke:#c0392b,color:#fff;\n"
        "    classDef throwB  fill:#ffa502,stroke:#d35400,color:#000;\n"
        "    classDef switchB fill:#a29bfe,stroke:#6c5ce7,color:#000;\n"
        "    classDef invokeB fill:#74b9ff,stroke:#0984e3,color:#000;\n"
        "    classDef condB   fill:#ffeaa7,stroke:#fdcb6e,color:#000;\n"
        "    classDef normal  fill:#dfe6e9,stroke:#636e72,color:#000;\n");

    for (uint32_t bi = 0; bi < fm->block_count; bi++)
        emit_block(out, fm, bi, mode);

    for (uint32_t bi = 0; bi < fm->block_count; bi++) {
        flow_block_t *b = &fm->blocks[bi];
        for (uint32_t e = 0; e < b->edge_count; e++) {
            const flow_edge_t *ed = &b->edges[e];
            const char *arr = edge_arrow(ed);
            if (ed->label) {
                fb_appendf(out, "    B%d %s|\"%s\"| B%d\n",
                           bi, arr, ed->label, ed->to);
            } else {
                fb_appendf(out, "    B%d %s B%d\n", bi, arr, ed->to);
            }
        }
    }

    fb_append(out, "    class ");
    for (uint32_t bi = 0; bi < fm->block_count; bi++) {
        if (bi > 0) fb_append(out, ",");
        fb_appendf(out, "B%d", bi);
    }
    fb_append(out, " normal;\n");

    for (uint32_t bi = 0; bi < fm->block_count; bi++) {
        flow_block_t *b = &fm->blocks[bi];
        const char *cls = NULL;
        switch (b->kind) {
            case FLOW_KIND_ENTRY:  cls = "entry";   break;
            case FLOW_KIND_RETURN: cls = "returnB"; break;
            case FLOW_KIND_THROW:  cls = "throwB";  break;
            case FLOW_KIND_SWITCH: cls = "switchB"; break;
            case FLOW_KIND_INVOKE: cls = "invokeB"; break;
            case FLOW_KIND_COND:   cls = "condB";   break;
            default: break;
        }
        if (cls) fb_appendf(out, "    class B%d %s;\n", bi, cls);
    }
}
