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

#define MAX_PACKAGES 4
#define MAX_TYPES 32
#define MAX_ATTR_ENTRIES 2048

typedef struct {
    uint32_t attr_id;  // resource ID of the attribute (e.g. 0x0101001c for orientation)
    int32_t value;     // numeric value (e.g. 1 for vertical)
    char *name;        // human name (e.g. "vertical")
} arsc_attr_map;

typedef struct {
    const uint8_t *type_pool;     // type name string pool
    const uint8_t *key_pool;      // key name string pool
    uint8_t id;                   // package ID (0x7f typically)
    char name[256];               // package name
    uint32_t type_pool_off;       // offset from data start
    uint32_t key_pool_off;
    uint32_t pkg_off;             // package chunk offset
    
    // Type-specific data per type ID (1-indexed)
    struct {
        const char *name;          // type name (string, id, layout, etc.)
        uint8_t id;                // type ID (1-based)
        uint32_t entry_count;      // number of entries
        const uint32_t *entry_offsets; // per-config entry offsets (from latest type chunk)
        const uint8_t *entry_data;     // entry data base pointer
        const uint8_t *key_pool;       // key pool for this type
    } types[MAX_TYPES];
    int type_count;
    
    // Parsed attr bag entries (enum/flag definitions from ResTable_map)
    arsc_attr_map attr_entries[MAX_ATTR_ENTRIES];
    int attr_entry_count;
} arsc_package;

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
    
    // Package/type/entry resolution
    arsc_package packages[MAX_PACKAGES];
    int package_count;
};

// Internal helpers
uint16_t arsc_r16(const uint8_t *p);
uint32_t arsc_r32(const uint8_t *p);
const char *arsc_sp_string(const uint8_t *pool, uint32_t index);

#endif
