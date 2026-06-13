#ifndef FORMAT_ARSC_INTERNAL_H
#define FORMAT_ARSC_INTERNAL_H

#include "formats.h"
#include <stdint.h>
#include <stddef.h>

#define RES_NULL_TYPE               0x0000
#define RES_STRING_POOL_TYPE        0x0001
#define RES_TABLE_TYPE              0x0002
#define RES_TABLE_PACKAGE_TYPE      0x0200
#define RES_TABLE_TYPE_TYPE         0x0201
#define RES_TABLE_TYPE_SPEC_TYPE    0x0202
#define RES_TABLE_LIBRARY_TYPE      0x0203

struct arsc_ctx {
    const uint8_t *data;
    size_t size;
    
    // String pool references (zero-copy)
    const uint8_t *string_pool_data;
    uint32_t string_pool_size;
    uint32_t string_count;
    uint32_t string_pool_offset;
    const uint32_t *string_offsets;
    int is_utf8;
    
    // Patching
    char **patched_strings;
    int *patched;
    
    // Parsed string cache
    char **string_cache;
};

#endif
