#include "smali_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    uint8_t op;
    uint8_t fmt;
    uint8_t kind;
    const char *name;
} smali_op_t;

static const smali_op_t smali_optab[] = {
    {0x00, 0, 0, "nop"},
    {0x01, 1, 0, "move"},
    {0x02, 23, 0, "move/from16"},
    {0x03, 24, 0, "move/16"},
    {0x04, 1, 0, "move-wide"},
    {0x05, 23, 0, "move-wide/from16"},
    {0x06, 24, 0, "move-wide/16"},
    {0x07, 1, 0, "move-object"},
    {0x08, 23, 0, "move-object/from16"},
    {0x09, 24, 0, "move-object/16"},
    {0x0A, 3, 0, "move-result"},
    {0x0B, 3, 0, "move-result-wide"},
    {0x0C, 3, 0, "move-result-object"},
    {0x0D, 3, 0, "move-exception"},
    {0x0E, 0, 0, "return-void"},
    {0x0F, 3, 0, "return"},
    {0x10, 3, 0, "return-wide"},
    {0x11, 3, 0, "return-object"},
    {0x12, 2, 0, "const/4"},
    {0x13, 11, 0, "const/16"},
    {0x14, 16, 0, "const"},
    {0x15, 11, 0, "const/high16"},
    {0x1C, 7, 2, "const-class"},
    {0x1D, 3, 0, "monitor-enter"},
    {0x1E, 3, 0, "monitor-exit"},
    {0x1A, 7, 1, "const-string"},
    {0x1B, 25, 1, "const-string-jumbo"},
    {0x1F, 7, 2, "check-cast"},
    {0x20, 8, 2, "instance-of"},
    {0x21, 1, 0, "array-length"},
    {0x22, 7, 2, "new-instance"},
    {0x23, 8, 2, "new-array"},
    {0x24, 14, 2, "filled-new-array"},
    {0x25, 15, 2, "filled-new-array/range"},
    {0x26, 18, 0, "fill-array-data"},
    {0x27, 3, 0, "throw"},
    {0x28, 4, 0, "goto"},
    {0x29, 5, 0, "goto/16"},
    {0x2A, 6, 0, "goto/32"},
    {0x2B, 18, 0, "packed-switch"},
    {0x2C, 18, 0, "sparse-switch"},
    {0x2D, 13, 0, "cmpl-float"},
    {0x2E, 13, 0, "cmpg-float"},
    {0x2F, 13, 0, "cmpl-double"},
    {0x30, 13, 0, "cmpg-double"},
    {0x31, 13, 0, "cmp-long"},
    {0x32, 10, 0, "if-eq"},
    {0x33, 10, 0, "if-ne"},
    {0x34, 10, 0, "if-lt"},
    {0x35, 10, 0, "if-ge"},
    {0x36, 10, 0, "if-gt"},
    {0x37, 10, 0, "if-le"},
    {0x38, 9, 0, "if-eqz"},
    {0x39, 9, 0, "if-nez"},
    {0x3A, 9, 0, "if-ltz"},
    {0x3B, 9, 0, "if-gez"},
    {0x3C, 9, 0, "if-gtz"},
    {0x3D, 9, 0, "if-lez"},
    {0x44, 13, 0, "aget"},
    {0x45, 13, 0, "aget-wide"},
    {0x46, 13, 0, "aget-object"},
    {0x47, 13, 0, "aget-boolean"},
    {0x48, 13, 0, "aget-byte"},
    {0x49, 13, 0, "aget-char"},
    {0x4a, 13, 0, "aget-short"},
    {0x4b, 13, 0, "aput"},
    {0x4c, 13, 0, "aput-wide"},
    {0x4d, 13, 0, "aput-object"},
    {0x4e, 13, 0, "aput-boolean"},
    {0x4f, 13, 0, "aput-byte"},
    {0x50, 13, 0, "aput-char"},
    {0x51, 13, 0, "aput-short"},
    {0x52, 8, 3, "iget"},
    {0x53, 8, 3, "iget-wide"},
    {0x54, 8, 3, "iget-object"},
    {0x55, 8, 3, "iget-boolean"},
    {0x56, 8, 3, "iget-byte"},
    {0x57, 8, 3, "iget-char"},
    {0x58, 8, 3, "iget-short"},
    {0x59, 8, 3, "iput"},
    {0x5A, 8, 3, "iput-wide"},
    {0x5B, 8, 3, "iput-object"},
    {0x5C, 8, 3, "iput-boolean"},
    {0x5D, 8, 3, "iput-byte"},
    {0x5E, 8, 3, "iput-char"},
    {0x5F, 8, 3, "iput-short"},
    {0x60, 7, 3, "sget"},
    {0x61, 7, 3, "sget-wide"},
    {0x62, 7, 3, "sget-object"},
    {0x63, 7, 3, "sget-boolean"},
    {0x64, 7, 3, "sget-byte"},
    {0x65, 7, 3, "sget-char"},
    {0x66, 7, 3, "sget-short"},
    {0x67, 7, 3, "sput"},
    {0x68, 7, 3, "sput-wide"},
    {0x69, 7, 3, "sput-object"},
    {0x6A, 7, 3, "sput-boolean"},
    {0x6B, 7, 3, "sput-byte"},
    {0x6C, 7, 3, "sput-char"},
    {0x6D, 7, 3, "sput-short"},
    {0x6E, 14, 4, "invoke-virtual"},
    {0x6F, 14, 4, "invoke-super"},
    {0x70, 14, 4, "invoke-direct"},
    {0x71, 14, 4, "invoke-static"},
    {0x72, 14, 4, "invoke-interface"},
    {0x74, 15, 4, "invoke-virtual/range"},
    {0x75, 15, 4, "invoke-super/range"},
    {0x76, 15, 4, "invoke-direct/range"},
    {0x77, 15, 4, "invoke-static/range"},
    {0x78, 15, 4, "invoke-interface/range"},
    {0xD0, 22, 0, "add-int/lit16"},
    {0xD1, 22, 0, "rsub-int/lit16"},
    {0xD2, 22, 0, "mul-int/lit16"},
    {0xD3, 22, 0, "div-int/lit16"},
    {0xD4, 22, 0, "rem-int/lit16"},
    {0xD5, 22, 0, "and-int/lit16"},
    {0xD6, 22, 0, "or-int/lit16"},
    {0xD7, 22, 0, "xor-int/lit16"},
    {0xD8, 12, 0, "add-int/lit8"},
    {0xD9, 12, 0, "rsub-int/lit8"},
    {0xDA, 12, 0, "mul-int/lit8"},
    {0xDB, 12, 0, "div-int/lit8"},
    {0xDC, 12, 0, "rem-int/lit8"},
    {0xDD, 12, 0, "and-int/lit8"},
    {0xDE, 12, 0, "or-int/lit8"},
    {0xDF, 12, 0, "xor-int/lit8"},
    {0xE0, 12, 0, "shl-int/lit8"},
    {0xE1, 12, 0, "shr-int/lit8"},
    {0xE2, 12, 0, "ushr-int/lit8"},
    /* DEX 037+ extended opcodes */
    {0xFA, 14, 4, "invoke-polymorphic"},
    {0xFB, 15, 4, "invoke-polymorphic/range"},
    {0xFC, 14, 4, "invoke-custom"},
    {0xFD, 15, 4, "invoke-custom/range"},
    {0xFE, 7, 2, "const-method-handle"},
    {0xFF, 7, 2, "const-method-type"},
    // 3-register operations
    {0x90, 13, 0, "add-int"},
    {0x91, 13, 0, "sub-int"},
    {0x92, 13, 0, "mul-int"},
    {0x93, 13, 0, "div-int"},
    {0x94, 13, 0, "rem-int"},
    {0x95, 13, 0, "and-int"},
    {0x96, 13, 0, "or-int"},
    {0x97, 13, 0, "xor-int"},
    {0x98, 13, 0, "shl-int"},
    {0x99, 13, 0, "shr-int"},
    {0x9A, 13, 0, "ushr-int"},
    {0x9B, 13, 0, "add-long"},
    {0x9C, 13, 0, "sub-long"},
    {0x9D, 13, 0, "mul-long"},
    {0x9E, 13, 0, "div-long"},
    {0x9F, 13, 0, "rem-long"},
    {0xA0, 13, 0, "and-long"},
    {0xA1, 13, 0, "or-long"},
    {0xA2, 13, 0, "xor-long"},
    {0xA3, 13, 0, "shl-long"},
    {0xA4, 13, 0, "shr-long"},
    {0xA5, 13, 0, "ushr-long"},
    {0xA6, 13, 0, "add-float"},
    {0xA7, 13, 0, "sub-float"},
    {0xA8, 13, 0, "mul-float"},
    {0xA9, 13, 0, "div-float"},
    {0xAA, 13, 0, "rem-float"},
    {0xAB, 13, 0, "add-double"},
    {0xAC, 13, 0, "sub-double"},
    {0xAD, 13, 0, "mul-double"},
    {0xAE, 13, 0, "div-double"},
    {0xAF, 13, 0, "rem-double"},
    // /2addr variants
    {0xB0, 1, 0, "add-int/2addr"},
    {0xB1, 1, 0, "sub-int/2addr"},
    {0xB2, 1, 0, "mul-int/2addr"},
    {0xB3, 1, 0, "div-int/2addr"},
    {0xB4, 1, 0, "rem-int/2addr"},
    {0xB5, 1, 0, "and-int/2addr"},
    {0xB6, 1, 0, "or-int/2addr"},
    {0xB7, 1, 0, "xor-int/2addr"},
    {0xB8, 1, 0, "shl-int/2addr"},
    {0xB9, 1, 0, "shr-int/2addr"},
    {0xBA, 1, 0, "ushr-int/2addr"},
    {0xBB, 1, 0, "add-long/2addr"},
    {0xBC, 1, 0, "sub-long/2addr"},
    {0xBD, 1, 0, "mul-long/2addr"},
    {0xBE, 1, 0, "div-long/2addr"},
    {0xBF, 1, 0, "rem-long/2addr"},
    {0xC0, 1, 0, "and-long/2addr"},
    {0xC1, 1, 0, "or-long/2addr"},
    {0xC2, 1, 0, "xor-long/2addr"},
    {0xC3, 1, 0, "shl-long/2addr"},
    {0xC4, 1, 0, "shr-long/2addr"},
    {0xC5, 1, 0, "ushr-long/2addr"},
    {0xC6, 1, 0, "add-float/2addr"},
    {0xC7, 1, 0, "sub-float/2addr"},
    {0xC8, 1, 0, "mul-float/2addr"},
    {0xC9, 1, 0, "div-float/2addr"},
    {0xCA, 1, 0, "rem-float/2addr"},
    {0xCB, 1, 0, "add-double/2addr"},
    {0xCC, 1, 0, "sub-double/2addr"},
    {0xCD, 1, 0, "mul-double/2addr"},
    {0xCE, 1, 0, "div-double/2addr"},
    {0xCF, 1, 0, "rem-double/2addr"},
    // Unary operations & Type conversions
    {0x7B, 1, 0, "neg-int"},
    {0x7C, 1, 0, "not-int"},
    {0x7D, 1, 0, "neg-long"},
    {0x7E, 1, 0, "not-long"},
    {0x7F, 1, 0, "neg-float"},
    {0x80, 1, 0, "neg-double"},
    {0x81, 1, 0, "int-to-long"},
    {0x82, 1, 0, "int-to-float"},
    {0x83, 1, 0, "int-to-double"},
    {0x84, 1, 0, "long-to-int"},
    {0x85, 1, 0, "long-to-float"},
    {0x86, 1, 0, "long-to-double"},
    {0x87, 1, 0, "float-to-int"},
    {0x88, 1, 0, "float-to-long"},
    {0x89, 1, 0, "float-to-double"},
    {0x8A, 1, 0, "double-to-int"},
    {0x8B, 1, 0, "double-to-long"},
    {0x8C, 1, 0, "double-to-float"},
    {0x8D, 1, 0, "int-to-byte"},
    {0x8E, 1, 0, "int-to-char"},
    {0x8F, 1, 0, "int-to-short"},
};

