#ifndef SMALI_ENCODER_H
#define SMALI_ENCODER_H

#include <stdint.h>
#include "smali_types.h"

uint32_t smali_encode_method_insns(smali_ctx_def_t *ctx, smali_method_def_t *m, uint16_t *out_buf);

#endif
