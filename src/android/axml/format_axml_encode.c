#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "format_axml_xml_parser.h"
#include "axml_res_map.h"

typedef struct {
    char **strings;
    int count;
    int cap;
} str_builder;

static int add_string(str_builder *b, const char *str) {
    if (!str) return -1;
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->strings[i], str) == 0) return i;
    }
    if (b->count >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->strings = realloc(b->strings, b->cap * sizeof(char*));
    }
    b->strings[b->count] = strdup(str);
    return b->count++;
}

static void free_builder(str_builder *b) {
    for (int i = 0; i < b->count; i++) {
        free(b->strings[i]);
    }
    free(b->strings);
}

// Recursively walk the AST to collect all unique strings
static void collect_strings(xml_node *node, str_builder *b) {
    while (node) {
        if (node->ns) add_string(b, node->ns);
        if (node->name) add_string(b, node->name);
        
        xml_attr *attr = node->attrs;
        while (attr) {
            if (attr->ns) add_string(b, attr->ns);
            if (attr->name) add_string(b, attr->name);
            if (attr->value) add_string(b, attr->value);
            attr = attr->next;
        }
        
        collect_strings(node->children, b);
        node = node->next;
    }
}

// Looks up the Android Resource ID for a given string
static uint32_t get_res_id(const char *name) {
    int num_entries = sizeof(axml_res_map) / sizeof(axml_res_map[0]);
    for (int i = 0; i < num_entries; i++) {
        if (strcmp(axml_res_map[i].name, name) == 0) {
            return axml_res_map[i].id;
        }
    }
    return 0; // Not found
}

// Builds the resource map array based on the collected string pool
static void build_resmap(str_builder *pool, uint32_t **res_ids, int *res_count) {
    // In AXML, the resource map usually provides IDs for the first N strings.
    // However, since we just appended strings linearly, attribute names might be anywhere.
    // Android requires that strings with resource IDs MUST be at the BEGINNING of the string pool.
    // Wait, this means we must SORT the string pool or partition it!
    
    // Let's create a new partitioned string pool where strings WITH resource IDs come first.
    str_builder partitioned = {0};
    
    // Pass 1: Strings with resource IDs
    for (int i = 0; i < pool->count; i++) {
        uint32_t id = get_res_id(pool->strings[i]);
        if (id != 0) {
            add_string(&partitioned, pool->strings[i]);
        }
    }
    
    int num_res_ids = partitioned.count;
    
    // Pass 2: Remaining strings
    for (int i = 0; i < pool->count; i++) {
        uint32_t id = get_res_id(pool->strings[i]);
        if (id == 0) {
            add_string(&partitioned, pool->strings[i]);
        }
    }
    
    // Build the output array
    *res_count = num_res_ids;
    *res_ids = malloc(num_res_ids * sizeof(uint32_t));
    for (int i = 0; i < num_res_ids; i++) {
        (*res_ids)[i] = get_res_id(partitioned.strings[i]);
    }
    
    // Replace old pool with partitioned one
    free_builder(pool);
    *pool = partitioned;
}

// --- Byte Buffer ---
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} byte_buf;

static void buf_append(byte_buf *b, const void *data, size_t size) {
    if (b->len + size > b->cap) {
        b->cap = b->cap ? b->cap * 2 + size : size + 64;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, data, size);
    b->len += size;
}

static void buf_w32(byte_buf *b, uint32_t v) {
    uint8_t buf[4] = {v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF};
    buf_append(b, buf, 4);
}

static void buf_w16(byte_buf *b, uint16_t v) {
    uint8_t buf[2] = {v & 0xFF, (v >> 8) & 0xFF};
    buf_append(b, buf, 2);
}

// Write UTF-16 String
static void write_string_utf16(byte_buf *b, const char *str) {
    // Basic ASCII/UTF-8 to UTF-16 conversion
    size_t len = strlen(str);
    buf_w16(b, (uint16_t)len);
    for (size_t i = 0; i < len; i++) {
        buf_w16(b, (uint16_t)str[i]);
    }
    buf_w16(b, 0x0000); // Null terminator
}

