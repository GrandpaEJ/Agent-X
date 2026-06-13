#ifndef SMALI_PARSER_H
#define SMALI_PARSER_H

#include "smali_types.h"

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text);
int parse_smali_file_path(smali_ctx_def_t *ctx, const char *filepath);
int smali_parse_method_body(smali_ctx_def_t *ctx, smali_method_def_t *m, char **p);
void parse_annot_value(char **p, smali_annotation_elem_t *el);

#endif