static const smali_op_t *find_smali_op(const char *name) {
    for (size_t i = 0; i < sizeof(smali_optab)/sizeof(smali_optab[0]); i++) {
        if (strcmp(smali_optab[i].name, name) == 0) return &smali_optab[i];
    }
    return NULL;
}

int smali_parse_method_body(smali_ctx_def_t *ctx, smali_method_def_t *m, char **p) {
    (void)ctx;
    char *start = *p;
    int current_line = -1;  /* track current .line for next instruction */
    while (*start) {
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
        if (!*start) break;
        
        char *line_end = strchr(start, '\n');
        if (!line_end) line_end = start + strlen(start);
        
        if (*start == '#') {
            start = line_end;
            continue;
        }
        
        char *tok = smali_next_token(&start);
        if (!tok) {
            start = line_end;
            continue;
        }
        
        if (strcmp(tok, ".end") == 0) {
            char *next = smali_next_token(&start);
            if (next && strcmp(next, "method") == 0) {
                free(next); free(tok);
                *p = line_end;
                return 0;
            }
            if (next) free(next);
        } else if (strcmp(tok, ".line") == 0) {
            char *line_tok = smali_next_token(&start);
            if (line_tok) {
                current_line = atoi(line_tok);
                free(line_tok);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".param") == 0) {
            char *reg_tok = smali_next_token(&start);
            char *name_tok = smali_next_token(&start);
            if (reg_tok && name_tok) {
                m->param_names = realloc(m->param_names, (m->param_name_count + 1) * sizeof(char *));
                m->param_names[m->param_name_count++] = strdup(name_tok);
                free(name_tok);
                free(reg_tok);
            } else {
                if (reg_tok) free(reg_tok);
                if (name_tok) free(name_tok);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".prologue") == 0) {
            m->prologue_set = 1;
            free(tok);
            continue;
        } else if (strcmp(tok, ".epilogue") == 0) {
            m->epilogue_set = 1;
            free(tok);
            continue;
        } else if (strcmp(tok, ".local") == 0) {
            char *reg_tok = smali_next_token(&start);
            char *name_tok = smali_next_token(&start);
            char *type_tok = smali_next_token(&start);
            if (reg_tok && name_tok && type_tok) {
                m->local_regs = realloc(m->local_regs, (m->local_count + 1) * sizeof(uint32_t));
                m->local_names = realloc(m->local_names, (m->local_count + 1) * sizeof(char *));
                m->local_types = realloc(m->local_types, (m->local_count + 1) * sizeof(char *));
                m->local_sigs = realloc(m->local_sigs, (m->local_count + 1) * sizeof(char *));
                m->local_regs[m->local_count] = smali_parse_register(reg_tok);
                m->local_names[m->local_count] = strdup(name_tok);
                m->local_types[m->local_count] = strdup(type_tok);
                char *sig_tok = smali_next_token(&start);
                if (sig_tok && strcmp(sig_tok, "=") == 0) {
                    free(sig_tok);
                    sig_tok = smali_next_token(&start);
                }
                m->local_sigs[m->local_count] = sig_tok ? strdup(sig_tok) : NULL;
                if (sig_tok) free(sig_tok);
                m->local_count++;
                free(name_tok);
                free(type_tok);
                free(reg_tok);
            } else {
                if (reg_tok) free(reg_tok);
                if (name_tok) free(name_tok);
                if (type_tok) free(type_tok);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".array-data") == 0) {
            char *width_tok = smali_next_token(&start);
            uint16_t elem_width = width_tok ? (uint16_t)atoi(width_tok) : 4;
            if (width_tok) free(width_tok);
            if (m->insns_count >= m->insns_cap) {
                m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
            }
            smali_insn_t *ins = &m->insns[m->insns_count++];
            memset(ins, 0, sizeof(smali_insn_t));
            ins->fmt = 103; /* array-data payload */
            ins->payload_element_width = elem_width;
            ins->line_number = current_line;
            current_line = -1;

            /* collect values until .end array-data */
            while (*start) {
                char *line_end_inner = strchr(start, '\n');
                if (!line_end_inner) line_end_inner = start + strlen(start);
                char *p_inner = start;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && strcmp(next_inner, "array-data") == 0) {
                            free(next_inner);
                            free(tok_inner);
                            start = line_end_inner;
                            if (*start == '\n') start++;
                            break;
                        }
                        if (next_inner) free(next_inner);
                    } else {
                        uint8_t *expanded = realloc(ins->payload_data, ins->payload_data_len + (uint32_t)elem_width);
                        if (expanded) {
                            ins->payload_data = expanded;
                            int32_t val = (int32_t)strtol(tok_inner, NULL, 0);
                            for (int bi2 = 0; bi2 < (int)elem_width; bi2++) {
                                ins->payload_data[ins->payload_data_len + bi2] = (val >> (bi2 * 8)) & 0xFF;
                            }
                            ins->payload_data_len += elem_width;
                        }
                    }
                    free(tok_inner);
                }
                start = line_end_inner;
                if (*start == '\n') start++;
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".registers") == 0) {
            char *count_tok = smali_next_token(&start);
            if (count_tok) { m->registers_count = atoi(count_tok); free(count_tok); }
        } else if (strcmp(tok, ".annotation") == 0) {
            // Skip the entire annotation block inside method
            int depth = 1;
            free(tok);
            while (depth > 0 && *start) {
                char *line_end_inner = strchr(start, '\n');
                if (!line_end_inner) line_end_inner = start + strlen(start);
                char *p_inner = start;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".annotation") == 0) {
                        depth++;
                    } else if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && strcmp(next_inner, "annotation") == 0) {
                            depth--;
                        }
                        if (next_inner) free(next_inner);
                    }
                    free(tok_inner);
                }
                start = line_end_inner;
                if (*start == '\n') start++;
            }
            continue;
        } else if (strcmp(tok, ".locals") == 0) {
            char *count_tok = smali_next_token(&start);
            if (count_tok) {
                m->locals_count = atoi(count_tok);
                m->registers_count = m->locals_count + m->ins_count;
                free(count_tok);
            }
        } else if (strcmp(tok, ".packed-switch") == 0 || strcmp(tok, ".sparse-switch") == 0) {
            int is_packed = (strcmp(tok, ".packed-switch") == 0);
            if (m->insns_count >= m->insns_cap) {
                m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
            }
            smali_insn_t *ins = &m->insns[m->insns_count++];
            memset(ins, 0, sizeof(smali_insn_t));
            ins->fmt = is_packed ? 101 : 102;
            ins->line_number = current_line;
            current_line = -1;
            
            if (is_packed) {
                char *first_key_tok = smali_next_token(&start);
                if (first_key_tok) {
                    ins->lit = (int32_t)strtol(first_key_tok, NULL, 0);
                    free(first_key_tok);
                }
            }

            while (*start) {
                char *line_end_inner = strchr(start, '\n');
                if (!line_end_inner) line_end_inner = start + strlen(start);
                char *p_inner = start;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && (strcmp(next_inner, "packed-switch") == 0 || strcmp(next_inner, "sparse-switch") == 0)) {
                            free(next_inner);
                            free(tok_inner);
                            start = line_end_inner;
                            if (*start == '\n') start++;
                            break;
                        }
                        if (next_inner) free(next_inner);
                    } else if (tok_inner[0] == ':') {
                        ins->payload_targets = realloc(ins->payload_targets, (ins->payload_targets_count + 1) * sizeof(char *));
                        ins->payload_targets[ins->payload_targets_count++] = strdup(tok_inner);
                    } else {
                        char *arrow_tok = smali_next_token(&p_inner);
                        if (arrow_tok && strcmp(arrow_tok, "->") == 0) {
                            char *target_tok = smali_next_token(&p_inner);
                            if (target_tok && target_tok[0] == ':') {
                                int32_t key = (int32_t)strtol(tok_inner, NULL, 0);
                                ins->payload_keys = realloc(ins->payload_keys, (ins->payload_targets_count + 1) * sizeof(int32_t));
                                ins->payload_targets = realloc(ins->payload_targets, (ins->payload_targets_count + 1) * sizeof(char *));
                                ins->payload_keys[ins->payload_targets_count] = key;
                                ins->payload_targets[ins->payload_targets_count] = strdup(target_tok);
                                ins->payload_targets_count++;
                            }
                            if (target_tok) free(target_tok);
                        }
                        if (arrow_tok) free(arrow_tok);
                    }
                    free(tok_inner);
                }
                start = line_end_inner;
                if (*start == '\n') start++;
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".catch") == 0 || strcmp(tok, ".catchall") == 0) {
            if (m->catches_count >= m->catches_cap) {
                m->catches_cap = m->catches_cap ? m->catches_cap * 2 : 4;
                m->catches = realloc(m->catches, m->catches_cap * sizeof(smali_catch_t));
            }
            smali_catch_t *ctch = &m->catches[m->catches_count++];
            memset(ctch, 0, sizeof(smali_catch_t));
            int is_catchall = (strcmp(tok, ".catchall") == 0);
            if (!is_catchall) {
                ctch->type = smali_next_token(&start);
            }
            char *range_tok = smali_next_token(&start);
            if (range_tok) {
                char *dots = strstr(range_tok, "..");
                if (dots) {
                    *dots = '\0';
                    char *start_lbl = range_tok;
                    if (start_lbl[0] == '{') start_lbl++;
                    while (*start_lbl == ' ' || *start_lbl == '\t') start_lbl++;
                    char *end_lbl = dots + 2;
                    while (*end_lbl == ' ' || *end_lbl == '\t') end_lbl++;
                    char *brace = strchr(end_lbl, '}');
                    if (brace) *brace = '\0';
                    size_t sl = strlen(start_lbl);
                    while (sl > 0 && (start_lbl[sl-1] == ' ' || start_lbl[sl-1] == '\t')) { start_lbl[sl-1] = '\0'; sl--; }
                    size_t el = strlen(end_lbl);
                    while (el > 0 && (end_lbl[el-1] == ' ' || end_lbl[el-1] == '\t')) { end_lbl[el-1] = '\0'; el--; }

                    ctch->start_label = strdup(start_lbl);
                    ctch->end_label = strdup(end_lbl);
                }
                free(range_tok);
            }
            ctch->handler_label = smali_next_token(&start);
        } else if (tok[0] == ':') {
            if (m->labels_count >= m->labels_cap) {
                m->labels_cap = m->labels_cap ? m->labels_cap * 2 : 8;
                m->labels = realloc(m->labels, m->labels_cap * sizeof(smali_label_t));
            }
            smali_label_t *lbl = &m->labels[m->labels_count++];
            lbl->name = strdup(tok);
            lbl->offset = m->insns_count;
        } else {
            const smali_op_t *op = find_smali_op(tok);
            if (op) {
                if (m->insns_count >= m->insns_cap) {
                    m->insns_cap = m->insns_cap ? m->insns_cap * 2 : 32;
                    m->insns = realloc(m->insns, m->insns_cap * sizeof(smali_insn_t));
                }
                smali_insn_t *ins = &m->insns[m->insns_count++];
                memset(ins, 0, sizeof(smali_insn_t));
                ins->op = op->op;
                ins->fmt = op->fmt;
                ins->kind = op->kind;
                ins->line_number = current_line;
                current_line = -1;
                
                if (ins->fmt == 1 || ins->fmt == 21 || ins->fmt == 23 || ins->fmt == 24) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 2 || ins->fmt == 11) {
                    char *vA_tok = smali_next_token(&start);
                    char *lit_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (lit_tok) {
                        if (lit_tok[0] == '#') ins->lit = (int32_t)strtol(lit_tok + 1, NULL, 0);
                        else ins->lit = (int32_t)strtol(lit_tok, NULL, 0);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 3) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 4 || ins->fmt == 5 || ins->fmt == 6) {
                    ins->label_target = smali_next_token(&start);
                } else if (ins->fmt == 7 || ins->fmt == 19 || ins->fmt == 25) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->ref_str = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 8) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    ins->ref_str = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 9 || ins->fmt == 10) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (vA_tok) free(vA_tok);
                    if (ins->fmt == 10) {
                        char *vB_tok = smali_next_token(&start);
                        ins->vB = smali_parse_register(vB_tok);
                        if (vB_tok) free(vB_tok);
                    }
                    ins->label_target = smali_next_token(&start);
                } else if (ins->fmt == 12 || ins->fmt == 22) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    char *lit_tok = smali_next_token(&start);
                    if (lit_tok) {
                        if (lit_tok[0] == '#') ins->lit = (int32_t)strtol(lit_tok + 1, NULL, 0);
                        else ins->lit = (int32_t)strtol(lit_tok, NULL, 0);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                } else if (ins->fmt == 13) {
                    char *vA_tok = smali_next_token(&start);
                    char *vB_tok = smali_next_token(&start);
                    char *vC_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->vB = smali_parse_register(vB_tok);
                    ins->vC = smali_parse_register(vC_tok);
                    if (vA_tok) free(vA_tok);
                    if (vB_tok) free(vB_tok);
                    if (vC_tok) free(vC_tok);
                } else if (ins->fmt == 14) {
                    char *reg_list = smali_next_token(&start);
                    if (reg_list) {
                        char *p_reg = reg_list;
                        if (p_reg[0] == '{') p_reg++;
                        uint32_t reg_idx = 0;
                        char rname[32]; int r_len = 0;
                        while (*p_reg && *p_reg != '}') {
                            if (*p_reg == ' ' || *p_reg == '\t' || *p_reg == ',') {
                                if (r_len > 0) {
                                    rname[r_len] = '\0';
                                    uint32_t reg_num = smali_parse_register(rname);
                                    if (reg_idx < 5) ins->regs[reg_idx] = reg_num;
                                    reg_idx++; r_len = 0;
                                }
                            } else { rname[r_len++] = *p_reg; }
                            p_reg++;
                        }
                        if (r_len > 0) {
                            rname[r_len] = '\0';
                            uint32_t reg_num = smali_parse_register(rname);
                            if (reg_idx < 5) ins->regs[reg_idx] = reg_num;
                            reg_idx++;
                        }
                        ins->vA = reg_idx; free(reg_list);
                    }
                    ins->ref_str = smali_next_token(&start);
                } else if (ins->fmt == 15) {
                    char *reg_range = smali_next_token(&start);
                    if (reg_range) {
                        char *dots = strstr(reg_range, "..");
                        if (dots) {
                            *dots = '\0';
                            char *start_reg = reg_range;
                            if (start_reg[0] == '{') start_reg++;
                            char *end_reg = dots + 2;
                            char *end_brace = strchr(end_reg, '}');
                            if (end_brace) *end_brace = '\0';
                            uint32_t first = smali_parse_register(start_reg);
                            uint32_t last = smali_parse_register(end_reg);
                            ins->vC = first;
                            ins->vA = last - first + 1;
                        } else {
                            char *start_reg = reg_range;
                            if (start_reg[0] == '{') start_reg++;
                            char *end_brace = strchr(start_reg, '}');
                            if (end_brace) *end_brace = '\0';
                            ins->vC = smali_parse_register(start_reg);
                            ins->vA = 1;
                        }
                        free(reg_range);
                    }
                    ins->ref_str = smali_next_token(&start);
                } else if (ins->fmt == 16) {
                    char *vA_tok = smali_next_token(&start);
                    char *lit_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    if (lit_tok) {
                        if (lit_tok[0] == '#') ins->lit = (int32_t)strtol(lit_tok + 1, NULL, 0);
                        else ins->lit = (int32_t)strtol(lit_tok, NULL, 0);
                        free(lit_tok);
                    }
                    if (vA_tok) free(vA_tok);
                } else if (ins->fmt == 18) {
                    char *vA_tok = smali_next_token(&start);
                    ins->vA = smali_parse_register(vA_tok);
                    ins->label_target = smali_next_token(&start);
                    if (vA_tok) free(vA_tok);
                }
            }
        }
        free(tok);
        start = line_end;
    }
    *p = start;
    return 0;
}