// Parses type of attribute (0x03 string, 0x11 int, 0x12 bool, 0x01 ref)
static void parse_attr_value(str_builder *pool, const char *val, uint8_t *out_type, uint32_t *out_data, int *out_str_idx) {
    *out_str_idx = -1; // Default
    if (!val || val[0] == '\0') {
        *out_type = 0x03; // String
        *out_data = 0xFFFFFFFF;
        return;
    }
    
    if (strcmp(val, "true") == 0) {
        *out_type = 0x12; // TYPE_INT_BOOLEAN
        *out_data = 1;
    } else if (strcmp(val, "false") == 0) {
        *out_type = 0x12;
        *out_data = 0;
    } else if (strncmp(val, "@ref/0x", 7) == 0) {
        *out_type = 0x01; // TYPE_REFERENCE
        *out_data = strtoul(val + 7, NULL, 16);
    } else {
        // String
        *out_type = 0x03; // TYPE_STRING
        *out_str_idx = add_string(pool, val);
        *out_data = *out_str_idx;
    }
}

// Encodes AST into AXML body chunks
static void encode_node(byte_buf *b, xml_node *node, str_builder *pool, uint32_t *line_no) {
    while (node) {
        int ns_idx = node->ns ? add_string(pool, node->ns) : 0xFFFFFFFF;
        int name_idx = node->name ? add_string(pool, node->name) : 0xFFFFFFFF;
        
        // START_ELEMENT
        uint32_t start_off = b->len;
        buf_w16(b, 0x0102); // type
        buf_w16(b, 0x0010); // header size
        buf_w32(b, 0);      // chunk size (placeholder)
        
        buf_w32(b, *line_no); // line
        buf_w32(b, 0xFFFFFFFF); // comment
        buf_w32(b, ns_idx);
        buf_w32(b, name_idx);
        
        // Count attributes (excluding xmlns)
        int attr_count = 0;
        xml_attr *attr = node->attrs;
        while(attr) { 
            if (!attr->ns || strcmp(attr->ns, "xmlns") != 0) {
                attr_count++; 
            }
            attr = attr->next; 
        }
        
        buf_w16(b, 0x0014); // attribute start offset (usually 20 from element header start)
                            // Wait, 0x0014 is 20. But element header size is 16. Total is 36 bytes before attributes.
                            // 36 - 16 = 20 bytes. Correct.
        buf_w16(b, 0x0014); // attribute size (20 bytes per attribute)
        buf_w16(b, attr_count);
        buf_w16(b, 0); // id index
        buf_w16(b, 0); // class index
        buf_w16(b, 0); // style index
        
        // Write Attributes
        attr = node->attrs;
        while(attr) {
            if (attr->ns && strcmp(attr->ns, "xmlns") == 0) {
                attr = attr->next;
                continue;
            }
            
            int a_ns_idx = attr->ns ? add_string(pool, attr->ns) : 0xFFFFFFFF;
            int a_name_idx = attr->name ? add_string(pool, attr->name) : 0xFFFFFFFF;
            
            uint8_t a_type;
            uint32_t a_data;
            int a_str_idx;
            parse_attr_value(pool, attr->value, &a_type, &a_data, &a_str_idx);
            
            buf_w32(b, a_ns_idx);
            buf_w32(b, a_name_idx);
            buf_w32(b, a_str_idx == -1 ? 0xFFFFFFFF : a_str_idx); // raw string
            buf_w16(b, 0x0008); // size of typed value (8 bytes)
            buf_append(b, "\x00", 1); // res0
            buf_append(b, &a_type, 1); // data type
            buf_w32(b, a_data); // data
            
            attr = attr->next;
        }
        
        // Fix chunk size
        uint32_t chunk_size = b->len - start_off;
        uint8_t *sz_ptr = b->data + start_off + 4;
        sz_ptr[0] = chunk_size & 0xFF;
        sz_ptr[1] = (chunk_size >> 8) & 0xFF;
        sz_ptr[2] = (chunk_size >> 16) & 0xFF;
        sz_ptr[3] = (chunk_size >> 24) & 0xFF;
        
        // Encode Children
        encode_node(b, node->children, pool, line_no);
        
        // END_ELEMENT
        buf_w16(b, 0x0103); // type
        buf_w16(b, 0x0010); // header size
        buf_w32(b, 24);     // chunk size
        buf_w32(b, *line_no);
        buf_w32(b, 0xFFFFFFFF); // comment
        buf_w32(b, ns_idx);
        buf_w32(b, name_idx);
        
        node = node->next;
    }
}

