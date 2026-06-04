#include "smali_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern uint32_t smali_encode_method_insns(smali_ctx_def_t *ctx, smali_method_def_t *m, uint16_t *out_buf);
extern uint32_t adler32(const uint8_t *data, size_t len);
extern void smali_sha1(const uint8_t *data, size_t len, uint8_t *out);

typedef struct {
    uint8_t *buf;
    uint32_t len;
    uint32_t cap;
} smali_buf_t;

static void buf_init(smali_buf_t *b) {
    b->buf = malloc(4096); b->len = 0; b->cap = 4096;
}

static void buf_free(smali_buf_t *b) {
    free(b->buf);
    b->buf = NULL; b->len = 0; b->cap = 0;
}

static void buf_write(smali_buf_t *b, const void *data, uint32_t size) {
    if (b->len + size > b->cap) {
        while (b->len + size > b->cap) b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, data, size);
    b->len += size;
}

static void buf_write_u32(smali_buf_t *b, uint32_t val) { buf_write(b, &val, 4); }
static void buf_write_u16(smali_buf_t *b, uint16_t val) { buf_write(b, &val, 2); }
static void buf_write_u8(smali_buf_t *b, uint8_t val) { buf_write(b, &val, 1); }

static void buf_write_uleb128(smali_buf_t *b, uint32_t val) {
    uint8_t buf[5]; int len = 0;
    do {
        uint8_t byte = val & 0x7F; val >>= 7;
        if (val) byte |= 0x80;
        buf[len++] = byte;
    } while (val);
    buf_write(b, buf, len);
}

static void buf_write_sleb128(smali_buf_t *b, int32_t val) {
    uint8_t buf[5]; int len = 0;
    int more = 1;
    while (more) {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if ((val == 0 && !(byte & 0x40)) || (val == -1 && (byte & 0x40))) {
            more = 0;
        } else {
            byte |= 0x80;
        }
        buf[len++] = byte;
    }
    buf_write(b, buf, len);
}

static void align_4(smali_buf_t *b) {
    uint32_t rem = b->len % 4;
    if (rem) {
        uint8_t pad[4] = {0};
        buf_write(b, pad, 4 - rem);
    }
}

static void write_map_item(smali_buf_t *b, uint16_t type, uint32_t size, uint32_t offset) {
    buf_write_u16(b, type);
    buf_write_u16(b, 0); // unused
    buf_write_u32(b, size);
    buf_write_u32(b, offset);
}

typedef struct {
    uint32_t type_idx;
    uint32_t handler_addr;
} write_handler_t;

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    write_handler_t handlers[16];
    uint32_t handlers_count;
} write_try_range_t;

static int comp_ranges(const void *a, const void *b) {
    const write_try_range_t *ra = (const write_try_range_t *)a;
    const write_try_range_t *rb = (const write_try_range_t *)b;
    if (ra->start_addr != rb->start_addr) {
        return (ra->start_addr < rb->start_addr) ? -1 : 1;
    }
    return (ra->end_addr < rb->end_addr) ? -1 : 1;
}

