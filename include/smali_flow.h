#ifndef SMALI_FLOW_H
#define SMALI_FLOW_H

#include "smali_types.h"

typedef enum {
    SMALI_FLOW_BASIC   = 0,
    SMALI_FLOW_ADVANCE = 1,
    SMALI_FLOW_FULL    = 2,
} smali_flow_mode_t;

char* smali_flow_method(smali_method_def_t *method, smali_flow_mode_t mode);
char* smali_flow_class(smali_class_def_t *cls, smali_flow_mode_t mode);
char* smali_flow_file(smali_ctx_def_t *ctx, smali_flow_mode_t mode);

char* smali_flow_generate(smali_method_def_t *method, const char *method_name);

#endif