// Phase 2 + 3: Prepares String Pool and Encodes AST to AXML Buffer
uint8_t* axml_assemble_doc(xml_doc *doc, size_t *out_size) {
    str_builder pool = {0};
    add_string(&pool, "http://schemas.android.com/apk/res/android");
    collect_strings(doc->root, &pool);
    
    uint32_t *res_ids = NULL;
    int res_count = 0;
    build_resmap(&pool, &res_ids, &res_count);
    
    // Encode XML tree chunks first (we need to know if new strings are added, 
    // but Phase 2 already collected all strings! Wait, parse_attr_value might add new strings?
    // No, all attribute values were already added in Phase 2!).
    
    byte_buf body = {0};
    uint32_t line_no = 1;
    
    // Add START_NAMESPACE
    buf_w16(&body, 0x0100);
    buf_w16(&body, 0x0010);
    buf_w32(&body, 24);
    buf_w32(&body, 1); // line
    buf_w32(&body, 0xFFFFFFFF); // comment
    buf_w32(&body, add_string(&pool, "android")); // prefix
    buf_w32(&body, add_string(&pool, "http://schemas.android.com/apk/res/android")); // uri
    
    encode_node(&body, doc->root, &pool, &line_no);
    
    // Add END_NAMESPACE
    buf_w16(&body, 0x0101);
    buf_w16(&body, 0x0010);
    buf_w32(&body, 24);
    buf_w32(&body, 1);
    buf_w32(&body, 0xFFFFFFFF);
    buf_w32(&body, add_string(&pool, "android"));
    buf_w32(&body, add_string(&pool, "http://schemas.android.com/apk/res/android"));
    
    // --- Now build the final AXML file ---
    byte_buf final = {0};
    buf_w16(&final, 0x0003); // Magic
    buf_w16(&final, 0x0008); // Header Size
    buf_w32(&final, 0); // File Size Placeholder
    
    // String Pool Chunk
    uint32_t pool_start = final.len;
    buf_w16(&final, 0x0001); // String Pool Type
    buf_w16(&final, 0x001C); // Header Size
    buf_w32(&final, 0); // Chunk size Placeholder
    buf_w32(&final, pool.count); // String count
    buf_w32(&final, 0); // Style count
    buf_w32(&final, 0); // Flags (0 = UTF-16)
    buf_w32(&final, 0x001C + pool.count * 4); // String data offset
    buf_w32(&final, 0); // Style data offset
    
    // String Offsets
    uint32_t str_offset = 0;
    for (int i = 0; i < pool.count; i++) {
        buf_w32(&final, str_offset);
        str_offset += (strlen(pool.strings[i]) * 2) + 4; // UTF-16: 2 bytes for len, 2 bytes per char, 2 bytes for null
    }
    
    // String Data
    for (int i = 0; i < pool.count; i++) {
        write_string_utf16(&final, pool.strings[i]);
    }
    
    // Align string pool to 4 bytes
    while (final.len % 4 != 0) buf_append(&final, "\x00", 1);
    
    // Fix string pool size
    uint32_t pool_size = final.len - pool_start;
    uint8_t *sz_ptr = final.data + pool_start + 4;
    sz_ptr[0] = pool_size & 0xFF;
    sz_ptr[1] = (pool_size >> 8) & 0xFF;
    sz_ptr[2] = (pool_size >> 16) & 0xFF;
    sz_ptr[3] = (pool_size >> 24) & 0xFF;
    
    // Resource Map Chunk
    if (res_count > 0) {
        uint32_t res_start = final.len;
        buf_w16(&final, 0x0180);
        buf_w16(&final, 0x0008);
        buf_w32(&final, 8 + res_count * 4);
        for (int i = 0; i < res_count; i++) {
            buf_w32(&final, res_ids[i]);
        }
    }
    
    // Append Body
    buf_append(&final, body.data, body.len);
    
    // Fix File Size
    uint32_t file_size = final.len;
    sz_ptr = final.data + 4;
    sz_ptr[0] = file_size & 0xFF;
    sz_ptr[1] = (file_size >> 8) & 0xFF;
    sz_ptr[2] = (file_size >> 16) & 0xFF;
    sz_ptr[3] = (file_size >> 24) & 0xFF;
    
    free(body.data);
    free(res_ids);
    free_builder(&pool);
    
    *out_size = final.len;
    return final.data;
}
#include <stdio.h>
int axml_assemble(const char *src_xml, const char *out_axml) {
    FILE *f = fopen(src_xml, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    
    xml_doc *doc = xml_parse(buf);
    if (!doc) {
        free(buf);
        return -1;
    }
    
    size_t out_sz = 0;
    uint8_t *axml_data = axml_assemble_doc(doc, &out_sz);
    
    FILE *out = fopen(out_axml, "wb");
    if (!out) {
        free(axml_data);
        xml_free(doc);
        free(buf);
        return -1;
    }
    fwrite(axml_data, 1, out_sz, out);
    fclose(out);
    
    free(axml_data);
    xml_free(doc);
    free(buf);
    return 0;
}
