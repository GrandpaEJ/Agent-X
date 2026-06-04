#ifndef FORMAT_DEX_INTERNAL_H
#define FORMAT_DEX_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int count;
    int cap;
    char **strings;
} dex_strpool;

typedef struct {
    uint32_t shorty_idx;
    uint32_t return_type_idx;
    uint32_t params_off;
} dex_proto;

typedef struct {
    uint16_t class_idx;
    uint16_t type_idx;
    uint32_t name_idx;
} dex_field;

typedef struct {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
} dex_method_id;

typedef struct {
    uint32_t method_idx;
    uint32_t access_flags;
    uint32_t code_off;
} dex_method_enc;

typedef struct {
    uint32_t field_idx;
    uint32_t access_flags;
} dex_field_enc;

typedef struct {
    uint32_t class_idx;
    uint32_t access_flags;
    uint32_t superclass_idx;
    uint32_t interfaces_off;
    uint32_t source_file_idx;
    uint32_t annotations_off;
    uint32_t class_data_off;
    int direct_count;
    int virtual_count;
    int static_count;
    int instance_count;
    dex_method_enc *direct;
    dex_method_enc *virtual;
    dex_field_enc *static_fields;
    dex_field_enc *instance_fields;
} dex_class;

typedef struct {
    uint32_t *string_ids;
    uint32_t string_count;
    int *type_ids;
    uint32_t type_count;
    dex_proto *protos;
    uint32_t proto_count;
    dex_field *fields;
    uint32_t field_count;
    dex_method_id *methods;
    uint32_t method_count;
    dex_class *classes;
    uint32_t class_count;
    const uint8_t *data;
    size_t size;
    dex_strpool sp;
    // header offsets cached
    uint32_t string_ids_off;
    uint32_t type_ids_off;
    uint32_t proto_ids_off;
    uint32_t field_ids_off;
    uint32_t method_ids_off;
    uint32_t class_defs_off;
} dex_ctx;

// ULEB128 reader used by multiple format_dex_* modules.
int dex_read_uleb128(const uint8_t *data, uint32_t *val);

#endif
