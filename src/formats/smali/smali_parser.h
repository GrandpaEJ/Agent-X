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
    int32_t lit;
    char *label_target;

    // For payloads (.packed-switch, .sparse-switch, .array-data)
    char **payload_targets;
    uint32_t payload_targets_count;
    int32_t *payload_keys;
    uint8_t *payload_data;
    uint32_t payload_data_len;
    uint16_t payload_element_width;
} smali_insn_t;

typedef struct {
    uint32_t access_flags;
    char *name;
    char *type;
    uint32_t init_value;
    int has_init_value;
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

// Pool functions
uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str);
uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str);
void smali_pool_sort_strings(smali_pool_strings_t *p);

#endif
