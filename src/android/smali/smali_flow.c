#include "smali_flow.h"
#include "smali_flow_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *smali_flow_method(smali_method_def_t *m, smali_flow_mode_t mode) {
    if (!m) return strdup("flowchart TD\n    EMPTY[no method]\n");
    flow_buf_t out; fb_init(&out);

    flow_method_t fm = {0};
    fm.m = m;
    fm.insn_count = m->insns_count;
    flow_build_blocks(&fm);
    flow_build_edges(&fm);
    flow_render_mermaid(&fm, mode, &out);

    free(fm.label_names);
    free(fm.label_idxs);
    return fb_steal(&out);
}

char *smali_flow_class(smali_class_def_t *cls, smali_flow_mode_t mode) {
    flow_buf_t out; fb_init(&out);
    flow_render_class(cls, mode, &out);
    return fb_steal(&out);
}

char *smali_flow_file(smali_ctx_def_t *ctx, smali_flow_mode_t mode) {
    flow_buf_t out; fb_init(&out);
    flow_render_file(ctx, mode, &out);
    return fb_steal(&out);
}

char *smali_flow_generate(smali_method_def_t *method, const char *method_name) {
    (void)method_name;
    return smali_flow_method(method, SMALI_FLOW_ADVANCE);
}
