#ifndef SMALI_CODE_H
#define SMALI_CODE_H

#include "smali_types.h"
#include "smali_buf.h"

uint32_t write_code_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m);

#endif