static uint32_t write_code_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    if (m->insns_count == 0) return 0;
    align_4(b);
    uint32_t offset = b->len;
    
    uint16_t *code_buf = malloc(m->insns_count * 3 * sizeof(uint16_t));
    uint32_t code_words = smali_encode_method_insns(ctx, m, code_buf);

    write_try_range_t ranges[64];
    uint32_t range_count = 0;

    if (m->catches_count > 0) {
        uint32_t *insn_offsets = malloc((m->insns_count + 1) * sizeof(uint32_t));
        uint32_t cur_offset = 0;
        for (uint32_t i = 0; i < m->insns_count; i++) {
            insn_offsets[i] = cur_offset;
            switch (m->insns[i].fmt) {
                case 0: case 1: case 21: case 2: case 3: case 4: cur_offset += 1; break;
                case 5: case 7: case 19: case 8: case 9: case 10: case 11: case 12: case 22: case 13: case 23: cur_offset += 2; break;
                case 6: case 14: case 15: case 16: case 18: case 24: case 25: cur_offset += 3; break;
                default: cur_offset += 1; break;
            }
        }
        insn_offsets[m->insns_count] = cur_offset;

        for (uint32_t c_idx = 0; c_idx < m->catches_count; c_idx++) {
            smali_catch_t *ctch = &m->catches[c_idx];
            uint32_t start_addr = 0;
            uint32_t end_addr = 0;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (strcmp(m->labels[j].name, ctch->start_label) == 0) {
                    start_addr = insn_offsets[m->labels[j].offset];
                }
                if (strcmp(m->labels[j].name, ctch->end_label) == 0) {
                    end_addr = insn_offsets[m->labels[j].offset];
                }
            }
            uint32_t handler_addr = 0;
            for (uint32_t j = 0; j < m->labels_count; j++) {
                if (strcmp(m->labels[j].name, ctch->handler_label) == 0) {
                    handler_addr = insn_offsets[m->labels[j].offset];
                    break;
                }
            }
            uint32_t type_idx = 0xFFFFFFFF;
            if (ctch->type) {
                type_idx = smali_pool_find(&ctx->types, ctch->type);
            }

            int found_range = -1;
            for (uint32_t r = 0; r < range_count; r++) {
                if (ranges[r].start_addr == start_addr && ranges[r].end_addr == end_addr) {
                    found_range = (int)r;
                    break;
                }
            }

            if (found_range == -1) {
                write_try_range_t *rng = &ranges[range_count++];
                rng->start_addr = start_addr;
                rng->end_addr = end_addr;
                rng->handlers_count = 1;
                rng->handlers[0].type_idx = type_idx;
                rng->handlers[0].handler_addr = handler_addr;
            } else {
                write_try_range_t *rng = &ranges[found_range];
                if (type_idx == 0xFFFFFFFF) {
                    rng->handlers[rng->handlers_count++] = (write_handler_t){type_idx, handler_addr};
                } else {
                    int catchall_idx = -1;
                    for (uint32_t h = 0; h < rng->handlers_count; h++) {
                        if (rng->handlers[h].type_idx == 0xFFFFFFFF) {
                            catchall_idx = (int)h;
                            break;
                        }
                    }
                    if (catchall_idx != -1) {
                        for (int h = (int)rng->handlers_count; h > catchall_idx; h--) {
                            rng->handlers[h] = rng->handlers[h - 1];
                        }
                        rng->handlers[catchall_idx] = (write_handler_t){type_idx, handler_addr};
                        rng->handlers_count++;
                    } else {
                        rng->handlers[rng->handlers_count++] = (write_handler_t){type_idx, handler_addr};
                    }
                }
            }
        }
        qsort(ranges, range_count, sizeof(write_try_range_t), comp_ranges);
        free(insn_offsets);
    }

    uint16_t outs_size = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].fmt == 14 || m->insns[i].fmt == 15) {
            if (m->insns[i].vA > outs_size) {
                outs_size = m->insns[i].vA;
            }
        }
    }

    buf_write_u16(b, m->registers_count);
    buf_write_u16(b, m->ins_count);
    buf_write_u16(b, outs_size); // outs_size
    buf_write_u16(b, range_count); // tries_size
    buf_write_u32(b, 0); // debug_info_off
    buf_write_u32(b, code_words);
    buf_write(b, code_buf, code_words * 2);

    if (range_count > 0) {
        if (code_words % 2 != 0) {
            buf_write_u16(b, 0); // padding
        }
        
        smali_buf_t h_buf;
        buf_init(&h_buf);
        buf_write_uleb128(&h_buf, range_count);
        
        uint32_t *handler_list_offsets = malloc(range_count * sizeof(uint32_t));
        for (uint32_t r = 0; r < range_count; r++) {
            handler_list_offsets[r] = h_buf.len;
            int has_catchall = 0;
            uint32_t explicit_count = 0;
            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx == 0xFFFFFFFF) has_catchall = 1;
                else explicit_count++;
            }
            int32_t size = has_catchall ? -((int32_t)explicit_count) : (int32_t)explicit_count;
            buf_write_sleb128(&h_buf, size);
            
            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF) {
                    buf_write_uleb128(&h_buf, ranges[r].handlers[h].type_idx);
                    buf_write_uleb128(&h_buf, ranges[r].handlers[h].handler_addr);
                }
            }
            if (has_catchall) {
                for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                    if (ranges[r].handlers[h].type_idx == 0xFFFFFFFF) {
                        buf_write_uleb128(&h_buf, ranges[r].handlers[h].handler_addr);
                        break;
                    }
                }
            }
        }
        
        for (uint32_t r = 0; r < range_count; r++) {
            buf_write_u32(b, ranges[r].start_addr);
            buf_write_u16(b, (uint16_t)(ranges[r].end_addr - ranges[r].start_addr));
            buf_write_u16(b, (uint16_t)handler_list_offsets[r]);
        }
        
        buf_write(b, h_buf.buf, h_buf.len);
        buf_free(&h_buf);
        free(handler_list_offsets);
    }
    free(code_buf);
    return offset;
}

