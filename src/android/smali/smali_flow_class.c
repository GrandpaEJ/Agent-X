#include "smali_flow_internal.h"
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

static void sanitize_id(char *out, size_t sz, const char *in) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 1 < sz; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) out[j++] = c;
        else if (c == '/' || c == '_' || c == '$') out[j++] = '_';
    }
    out[j] = 0;
}

static void collect_method_calls(smali_method_def_t *m, char **callees, int *count, int cap) {
    *count = 0;
    if (!m || !m->insns) return;
    for (uint32_t i = 0; i < m->insns_count && *count < cap; i++) {
        const smali_insn_t *ins = &m->insns[i];
        if (!flow_insn_is_invoke(ins)) continue;
        if (!ins->ref_str) continue;
        int dup = 0;
        for (int k = 0; k < *count; k++) if (strcmp(callees[k], ins->ref_str) == 0) { dup = 1; break; }
        if (!dup) callees[(*count)++] = ins->ref_str;
    }
}

void flow_render_class(smali_class_def_t *cls, smali_flow_mode_t mode, flow_buf_t *out) {
    (void)mode;
    if (!cls) {
        fb_append(out, "flowchart TD\n    EMPTY[no class]\n");
        return;
    }

    fb_appendf(out,
        "%%{init: {'theme':'base'}}%%\n"
        "%% class flow | %s | mode: %s\n"
        "flowchart TD\n",
        cls->descriptor ? cls->descriptor : "?", mode_name(mode));

    char self_id[128];
    sanitize_id(self_id, sizeof(self_id), cls->descriptor);
    fb_appendf(out, "    classDef thisCls fill:#74b9ff,stroke:#0984e3,color:#000;\n");
    fb_appendf(out, "    classDef callee  fill:#dfe6e9,stroke:#636e72,color:#000;\n");
    fb_appendf(out, "    ME[%s]:::thisCls\n", cls->descriptor ? cls->descriptor : "this");

    smali_method_def_t *groups[2] = {cls->direct_methods, cls->virtual_methods};
    uint32_t counts[2] = {cls->direct_method_count, cls->virtual_method_count};
    const char *kinds[2] = {"direct", "virtual"};

    for (int g = 0; g < 2; g++) {
        for (uint32_t i = 0; i < counts[g]; i++) {
            smali_method_def_t *m = &groups[g][i];
            char mid[256];
            snprintf(mid, sizeof(mid), "%s_%u_%s", self_id, i, kinds[g]);
            char label[512];
            snprintf(label, sizeof(label), "%s%s", m->name ? m->name : "?", m->signature ? m->signature : "");
            fb_appendf(out, "    subgraph SG_%s [\"%s %s\"]\n", mid, kinds[g], label);
            fb_appendf(out, "        M_%s[\"%s\"]\n", mid, label);

            char *callees[64];
            int ccount = 0;
            collect_method_calls(m, callees, &ccount, 64);
            for (int c = 0; c < ccount; c++) {
                char cid[256];
                sanitize_id(cid, sizeof(cid), callees[c]);
                char cname[512];
                snprintf(cname, sizeof(cname), "%s", callees[c]);
                fb_appendf(out, "        C_%s[\"%s\"]:::callee\n", cid, cname);
                fb_appendf(out, "        M_%s --> C_%s\n", mid, cid);
            }
            fb_appendf(out, "    end\n");
            fb_appendf(out, "    ME --> M_%s\n", mid);
        }
    }
}

void flow_render_file(smali_ctx_def_t *ctx, smali_flow_mode_t mode, flow_buf_t *out) {
    (void)mode;
    if (!ctx || ctx->class_count == 0) {
        fb_append(out, "flowchart TD\n    EMPTY[no classes]\n");
        return;
    }

    fb_appendf(out,
        "%%{init: {'theme':'base'}}%%\n"
        "%% file flow | %u classes | mode: %s\n"
        "flowchart LR\n",
        ctx->class_count, mode_name(mode));

    fb_append(out,
        "    classDef cls    fill:#74b9ff,stroke:#0984e3,color:#000;\n"
        "    classDef entryM fill:#7bed9f,stroke:#27ae60,color:#000;\n"
        "    classDef normal fill:#dfe6e9,stroke:#636e72,color:#000;\n");

    for (uint32_t ci = 0; ci < ctx->class_count; ci++) {
        smali_class_def_t *c = &ctx->classes[ci];
        char cid[128];
        sanitize_id(cid, sizeof(cid), c->descriptor);
        fb_appendf(out, "    subgraph CL_%s [\"%s\"]\n", cid, c->descriptor ? c->descriptor : "?");
        fb_appendf(out, "        CL_%s_NODE[\"class\"]:::cls\n", cid);

        smali_method_def_t *groups[2] = {c->direct_methods, c->virtual_methods};
        uint32_t counts[2] = {c->direct_method_count, c->virtual_method_count};
        const char *kinds[2] = {"d", "v"};

        for (int g = 0; g < 2; g++) {
            for (uint32_t mi = 0; mi < counts[g]; mi++) {
                smali_method_def_t *m = &groups[g][mi];
                char mid[256];
                snprintf(mid, sizeof(mid), "%s_%s%u", cid, kinds[g], mi);
                char label[512];
                snprintf(label, sizeof(label), "%s%s", m->name ? m->name : "?", m->signature ? m->signature : "");
                int is_entry = (m->access_flags & 0x10000) != 0;
                fb_appendf(out, "        M_%s[\"%s\"]%s\n", mid, label,
                           is_entry ? ":::entryM" : ":::normal");
            }
        }
        fb_append(out, "    end\n");
    }

    for (uint32_t ci = 0; ci < ctx->class_count; ci++) {
        smali_class_def_t *c = &ctx->classes[ci];
        char cid[128];
        sanitize_id(cid, sizeof(cid), c->descriptor);
        smali_method_def_t *groups[2] = {c->direct_methods, c->virtual_methods};
        uint32_t counts[2] = {c->direct_method_count, c->virtual_method_count};
        const char *kinds[2] = {"d", "v"};

        for (int g = 0; g < 2; g++) {
            for (uint32_t mi = 0; mi < counts[g]; mi++) {
                smali_method_def_t *m = &groups[g][mi];
                char mid[256];
                snprintf(mid, sizeof(mid), "%s_%s%u", cid, kinds[g], mi);
                char *callees[64];
                int ccount = 0;
                collect_method_calls(m, callees, &ccount, 64);
                for (int c = 0; c < ccount; c++) {
                    const char *cref = callees[c];
                    const char *slash = strchr(cref, '-');
                    const char *paren = strrchr(cref, '(');
                    if (!slash || !paren || paren < slash) continue;
                    char cls_name[256];
                    size_t cn_len = (size_t)(paren - slash - 1);
                    if (cn_len >= sizeof(cls_name)) cn_len = sizeof(cls_name) - 1;
                    memcpy(cls_name, slash + 1, cn_len);
                    cls_name[cn_len] = 0;
                    char target_id[128];
                    sanitize_id(target_id, sizeof(target_id), cls_name);
                    fb_appendf(out, "    M_%s --> M_%s_0\n", mid, target_id);
                }
            }
        }
    }
}
