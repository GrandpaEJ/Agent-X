#ifndef SMALI_PARSER_H
#define SMALI_PARSER_H

#include <stdint.h>
#include <stddef.h>

#define MAX_LABELS 128
#define MAX_INSNS 2048
#define MAX_FIELDS 256
#define MAX_METHODS 256

typedef struct {
    char *name;
    uint32_t offset;
} smali_label_t;

typedef struct {
    uint8_t op;
    uint8_t fmt;
    uint8_t kind;
    uint32_t vA;
    uint32_t vB;
    uint32_t vC;
    uint32_t regs[5];
    char *ref_str;
    int64_t lit;
    char *label_target;
    int line_number;     /* .line directive value, -1 if none */

    // For payloads (.packed-switch, .sparse-switch, .array-data)
    char **payload_targets;
    uint32_t payload_targets_count;
    int32_t *payload_keys;
    uint8_t *payload_data;
    uint32_t payload_data_len;
    uint16_t payload_element_width;
} smali_insn_t;

typedef struct smali_encoded_value_s smali_encoded_value_t;
typedef struct smali_annotation_element_s smali_annotation_element_t;
typedef struct smali_annotation_s smali_annotation_t;

struct smali_encoded_value_s {
    uint8_t type;
    union {
        uint64_t v_int;
        double v_double;
        char *v_string;
        struct {
            smali_encoded_value_t *elements;
            uint32_t count;
        } v_array;
        smali_annotation_t *v_annotation;
    };
};

struct smali_annotation_element_s {
    char *name;
    smali_encoded_value_t value;
};

struct smali_annotation_s {
    uint8_t visibility; /* 0=build, 1=runtime, 2=system */
    char *type;
    smali_annotation_element_t *elements;
    uint32_t element_count;
    uint32_t element_cap;
};

typedef struct {
    uint32_t access_flags;
    char *name;
    char *type;
    uint64_t init_value;
    int has_init_value;
    char *init_string;
    smali_annotation_t *annotations;
    uint32_t annotation_count;
    uint32_t annotation_cap;
} smali_field_def_t;

typedef struct {
    char *type;
    char *start_label;
    char *end_label;
    char *handler_label;
} smali_catch_t;

typedef struct {
    uint32_t access_flags;
    char *name;
    char *signature;
    uint32_t registers_count;
    uint32_t locals_count;
    uint32_t ins_count;
    smali_insn_t *insns;
    uint32_t insns_count;
    uint32_t insns_cap;
    smali_label_t *labels;
    uint32_t labels_count;
    uint32_t labels_cap;
    smali_catch_t *catches;
    uint32_t catches_count;
    uint32_t catches_cap;
    /* debug info */
    int prologue_set;
    int epilogue_set;
    char **param_names;
    uint32_t param_name_count;
    char **local_names;
    char **local_types;
    char **local_sigs;
    uint32_t *local_regs;
    uint32_t local_count;
    smali_annotation_t *annotations;
    uint32_t annotation_count;
    uint32_t annotation_cap;
    smali_annotation_t **param_annotations;
    uint32_t *param_annotation_counts;
    uint32_t param_annotation_cap;
} smali_method_def_t;

typedef struct {
    char *descriptor;
    char *super_class;
    char *source_file;
    char **interfaces;
    uint32_t interface_count;
    smali_field_def_t *static_fields;
    uint32_t static_field_count;
    uint32_t static_field_cap;
    smali_field_def_t *instance_fields;
    uint32_t instance_field_count;
    uint32_t instance_field_cap;
    smali_method_def_t *direct_methods;
    uint32_t direct_method_count;
    uint32_t direct_method_cap;
    smali_method_def_t *virtual_methods;
    uint32_t virtual_method_count;
    uint32_t virtual_method_cap;
    uint32_t access_flags;
    smali_annotation_t *annotations;
    uint32_t annotation_count;
    uint32_t annotation_cap;
} smali_class_def_t;

typedef struct {
    char **strings;
    uint32_t count;
    uint32_t cap;
} smali_pool_strings_t;

typedef struct {
    uint32_t string_idx; // Index into sorted strings pool
} smali_pool_type_t;

typedef struct {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t params_off;
    uint32_t param_count;
    uint32_t *param_type_indices;
} smali_pool_proto_t;

typedef struct {
    uint32_t class_type_idx;
    uint32_t type_idx;
    uint32_t name_idx;
} smali_pool_field_t;

typedef struct {
    uint32_t class_type_idx;
    uint32_t proto_idx;
    uint32_t name_idx;
} smali_pool_method_t;

typedef struct {
    smali_class_def_t *classes;
    uint32_t class_count;
    uint32_t class_cap;

    smali_pool_strings_t strings;
    smali_pool_strings_t types;
    smali_pool_strings_t protos;
    smali_pool_strings_t fields;
    smali_pool_strings_t methods;
} smali_ctx_def_t;

// Lexer / helper functions
char *smali_next_token(char **p);
char *smali_parse_string_literal(char **p);
uint32_t smali_parse_register(const char *tok);
smali_annotation_t *smali_parse_annotation(char **p);
smali_encoded_value_t *smali_parse_encoded_value(char **p);

// Pool functions
uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str);
uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str);
void smali_pool_sort_strings(smali_pool_strings_t *p);

#endif