int write_assembled_dex(smali_ctx_def_t *ctx, const char *out_dex) {
    smali_buf_t b; buf_init(&b);

    uint32_t string_count = ctx->strings.count;
    uint32_t type_count = ctx->types.count;
    uint32_t proto_count = ctx->protos.count;
    uint32_t field_count = ctx->fields.count;
    uint32_t method_count = ctx->methods.count;
    uint32_t class_count = ctx->class_count;

    uint32_t header_size = 0x70;
    uint32_t string_ids_off = header_size;
    uint32_t type_ids_off = string_ids_off + string_count * 4;
    uint32_t proto_ids_off = type_ids_off + type_count * 4;
    uint32_t field_ids_off = proto_ids_off + proto_count * 12;
    uint32_t method_ids_off = field_ids_off + field_count * 8;
    uint32_t class_defs_off = method_ids_off + method_count * 8;
    uint32_t data_off = class_defs_off + class_count * 32;

    // Allocate header and tables space
    if (data_off > b.cap) {
        while (data_off > b.cap) b.cap *= 2;
        b.buf = realloc(b.buf, b.cap);
    }
    b.len = data_off;
    memset(b.buf, 0, data_off);

    // 1. Write String Data Items and record offsets
    uint32_t *string_offsets = malloc(string_count * 4);
    for (uint32_t i = 0; i < string_count; i++) {
        string_offsets[i] = b.len;
        const char *str = ctx->strings.strings[i];
        uint32_t len = strlen(str);
        buf_write_uleb128(&b, len);
        buf_write(&b, str, len + 1);
    }
    align_4(&b);

    // 2. Write Parameter Type Lists and record offsets
    uint32_t *proto_param_offsets = malloc(proto_count * 4);
    for (uint32_t i = 0; i < proto_count; i++) {
        const char *sig = ctx->protos.strings[i];
        // Parse parameters out of signature, e.g. "(III)V" or "()"
        // Just write them as type index list
        uint32_t pcount = 0;
        uint32_t ptypes[64];
        const char *p = sig;
        if (*p == '(') p++;
        while (*p && *p != ')') {
            const char *type_start = p;
            if (*p == 'L') {
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else if (*p == '[') {
                while (*p == '[') p++;
                if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
                else p++;
            } else { p++; }
            char *type_str = strndup(type_start, p - type_start);
            ptypes[pcount++] = smali_pool_find(&ctx->types, type_str);
            free(type_str);
        }
        if (pcount > 0) {
            align_4(&b);
            proto_param_offsets[i] = b.len;
            buf_write_u32(&b, pcount);
            for (uint32_t j = 0; j < pcount; j++) {
                buf_write_u16(&b, ptypes[j]);
            }
        } else {
            proto_param_offsets[i] = 0;
        }
    }
    align_4(&b);

    // 2.5 Write Class Interfaces Type Lists and record offsets
    uint32_t *interfaces_offsets = malloc(class_count * 4);
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        if (c->interface_count > 0) {
            align_4(&b);
            interfaces_offsets[i] = b.len;
            buf_write_u32(&b, c->interface_count);
            for (uint32_t j = 0; j < c->interface_count; j++) {
                uint32_t type_idx = smali_pool_find(&ctx->types, c->interfaces[j]);
                buf_write_u16(&b, (uint16_t)type_idx);
            }
        } else {
            interfaces_offsets[i] = 0;
        }
    }
    align_4(&b);

    // 3. Write Code Items and record offsets
    uint32_t **direct_code_offsets = malloc(class_count * sizeof(uint32_t *));
    uint32_t **virtual_code_offsets = malloc(class_count * sizeof(uint32_t *));
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        direct_code_offsets[i] = c->direct_method_count ? malloc(c->direct_method_count * sizeof(uint32_t)) : NULL;
        virtual_code_offsets[i] = c->virtual_method_count ? malloc(c->virtual_method_count * sizeof(uint32_t)) : NULL;
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            direct_code_offsets[i][j] = write_code_item(ctx, &b, &c->direct_methods[j]);
        }
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            virtual_code_offsets[i][j] = write_code_item(ctx, &b, &c->virtual_methods[j]);
        }
    }
    align_4(&b);

    // 4. Write Class Data Items and record offsets
    uint32_t *class_data_offsets = malloc(class_count * 4);
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        class_data_offsets[i] = b.len;
        buf_write_uleb128(&b, c->static_field_count);
        buf_write_uleb128(&b, c->instance_field_count);
        buf_write_uleb128(&b, c->direct_method_count);
        buf_write_uleb128(&b, c->virtual_method_count);

        uint32_t last_idx = 0;
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            char key[1024]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
            uint32_t idx = smali_pool_find(&ctx->fields, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->static_fields[j].access_flags);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            char key[1024]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
            uint32_t idx = smali_pool_find(&ctx->fields, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->instance_fields[j].access_flags);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            char key[1024]; snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, c->direct_methods[j].name, c->direct_methods[j].signature);
            uint32_t idx = smali_pool_find(&ctx->methods, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->direct_methods[j].access_flags);
            buf_write_uleb128(&b, direct_code_offsets[i][j]);
            last_idx = idx;
        }
        last_idx = 0;
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            char key[1024]; snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, c->virtual_methods[j].name, c->virtual_methods[j].signature);
            uint32_t idx = smali_pool_find(&ctx->methods, key);
            buf_write_uleb128(&b, idx - last_idx);
            buf_write_uleb128(&b, c->virtual_methods[j].access_flags);
            buf_write_uleb128(&b, virtual_code_offsets[i][j]);
            last_idx = idx;
        }
    }
    align_4(&b);

    // 4.5 Write Static Field Initial Values
    uint32_t *static_values_offsets = malloc(class_count * 4);
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        int32_t last_init_idx = -1;
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].has_init_value) {
                last_init_idx = (int32_t)j;
            }
        }
        if (last_init_idx != -1) {
            static_values_offsets[i] = b.len;
            buf_write_uleb128(&b, last_init_idx + 1);
            for (int32_t j = 0; j <= last_init_idx; j++) {
                if (c->static_fields[j].has_init_value) {
                    int32_t val = c->static_fields[j].init_value;
                    int size = 4;
                    if (val >= -128 && val <= 127) size = 1;
                    else if (val >= -32768 && val <= 32767) size = 2;
                    else if (val >= -8388608 && val <= 8388607) size = 3;
                    
                    buf_write_u8(&b, ((size - 1) << 5) | 0x04);
                    for (int k = 0; k < size; k++) {
                        buf_write_u8(&b, (val >> (k * 8)) & 0xFF);
                    }
                } else {
                    buf_write_u8(&b, 0x04); // 1-byte int type
                    buf_write_u8(&b, 0x00);
                }
            }
        } else {
            static_values_offsets[i] = 0;
        }
    }
    align_4(&b);

    // 5. Fill table metadata back to the start
    // String IDs
    for (uint32_t i = 0; i < string_count; i++) {
        *(uint32_t *)(b.buf + string_ids_off + i * 4) = string_offsets[i];
    }
    // Type IDs
    for (uint32_t i = 0; i < type_count; i++) {
        *(uint32_t *)(b.buf + type_ids_off + i * 4) = smali_pool_find(&ctx->strings, ctx->types.strings[i]);
    }
    // Proto IDs
    for (uint32_t i = 0; i < proto_count; i++) {
        const char *sig = ctx->protos.strings[i];
        char shorty[64] = "V"; // Simplified shorty
        // Extract return type descriptor
        const char *close_paren = strchr(sig, ')');
        if (close_paren) shorty[0] = (close_paren[1] == 'L' || close_paren[1] == '[') ? 'L' : close_paren[1];
        
        uint32_t proto_off = proto_ids_off + i * 12;
        *(uint32_t *)(b.buf + proto_off) = smali_pool_find(&ctx->strings, shorty);
        *(uint32_t *)(b.buf + proto_off + 4) = smali_pool_find(&ctx->types, close_paren ? close_paren + 1 : "V");
        *(uint32_t *)(b.buf + proto_off + 8) = proto_param_offsets[i];
    }
    // Field IDs
    for (uint32_t i = 0; i < field_count; i++) {
        const char *key = ctx->fields.strings[i];
        char *arrow = strstr(key, "->");
        char *colon = strchr(key, ':');
        char *class_part = strndup(key, arrow - key);
        char *name_part = strndup(arrow + 2, colon - (arrow + 2));
        uint32_t field_off = field_ids_off + i * 8;
        *(uint16_t *)(b.buf + field_off) = smali_pool_find(&ctx->types, class_part);
        *(uint16_t *)(b.buf + field_off + 2) = smali_pool_find(&ctx->types, colon + 1);
        *(uint32_t *)(b.buf + field_off + 4) = smali_pool_find(&ctx->strings, name_part);
        free(class_part); free(name_part);
    }
    // Method IDs
    for (uint32_t i = 0; i < method_count; i++) {
        const char *key = ctx->methods.strings[i];
        char *arrow = strstr(key, "->");
        char *paren = strchr(key, '(');
        char *class_part = strndup(key, arrow - key);
        char *name_part = strndup(arrow + 2, paren - (arrow + 2));
        uint32_t method_off = method_ids_off + i * 8;
        *(uint16_t *)(b.buf + method_off) = smali_pool_find(&ctx->types, class_part);
        *(uint16_t *)(b.buf + method_off + 2) = smali_pool_find(&ctx->protos, paren);
        *(uint32_t *)(b.buf + method_off + 4) = smali_pool_find(&ctx->strings, name_part);
        free(class_part); free(name_part);
    }
    // Class Defs
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        uint32_t def_off = class_defs_off + i * 32;
        
        uint32_t cls_idx = smali_pool_find(&ctx->types, c->descriptor);
        if (cls_idx == 0xFFFFFFFF) {
            printf("WARNING: Class descriptor '%s' not found in types pool!\n", c->descriptor);
        }
        uint32_t super_idx = c->super_class ? smali_pool_find(&ctx->types, c->super_class) : 0xFFFFFFFF;
        if (c->super_class && super_idx == 0xFFFFFFFF) {
            printf("WARNING: Superclass '%s' of '%s' not found in types pool!\n", c->super_class, c->descriptor);
        }
        uint32_t src_idx = c->source_file ? smali_pool_find(&ctx->strings, c->source_file) : 0xFFFFFFFF;
        if (c->source_file && src_idx == 0xFFFFFFFF) {
            printf("WARNING: Source file '%s' of '%s' not found in strings pool!\n", c->source_file, c->descriptor);
        }

        *(uint32_t *)(b.buf + def_off) = cls_idx;
        *(uint32_t *)(b.buf + def_off + 4) = c->access_flags;
        *(uint32_t *)(b.buf + def_off + 8) = super_idx;
        *(uint32_t *)(b.buf + def_off + 12) = interfaces_offsets[i]; // interfaces_off
        *(uint32_t *)(b.buf + def_off + 16) = src_idx;
        *(uint32_t *)(b.buf + def_off + 20) = 0; // annotations_off
        *(uint32_t *)(b.buf + def_off + 24) = class_data_offsets[i];
        *(uint32_t *)(b.buf + def_off + 28) = static_values_offsets[i]; // static_values_off
    }

    // Write Map List at the end
    align_4(&b);
    uint32_t map_off = b.len;
    buf_write_u32(&b, 7); // 7 map items
    write_map_item(&b, 0x0000, string_count, string_ids_off);
    write_map_item(&b, 0x0001, type_count, type_ids_off);
    write_map_item(&b, 0x0002, proto_count, proto_ids_off);
    write_map_item(&b, 0x0003, field_count, field_ids_off);
    write_map_item(&b, 0x0004, method_count, method_ids_off);
    write_map_item(&b, 0x0006, class_count, class_defs_off);
    write_map_item(&b, 0x1000, 1, map_off);

    // Fill Header fields
    memcpy(b.buf, "dex\n035\0", 8);
    *(uint32_t *)(b.buf + 32) = b.len; // file_size
    *(uint32_t *)(b.buf + 36) = header_size;
    *(uint32_t *)(b.buf + 40) = 0x12345678; // endian_tag
    
    // Write table counts and offsets in header
    uint32_t header_offsets[12] = {
        string_count, string_ids_off,
        type_count, type_ids_off,
        proto_count, proto_ids_off,
        field_count, field_ids_off,
        method_count, method_ids_off,
        class_count, class_defs_off
    };
    memcpy(b.buf + 56, header_offsets, sizeof(header_offsets));
    *(uint32_t *)(b.buf + 52) = map_off;

    // Compute checksums
    // 1. SHA-1 signature over all bytes from offset 32 to end of file, written to offset 12.
    smali_sha1(b.buf + 32, b.len - 32, b.buf + 12);
    // 2. Adler32 checksum over all bytes from offset 12 to end of file, written to offset 8.
    uint32_t checksum = adler32(b.buf + 12, b.len - 12);
    *(uint32_t *)(b.buf + 8) = checksum;

    // Write file
    FILE *fp = fopen(out_dex, "wb");
    if (fp) {
        fwrite(b.buf, 1, b.len, fp);
        fclose(fp);
    }
    buf_free(&b);
    free(string_offsets); free(proto_param_offsets); free(interfaces_offsets); free(static_values_offsets);
    for (uint32_t i = 0; i < class_count; i++) {
        if (direct_code_offsets[i]) free(direct_code_offsets[i]);
        if (virtual_code_offsets[i]) free(virtual_code_offsets[i]);
    }
    free(direct_code_offsets); free(virtual_code_offsets); free(class_data_offsets);
    return fp ? 0 : -1;
}
