#ifndef SMALI_VALUE_H
#define SMALI_VALUE_H

#include "smali_types.h"
#include "smali_buf.h"

void write_encoded_value(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_elem_t *el);
uint32_t write_encoded_annotation(smali_buf_t *b, smali_ctx_def_t *ctx, smali_annotation_t *a);

#endif
