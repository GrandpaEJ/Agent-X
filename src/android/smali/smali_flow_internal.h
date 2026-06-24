#ifndef SMALI_FLOW_INTERNAL_H
#define SMALI_FLOW_INTERNAL_H

#include "smali_types.h"
#include "smali_flow.h"
#include <stdint.h>
#include <stddef.h>

#define FLOW_MAX_BLOCKS 512
#define FLOW_MAX_EDGES_PER_BLOCK 32
#define FLOW_OUT_CHUNK 4096

typedef enum {
    FLOW_KIND_NORMAL   = 0,
    FLOW_KIND_ENTRY    = 1,
    FLOW_KIND_RETURN   = 2,
    FLOW_KIND_THROW    = 3,
    FLOW_KIND_SWITCH   = 4,
    FLOW_KIND_INVOKE   = 5,
    FLOW_KIND_COND     = 6,
} flow_kind_t;

typedef struct {
    uint32_t    to;
    const char *label;
    int         kind;
    int         dashed;
} flow_edge_t;

typedef struct {
    uint32_t    start;
    uint32_t    end;
    flow_kind_t kind;
    const char *label;
    flow_edge_t edges[FLOW_MAX_EDGES_PER_BLOCK];
    uint32_t    edge_count;
} flow_block_t;

typedef struct {
    smali_method_def_t *m;
    uint32_t            insn_count;
    uint32_t            label_count;
    const char        **label_names;
    uint32_t           *label_idxs;
    flow_block_t        blocks[FLOW_MAX_BLOCKS];
    uint32_t            block_count;
} flow_method_t;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} flow_buf_t;

void   fb_init(flow_buf_t *b);
void   fb_free(flow_buf_t *b);
void   fb_append(flow_buf_t *b, const char *s);
void   fb_appendf(flow_buf_t *b, const char *fmt, ...);
char  *fb_steal(flow_buf_t *b);

void   flow_build_blocks(flow_method_t *fm);
void   flow_build_edges(flow_method_t *fm);
void   flow_render_mermaid(flow_method_t *fm, smali_flow_mode_t mode, flow_buf_t *out);
void   flow_render_class(smali_class_def_t *cls, smali_flow_mode_t mode, flow_buf_t *out);
void   flow_render_file(smali_ctx_def_t *ctx, smali_flow_mode_t mode, flow_buf_t *out);

int    flow_insn_is_term(const smali_insn_t *ins);
int    flow_insn_is_code(const smali_insn_t *ins);
int    flow_insn_is_branch(const smali_insn_t *ins);
int    flow_insn_is_switch(const smali_insn_t *ins);
int    flow_insn_is_return(const smali_insn_t *ins);
int    flow_insn_is_throw(const smali_insn_t *ins);
int    flow_insn_is_invoke(const smali_insn_t *ins);
const char *flow_op_name(uint8_t op);
void   flow_reg_name(char *b, size_t sz, uint32_t r);
void   flow_fmt_insn(const smali_insn_t *ins, char *b, size_t sz, int with_line);

#endif
