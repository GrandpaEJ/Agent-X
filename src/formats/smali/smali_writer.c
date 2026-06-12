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
        uint32_t old_cap = b->cap;
        while (b->len + size > b->cap) b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
        /* Zero new portion */
        memset(b->buf + old_cap, 0, b->cap - old_cap);
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

#define DBG_LINE_BASE (-4)
#define DBG_LINE_RANGE 15

static uint32_t write_debug_info(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    (void)ctx;
    /* quick check: any debug info at all? */
    int has_lines = 0, has_locals = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number >= 0) has_lines = 1;
    }
    if (m->local_count > 0) has_locals = 1;
    if (m->param_name_count == 0 && !has_lines && !m->prologue_set && !m->epilogue_set && !has_locals)
        return 0;

    align_4(b);
    uint32_t offset = b->len;

    /* determine starting line */
    int start_line = 1;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number > 0) {
            start_line = m->insns[i].line_number;
            break;
        }
    }

    /* line_start */
    buf_write_uleb128(b, (uint32_t)(start_line > 0 ? start_line : 1));

    /* parameter names */
    buf_write_uleb128(b, m->param_name_count);
    for (uint32_t pi = 0; pi < m->param_name_count; pi++) {
        uint32_t idx = smali_pool_find(&ctx->strings, m->param_names[pi]);
        buf_write_uleb128(b, idx);
    }

    /* emit debug bytecode: iterate through instructions and emit .line entries */
    int cur_line = start_line;
    uint32_t cur_addr = 0;
    uint32_t insn_offsets[4096];
    for (uint32_t i = 0; i < m->insns_count && i < 4096; i++) {
        insn_offsets[i] = cur_addr;
        switch (m->insns[i].fmt) {
            case 0: case 1: case 21: case 2: case 3: case 4: cur_addr += 1; break;
            case 5: case 7: case 19: case 8: case 9: case 10: case 11: case 12: case 22: case 13: case 23: cur_addr += 2; break;
            case 6: case 14: case 15: case 16: case 18: case 24: case 25: cur_addr += 3; break;
            case 26: cur_addr += 5; break;
            case 101: cur_addr += 4 + m->insns[i].payload_targets_count * 2; break;
            case 102: cur_addr += 2 + m->insns[i].payload_targets_count * 4; break;
            case 103: cur_addr += (m->insns[i].payload_data_len + 5) / 2 + 2; break;
            default: cur_addr += 1; break;
        }
    }

    /* .prologue after first instruction? */
    if (m->prologue_set && m->insns_count > 0) {
        buf_write_u8(b, 0x07); /* DBG_SET_PROLOGUE_END */
    }

    /* Start locals before processing instructions */
    for (uint32_t li = 0; li < m->local_count; li++) {
        uint32_t reg = m->local_regs[li] & ~0x80000000;
        uint32_t name_idx = m->local_names[li] ? smali_pool_find(&ctx->strings, m->local_names[li]) : 0xFFFFFFFF;
        uint32_t type_idx = m->local_types[li] ? smali_pool_find(&ctx->types, m->local_types[li]) : 0xFFFFFFFF;
        if (name_idx == 0xFFFFFFFF) name_idx = 0;
        if (type_idx == 0xFFFFFFFF) type_idx = 0;

        uint32_t sig_idx = 0xFFFFFFFF;
        if (m->local_sigs[li]) sig_idx = smali_pool_find(&ctx->strings, m->local_sigs[li]);

        if (sig_idx != 0xFFFFFFFF) {
            buf_write_u8(b, 0x04); /* DBG_START_LOCAL_EXTENDED */
            buf_write_uleb128(b, reg);
            buf_write_uleb128(b, name_idx);
            buf_write_uleb128(b, type_idx);
            buf_write_uleb128(b, sig_idx);
        } else {
            buf_write_u8(b, 0x03); /* DBG_START_LOCAL */
            buf_write_uleb128(b, reg);
            buf_write_uleb128(b, name_idx);
            buf_write_uleb128(b, type_idx);
        }
    }

    /* Process each instruction, emitting line entries */
    cur_line = start_line;
    cur_addr = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        uint32_t iaddr = insn_offsets[i];
        uint32_t addr_diff = iaddr - cur_addr;

        if (m->insns[i].line_number > 0) {
            int line_diff = m->insns[i].line_number - cur_line;

            while (addr_diff > 0 || line_diff != 0) {
                int use_special = 0;
                /* check if we can use a special opcode */;
                int adj = line_diff - DBG_LINE_BASE;
                if (adj >= 0 && addr_diff <= (255 - 0x0A) / DBG_LINE_RANGE) {
                    int max_adv = (255 - 0x0A) / DBG_LINE_RANGE;
                    if (addr_diff <= (uint32_t)max_adv && adj == line_diff - DBG_LINE_BASE) {
                        use_special = 1;
                    }
                }

                if (use_special && addr_diff > 0) {
                    /* use special opcode */
                    uint8_t special = 0x0A + (addr_diff * DBG_LINE_RANGE + (line_diff - DBG_LINE_BASE));
                    buf_write_u8(b, special);
                    cur_line = m->insns[i].line_number;
                    cur_addr = iaddr;
                    addr_diff = 0;
                    line_diff = 0;
                } else if (addr_diff > 0) {
                    buf_write_u8(b, 0x01); /* DBG_ADVANCE_PC */
                    buf_write_uleb128(b, addr_diff);
                    cur_addr = iaddr;
                    addr_diff = 0;
                } else if (line_diff != 0) {
                    buf_write_u8(b, 0x02); /* DBG_ADVANCE_LINE */
                    buf_write_sleb128(b, (int32_t)line_diff);
                    cur_line = m->insns[i].line_number;
                    line_diff = 0;
                } else {
                    break;
                }
            }
        } else if (addr_diff > 0) {
            /* no line change, just advance PC */
            buf_write_u8(b, 0x01); /* DBG_ADVANCE_PC */
            buf_write_uleb128(b, addr_diff);
            cur_addr = iaddr;
        }
    }

    buf_write_u8(b, 0x00); /* DBG_END_SEQUENCE */
    (void)has_locals;
    (void)m->epilogue_set;
    return offset;
}

static uint32_t write_code_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    if (m->insns_count == 0) return 0;
    align_4(b);
    uint32_t offset = b->len;
    
    uint32_t max_words = m->insns_count * 5;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].fmt == 101) max_words += 4 + m->insns[i].payload_targets_count * 2;
        if (m->insns[i].fmt == 102) max_words += 2 + m->insns[i].payload_targets_count * 4;
        if (m->insns[i].fmt == 103) max_words += 4 + (m->insns[i].payload_data_len + 1) / 2;
    }
    uint16_t *code_buf = calloc(max_words, sizeof(uint16_t));
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
                case 26: cur_offset += 5; break;
                case 101: cur_offset += 4 + m->insns[i].payload_targets_count * 2; break;
                case 102: cur_offset += 2 + m->insns[i].payload_targets_count * 4; break;
                case 103: cur_offset += 2 + (m->insns[i].payload_data_len + 1) / 2 + 2; break;
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

    /* Filter out invalid try ranges BEFORE writing header */
    {
        uint32_t valid_count = 0;
        for (uint32_t r = 0; r < range_count; r++) {
            if (ranges[r].start_addr == 0) continue;
            if (ranges[r].end_addr <= ranges[r].start_addr) continue;
            if (ranges[r].end_addr > code_words) continue;
            /* Validate handler type indices */
            int valid_handlers = 1;
            for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF &&
                    ranges[r].handlers[h].type_idx >= ctx->types.count) {
                    valid_handlers = 0;
                    break;
                }
            }
            if (!valid_handlers) continue;
            if (valid_count != r)
                ranges[valid_count] = ranges[r];
            valid_count++;
        }
        range_count = valid_count;
    }

    buf_write_u16(b, m->registers_count);
    buf_write_u16(b, m->ins_count);
    buf_write_u16(b, outs_size); // outs_size
    buf_write_u16(b, range_count); // tries_size
    uint32_t dbg_off_slot = b->len; /* patch this later */
    buf_write_u32(b, 0); // debug_info_off placeholder
    buf_write_u32(b, code_words);
    buf_write(b, code_buf, code_words * 2);

    if (range_count > 0) {
        /* Filter out invalid try ranges */
        {
            uint32_t valid_count = 0;
            for (uint32_t r = 0; r < range_count; r++) {
                if (ranges[r].start_addr == 0) continue;
                if (ranges[r].end_addr <= ranges[r].start_addr) continue;
                if (ranges[r].end_addr > code_words) continue;
                /* Also validate handler type indices */
                int valid_handlers = 1;
                for (uint32_t h = 0; h < ranges[r].handlers_count; h++) {
                    if (ranges[r].handlers[h].type_idx != 0xFFFFFFFF &&
                        ranges[r].handlers[h].type_idx >= (uint32_t)ctx->types.count) {
                        valid_handlers = 0;
                        break;
                    }
                }
                if (!valid_handlers) continue;
                if (valid_count != r)
                    ranges[valid_count] = ranges[r];
                valid_count++;
            }
            range_count = valid_count;
        }
    }

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

    /* Write debug info and patch the placeholder */
    /* uint32_t debug_info_off = write_debug_info(ctx, b, m); */
    uint32_t debug_info_off = 0;
    if (debug_info_off != 0) {
        *(uint32_t *)(b->buf + dbg_off_slot) = debug_info_off;
    }

    free(code_buf);
    align_4(b);
    return offset;
}

typedef struct {
    uint16_t type;
    uint32_t size;
    uint32_t offset;
} map_item_entry_t;

static int comp_map_entries(const void *a, const void *b) {
    const map_item_entry_t *ma = (const map_item_entry_t *)a;
    const map_item_entry_t *mb = (const map_item_entry_t *)b;
    if (ma->offset != mb->offset) {
        return (ma->offset < mb->offset) ? -1 : 1;
    }
    return 0;
}

static smali_ctx_def_t *s_write_ctx = NULL;
static const char *s_current_class_desc = NULL;

static int comp_class_fields(const void *a, const void *b) {
    const smali_field_def_t *fa = (const smali_field_def_t *)a;
    const smali_field_def_t *fb = (const smali_field_def_t *)b;
    char keyA[1024], keyB[1024];
    snprintf(keyA, sizeof(keyA), "%s->%s:%s", s_current_class_desc, fa->name, fa->type);
    snprintf(keyB, sizeof(keyB), "%s->%s:%s", s_current_class_desc, fb->name, fb->type);
    uint32_t idxA = smali_pool_find(&s_write_ctx->fields, keyA);
    uint32_t idxB = smali_pool_find(&s_write_ctx->fields, keyB);
    return (idxA < idxB) ? -1 : 1;
}

static int comp_class_methods(const void *a, const void *b) {
    const smali_method_def_t *ma = (const smali_method_def_t *)a;
    const smali_method_def_t *mb = (const smali_method_def_t *)b;
    char keyA[1024], keyB[1024];
    snprintf(keyA, sizeof(keyA), "%s->%s%s", s_current_class_desc, ma->name, ma->signature);
    snprintf(keyB, sizeof(keyB), "%s->%s%s", s_current_class_desc, mb->name, mb->signature);
    uint32_t idxA = smali_pool_find(&s_write_ctx->methods, keyA);
    uint32_t idxB = smali_pool_find(&s_write_ctx->methods, keyB);
    return (idxA < idxB) ? -1 : 1;
}

static void write_encoded_value(smali_ctx_def_t *ctx, smali_buf_t *b, smali_encoded_value_t *val) {
    if (!val) return;
    if (val->type == 0x1d) { // Annotation
        buf_write_u8(b, val->type);
        buf_write_uleb128(b, smali_pool_find(&ctx->types, val->v_annotation->type));
        buf_write_uleb128(b, val->v_annotation->element_count);
        for (uint32_t i = 0; i < val->v_annotation->element_count; i++) {
            buf_write_uleb128(b, smali_pool_find(&ctx->strings, val->v_annotation->elements[i].name));
            write_encoded_value(ctx, b, &val->v_annotation->elements[i].value);
        }
    } else if (val->type == 0x1c) { // Array
        buf_write_u8(b, val->type);
        buf_write_uleb128(b, val->v_array.count);
        for (uint32_t i = 0; i < val->v_array.count; i++) {
            write_encoded_value(ctx, b, &val->v_array.elements[i]);
        }
    } else if (val->type == 0x17) { // String
        uint32_t idx = smali_pool_find(&ctx->strings, val->v_string);
        int size = 4;
        if (idx <= 0xFF) size = 1; else if (idx <= 0xFFFF) size = 2; else if (idx <= 0xFFFFFF) size = 3;
        buf_write_u8(b, ((size - 1) << 5) | 0x17);
        for (int i = 0; i < size; i++) buf_write_u8(b, (idx >> (i * 8)) & 0xFF);
    } else if (val->type == 0x18) { // Type
        uint32_t idx = smali_pool_find(&ctx->types, val->v_string);
        int size = 4;
        if (idx <= 0xFF) size = 1; else if (idx <= 0xFFFF) size = 2; else if (idx <= 0xFFFFFF) size = 3;
        buf_write_u8(b, ((size - 1) << 5) | 0x18);
        for (int i = 0; i < size; i++) buf_write_u8(b, (idx >> (i * 8)) & 0xFF);
    } else if (val->type == 0x19 || val->type == 0x1b) { // Field/Enum
        uint32_t idx = smali_pool_find(&ctx->fields, val->v_string);
        int size = 4;
        if (idx <= 0xFF) size = 1; else if (idx <= 0xFFFF) size = 2; else if (idx <= 0xFFFFFF) size = 3;
        buf_write_u8(b, ((size - 1) << 5) | val->type);
        for (int i = 0; i < size; i++) buf_write_u8(b, (idx >> (i * 8)) & 0xFF);
    } else if (val->type == 0x1a) { // Method
        uint32_t idx = smali_pool_find(&ctx->methods, val->v_string);
        int size = 4;
        if (idx <= 0xFF) size = 1; else if (idx <= 0xFFFF) size = 2; else if (idx <= 0xFFFFFF) size = 3;
        buf_write_u8(b, ((size - 1) << 5) | 0x1a);
        for (int i = 0; i < size; i++) buf_write_u8(b, (idx >> (i * 8)) & 0xFF);
    } else if (val->type == 0x1e) { // Null
        buf_write_u8(b, 0x1e);
    } else if (val->type == 0x1f) { // Boolean
        buf_write_u8(b, (val->v_int ? 1 : 0) << 5 | 0x1f);
    } else { // Primitives
        int64_t v = (int64_t)val->v_int;
        int size = 8;
        if (v >= -128 && v <= 127) size = 1;
        else if (v >= -32768 && v <= 32767) size = 2;
        else if (v >= -8388608 && v <= 8388607) size = 3;
        else if (v >= -2147483648LL && v <= 2147483647LL) size = 4;
        else if (v >= -549755813888LL && v <= 549755813887LL) size = 5;
        else if (v >= -140737488355328LL && v <= 140737488355327LL) size = 6;
        else if (v >= -36028797018963968LL && v <= 36028797018963967LL) size = 7;
        if (val->type == 0x00) size = 1;
        else if (val->type == 0x02 || val->type == 0x03) { if (size > 2) size = 2; }
        else if (val->type == 0x04 || val->type == 0x10) { if (size > 4) size = 4; }
        buf_write_u8(b, ((size - 1) << 5) | val->type);
        for (int i = 0; i < size; i++) buf_write_u8(b, (v >> (i * 8)) & 0xFF);
    }
}

static uint32_t write_annotation_item(smali_ctx_def_t *ctx, smali_buf_t *b, smali_annotation_t *annot) {
    uint32_t off = b->len;
    buf_write_u8(b, annot->visibility);
    buf_write_uleb128(b, smali_pool_find(&ctx->types, annot->type));
    buf_write_uleb128(b, annot->element_count);
    for (uint32_t i = 0; i < annot->element_count; i++) {
        buf_write_uleb128(b, smali_pool_find(&ctx->strings, annot->elements[i].name));
        write_encoded_value(ctx, b, &annot->elements[i].value);
    }
    return off;
}

static uint32_t write_annotation_set_item(smali_buf_t *b, uint32_t *item_offsets, uint32_t count) {
    if (count == 0) return 0;
    align_4(b);
    uint32_t off = b->len;
    buf_write_u32(b, count);
    for (uint32_t i = 0; i < count; i++) buf_write_u32(b, item_offsets[i]);
    return off;
}

static uint32_t write_annotation_set_ref_list(smali_buf_t *b, uint32_t *set_offsets, uint32_t count) {
    if (count == 0) return 0;
    align_4(b);
    uint32_t off = b->len;
    buf_write_u32(b, count);
    for (uint32_t i = 0; i < count; i++) buf_write_u32(b, set_offsets[i]);
    return off;
}

int write_assembled_dex(smali_ctx_def_t *ctx, const char *out_dex) {
    printf("WRITE ASSEMBLED DEX START\\n"); fflush(stdout);
    // Sort all class fields and methods by their indexes in the pools first
    s_write_ctx = ctx;
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        s_current_class_desc = c->descriptor;
        if (c->static_field_count > 1) {
            qsort(c->static_fields, c->static_field_count, sizeof(smali_field_def_t), comp_class_fields);
        }
        if (c->instance_field_count > 1) {
            qsort(c->instance_fields, c->instance_field_count, sizeof(smali_field_def_t), comp_class_fields);
        }
        if (c->direct_method_count > 1) {
            qsort(c->direct_methods, c->direct_method_count, sizeof(smali_method_def_t), comp_class_methods);
        }
        if (c->virtual_method_count > 1) {
            qsort(c->virtual_methods, c->virtual_method_count, sizeof(smali_method_def_t), comp_class_methods);
        }
    }
    s_write_ctx = NULL;
    s_current_class_desc = NULL;

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
    memset(b.buf, 0, b.cap);

    // 1. Write String Data Items and record offsets
    uint32_t *string_offsets = malloc(string_count * 4);
    for (uint32_t i = 0; i < string_count; i++) {
        string_offsets[i] = b.len;
        const char *str = ctx->strings.strings[i];
        
        uint32_t utf16_size = 0;
        const unsigned char *p = (const unsigned char *)str;
        while (*p) {
            if (*p < 0x80) { utf16_size++; p++; }
            else if ((*p & 0xE0) == 0xC0) { utf16_size++; p += 2; }
            else if ((*p & 0xF0) == 0xE0) { utf16_size++; p += 3; }
            else if ((*p & 0xF8) == 0xF0) { utf16_size += 2; p += 4; }
            else { utf16_size++; p++; }
        }
        
        buf_write_uleb128(&b, utf16_size);
        buf_write(&b, str, strlen(str) + 1);
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
    uint32_t **direct_code_ends = malloc(class_count * sizeof(uint32_t *));
    uint32_t **virtual_code_ends = malloc(class_count * sizeof(uint32_t *));
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        direct_code_offsets[i] = c->direct_method_count ? malloc(c->direct_method_count * sizeof(uint32_t)) : NULL;
        virtual_code_offsets[i] = c->virtual_method_count ? malloc(c->virtual_method_count * sizeof(uint32_t)) : NULL;
        direct_code_ends[i] = c->direct_method_count ? malloc(c->direct_method_count * sizeof(uint32_t)) : NULL;
        virtual_code_ends[i] = c->virtual_method_count ? malloc(c->virtual_method_count * sizeof(uint32_t)) : NULL;
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            uint32_t before = b.len;
            direct_code_offsets[i][j] = write_code_item(ctx, &b, &c->direct_methods[j]);
            direct_code_ends[i][j] = b.len;
        }
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            uint32_t before = b.len;
            virtual_code_offsets[i][j] = write_code_item(ctx, &b, &c->virtual_methods[j]);
            virtual_code_ends[i][j] = b.len;
        }
    }
    /* Find the actual end of the code section */
    uint32_t code_section_end = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c2 = &ctx->classes[i];
        for (uint32_t j = 0; j < c2->direct_method_count; j++)
            if (direct_code_ends[i] && direct_code_ends[i][j] > code_section_end) code_section_end = direct_code_ends[i][j];
        for (uint32_t j = 0; j < c2->virtual_method_count; j++)
            if (virtual_code_ends[i] && virtual_code_ends[i][j] > code_section_end) code_section_end = virtual_code_ends[i][j];
    }
    /* Zero-fill gap between code section end and next aligned position */
    align_4(&b);
    if (code_section_end > 0 && b.len > code_section_end) {
        memset(b.buf + code_section_end, 0, b.len - code_section_end);
    }
    align_4(&b);

    // 4. Write Class Data Items and record offsets
    uint32_t *class_data_offsets = malloc(class_count * 4);
    /* Zero-fill from end of code section to class_data start */
    align_4(&b);
    {
        uint32_t expected = b.len;
        /* zero all bytes from here to b.cap for safety */
        if (b.len < b.cap)
            memset(b.buf + b.len, 0, b.cap - b.len);
    }
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
                    if (c->static_fields[j].init_string) {
                        uint32_t str_idx = smali_pool_find(&ctx->strings, c->static_fields[j].init_string);
                        int size = 4;
                        if (str_idx <= 0xFF) size = 1;
                        else if (str_idx <= 0xFFFF) size = 2;
                        else if (str_idx <= 0xFFFFFF) size = 3;
                        buf_write_u8(&b, ((size - 1) << 5) | 0x17); // VALUE_STRING
                        for (int k = 0; k < size; k++) {
                            buf_write_u8(&b, (str_idx >> (k * 8)) & 0xFF);
                        }
                    } else {
                        int64_t val = (int64_t)c->static_fields[j].init_value;
                        char t = c->static_fields[j].type[0];
                        if (t == 'Z') {
                            buf_write_u8(&b, (val ? 1 : 0) << 5 | 0x1F); // VALUE_BOOLEAN
                        } else {
                            int size = 8;
                            if (val >= -128 && val <= 127) size = 1;
                            else if (val >= -32768 && val <= 32767) size = 2;
                            else if (val >= -8388608 && val <= 8388607) size = 3;
                            else if (val >= -2147483648LL && val <= 2147483647LL) size = 4;
                            else if (val >= -549755813888LL && val <= 549755813887LL) size = 5;
                            else if (val >= -140737488355328LL && val <= 140737488355327LL) size = 6;
                            else if (val >= -36028797018963968LL && val <= 36028797018963967LL) size = 7;
                            
                            uint8_t type_tag = 0x04; // VALUE_INT
                            if (t == 'B') type_tag = 0x00;
                            else if (t == 'S') type_tag = 0x02;
                            else if (t == 'C') type_tag = 0x03;
                            else if (t == 'J') type_tag = 0x06;
                            else if (t == 'F') type_tag = 0x10;
                            else if (t == 'D') type_tag = 0x11;
                            if (t == 'L' || t == '[') type_tag = 0x1E; // Fallback to NULL for objects

                            if (type_tag == 0x00) size = 1;
                            else if (type_tag == 0x02 || type_tag == 0x03) { if (size > 2) size = 2; }
                            else if (type_tag == 0x04 || type_tag == 0x10) { if (size > 4) size = 4; }

                            if (type_tag == 0x1E) {
                                buf_write_u8(&b, 0x1E);
                            } else {
                                buf_write_u8(&b, ((size - 1) << 5) | type_tag);
                                for (int k = 0; k < size; k++) {
                                    buf_write_u8(&b, (val >> (k * 8)) & 0xFF);
                                }
                            }
                        }
                    }
                } else {
                    char t = c->static_fields[j].type[0];
                    if (t == 'L' || t == '[') {
                        buf_write_u8(&b, 0x1E); // VALUE_NULL
                    } else if (t == 'Z') {
                        buf_write_u8(&b, 0x1F); // VALUE_BOOLEAN (false)
                    } else if (t == 'J') {
                        buf_write_u8(&b, 0x06); buf_write_u8(&b, 0x00); // VALUE_LONG
                    } else if (t == 'F') {
                        buf_write_u8(&b, 0x10); buf_write_u8(&b, 0x00); // VALUE_FLOAT
                    } else if (t == 'D') {
                        buf_write_u8(&b, 0x11); buf_write_u8(&b, 0x00); // VALUE_DOUBLE
                    } else if (t == 'B') {
                        buf_write_u8(&b, 0x00); buf_write_u8(&b, 0x00); // VALUE_BYTE
                    } else if (t == 'C') {
                        buf_write_u8(&b, 0x03); buf_write_u8(&b, 0x00); // VALUE_CHAR
                    } else if (t == 'S') {
                        buf_write_u8(&b, 0x02); buf_write_u8(&b, 0x00); // VALUE_SHORT
                    } else {
                        buf_write_u8(&b, 0x04); buf_write_u8(&b, 0x00); // VALUE_INT
                    }
                }
            }
        } else {
            static_values_offsets[i] = 0;
        }
    }
    align_4(&b);

    // 4.6 Write Annotations
    uint32_t *annotations_dir_offsets = malloc(class_count * 4);
    memset(annotations_dir_offsets, 0, class_count * 4);
    
    uint32_t annotation_item_count = 0;
    uint32_t first_annotation_item_off = 0;
    uint32_t annotation_set_item_count = 0;
    uint32_t first_annotation_set_item_off = 0;
    uint32_t annotation_set_ref_list_count = 0;
    uint32_t first_annotation_set_ref_list_off = 0;
    uint32_t annotations_directory_count = 0;
    uint32_t first_annotations_directory_off = 0;

    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        
        uint32_t field_ann_count = 0;
        for (uint32_t j = 0; j < c->static_field_count; j++) if (c->static_fields[j].annotation_count > 0) field_ann_count++;
        for (uint32_t j = 0; j < c->instance_field_count; j++) if (c->instance_fields[j].annotation_count > 0) field_ann_count++;
        
        uint32_t method_ann_count = 0;
        uint32_t param_ann_count = 0;
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                if (m_arr[j].annotation_count > 0) method_ann_count++;
                int has_param_ann = 0;
                if (m_arr[j].param_annotations) {
                    for (uint32_t p_idx = 0; p_idx < m_arr[j].param_annotation_cap; p_idx++) {
                        if (m_arr[j].param_annotation_counts[p_idx] > 0) has_param_ann = 1;
                    }
                }
                if (has_param_ann) param_ann_count++;
            }
        }
        
        if (c->annotation_count == 0 && field_ann_count == 0 && method_ann_count == 0 && param_ann_count == 0) {
            continue;
        }
        
        uint32_t class_set_off = 0;
        if (c->annotation_count > 0) {
            uint32_t *item_offs = malloc(c->annotation_count * 4);
            for (uint32_t j = 0; j < c->annotation_count; j++) {
                item_offs[j] = write_annotation_item(ctx, &b, &c->annotations[j]);
                if (!first_annotation_item_off) first_annotation_item_off = item_offs[j];
                annotation_item_count++;
            }
            class_set_off = write_annotation_set_item(&b, item_offs, c->annotation_count);
            if (!first_annotation_set_item_off) first_annotation_set_item_off = class_set_off;
            annotation_set_item_count++;
            free(item_offs);
        }
        
        uint32_t *field_idxs = malloc(field_ann_count * 4);
        uint32_t *field_set_offs = malloc(field_ann_count * 4);
        uint32_t f_idx = 0;
        
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            if (c->static_fields[j].annotation_count > 0) {
                uint32_t *item_offs = malloc(c->static_fields[j].annotation_count * 4);
                for (uint32_t k = 0; k < c->static_fields[j].annotation_count; k++) {
                    item_offs[k] = write_annotation_item(ctx, &b, &c->static_fields[j].annotations[k]);
                    if (!first_annotation_item_off) first_annotation_item_off = item_offs[k];
                    annotation_item_count++;
                }
                field_set_offs[f_idx] = write_annotation_set_item(&b, item_offs, c->static_fields[j].annotation_count);
                if (!first_annotation_set_item_off) first_annotation_set_item_off = field_set_offs[f_idx];
                annotation_set_item_count++;
                char key[1024]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
                field_idxs[f_idx] = smali_pool_find(&ctx->fields, key);
                f_idx++;
                free(item_offs);
            }
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            if (c->instance_fields[j].annotation_count > 0) {
                uint32_t *item_offs = malloc(c->instance_fields[j].annotation_count * 4);
                for (uint32_t k = 0; k < c->instance_fields[j].annotation_count; k++) {
                    item_offs[k] = write_annotation_item(ctx, &b, &c->instance_fields[j].annotations[k]);
                    if (!first_annotation_item_off) first_annotation_item_off = item_offs[k];
                    annotation_item_count++;
                }
                field_set_offs[f_idx] = write_annotation_set_item(&b, item_offs, c->instance_fields[j].annotation_count);
                if (!first_annotation_set_item_off) first_annotation_set_item_off = field_set_offs[f_idx];
                annotation_set_item_count++;
                char key[1024]; snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
                field_idxs[f_idx] = smali_pool_find(&ctx->fields, key);
                f_idx++;
                free(item_offs);
            }
        }
        
        uint32_t *method_idxs = malloc(method_ann_count * 4);
        uint32_t *method_set_offs = malloc(method_ann_count * 4);
        uint32_t m_idx = 0;
        
        uint32_t *param_idxs = malloc(param_ann_count * 4);
        uint32_t *param_ref_offs = malloc(param_ann_count * 4);
        uint32_t p_idx_out = 0;
        
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                char key[1024]; snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, m_arr[j].name, m_arr[j].signature);
                uint32_t pool_m_idx = smali_pool_find(&ctx->methods, key);
                
                if (m_arr[j].annotation_count > 0) {
                    uint32_t *item_offs = malloc(m_arr[j].annotation_count * 4);
                    for (uint32_t k = 0; k < m_arr[j].annotation_count; k++) {
                        item_offs[k] = write_annotation_item(ctx, &b, &m_arr[j].annotations[k]);
                        if (!first_annotation_item_off) first_annotation_item_off = item_offs[k];
                        annotation_item_count++;
                    }
                    method_set_offs[m_idx] = write_annotation_set_item(&b, item_offs, m_arr[j].annotation_count);
                    if (!first_annotation_set_item_off) first_annotation_set_item_off = method_set_offs[m_idx];
                    annotation_set_item_count++;
                    method_idxs[m_idx] = pool_m_idx;
                    m_idx++;
                    free(item_offs);
                }
                
                int has_param = 0;
                if (m_arr[j].param_annotations) {
                    for (uint32_t p_idx = 0; p_idx < m_arr[j].param_annotation_cap; p_idx++) {
                        if (m_arr[j].param_annotation_counts[p_idx] > 0) has_param = 1;
                    }
                }
                if (has_param) {
                    uint32_t param_cnt = m_arr[j].param_annotation_cap;
                    uint32_t *set_offs = malloc(param_cnt * 4);
                    for (uint32_t p_idx = 0; p_idx < param_cnt; p_idx++) {
                        if (m_arr[j].param_annotation_counts[p_idx] > 0) {
                            uint32_t acnt = m_arr[j].param_annotation_counts[p_idx];
                            uint32_t *item_offs = malloc(acnt * 4);
                            for (uint32_t k = 0; k < acnt; k++) {
                                item_offs[k] = write_annotation_item(ctx, &b, &m_arr[j].param_annotations[p_idx][k]);
                                if (!first_annotation_item_off) first_annotation_item_off = item_offs[k];
                                annotation_item_count++;
                            }
                            set_offs[p_idx] = write_annotation_set_item(&b, item_offs, acnt);
                            if (!first_annotation_set_item_off) first_annotation_set_item_off = set_offs[p_idx];
                            annotation_set_item_count++;
                            free(item_offs);
                        } else {
                            set_offs[p_idx] = 0;
                        }
                    }
                    param_ref_offs[p_idx_out] = write_annotation_set_ref_list(&b, set_offs, param_cnt);
                    if (!first_annotation_set_ref_list_off) first_annotation_set_ref_list_off = param_ref_offs[p_idx_out];
                    annotation_set_ref_list_count++;
                    param_idxs[p_idx_out] = pool_m_idx;
                    p_idx_out++;
                    free(set_offs);
                }
            }
        }
        
        align_4(&b);
        annotations_dir_offsets[i] = b.len;
        if (!first_annotations_directory_off) first_annotations_directory_off = b.len;
        annotations_directory_count++;
        
        buf_write_u32(&b, class_set_off);
        buf_write_u32(&b, field_ann_count);
        buf_write_u32(&b, method_ann_count);
        buf_write_u32(&b, param_ann_count);
        
        for(uint32_t x=0; x<field_ann_count; x++) {
            for(uint32_t y=x+1; y<field_ann_count; y++) {
                if(field_idxs[x] > field_idxs[y]) {
                    uint32_t tmp = field_idxs[x]; field_idxs[x] = field_idxs[y]; field_idxs[y] = tmp;
                    tmp = field_set_offs[x]; field_set_offs[x] = field_set_offs[y]; field_set_offs[y] = tmp;
                }
            }
        }
        for (uint32_t j = 0; j < field_ann_count; j++) {
            buf_write_u32(&b, field_idxs[j]);
            buf_write_u32(&b, field_set_offs[j]);
        }
        
        for(uint32_t x=0; x<method_ann_count; x++) {
            for(uint32_t y=x+1; y<method_ann_count; y++) {
                if(method_idxs[x] > method_idxs[y]) {
                    uint32_t tmp = method_idxs[x]; method_idxs[x] = method_idxs[y]; method_idxs[y] = tmp;
                    tmp = method_set_offs[x]; method_set_offs[x] = method_set_offs[y]; method_set_offs[y] = tmp;
                }
            }
        }
        for (uint32_t j = 0; j < method_ann_count; j++) {
            buf_write_u32(&b, method_idxs[j]);
            buf_write_u32(&b, method_set_offs[j]);
        }
        
        for(uint32_t x=0; x<param_ann_count; x++) {
            for(uint32_t y=x+1; y<param_ann_count; y++) {
                if(param_idxs[x] > param_idxs[y]) {
                    uint32_t tmp = param_idxs[x]; param_idxs[x] = param_idxs[y]; param_idxs[y] = tmp;
                    tmp = param_ref_offs[x]; param_ref_offs[x] = param_ref_offs[y]; param_ref_offs[y] = tmp;
                }
            }
        }
        for (uint32_t j = 0; j < param_ann_count; j++) {
            buf_write_u32(&b, param_idxs[j]);
            buf_write_u32(&b, param_ref_offs[j]);
        }
        
        free(field_idxs); free(field_set_offs);
        free(method_idxs); free(method_set_offs);
        free(param_idxs); free(param_ref_offs);
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
        char shorty[512];
        int s_idx = 0;
        const char *close_paren = strchr(sig, ')');
        if (close_paren) {
            char ret_char = close_paren[1];
            shorty[s_idx++] = (ret_char == 'L' || ret_char == '[') ? 'L' : ret_char;
        } else {
            shorty[s_idx++] = 'V';
        }
        
        const char *p = sig;
        if (*p == '(') p++;
        while (p && *p && *p != ')') {
            if (*p == 'L') {
                shorty[s_idx++] = 'L';
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else if (*p == '[') {
                shorty[s_idx++] = 'L';
                while (*p == '[') p++;
                if (*p == 'L') {
                    while (*p && *p != ';') p++;
                    if (*p) p++;
                } else {
                    p++;
                }
            } else {
                shorty[s_idx++] = *p;
                p++;
            }
        }
        shorty[s_idx] = '\0';
        
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
        *(uint32_t *)(b.buf + def_off) = smali_pool_find(&ctx->types, c->descriptor);
        *(uint32_t *)(b.buf + def_off + 4) = c->access_flags;
        *(uint32_t *)(b.buf + def_off + 8) = c->super_class ? smali_pool_find(&ctx->types, c->super_class) : 0xFFFFFFFF;
        *(uint32_t *)(b.buf + def_off + 12) = interfaces_offsets[i];
        *(uint32_t *)(b.buf + def_off + 16) = c->source_file ? smali_pool_find(&ctx->strings, c->source_file) : 0xFFFFFFFF;
        *(uint32_t *)(b.buf + def_off + 20) = annotations_dir_offsets[i]; // annotations_off
        *(uint32_t *)(b.buf + def_off + 24) = class_data_offsets[i];
        *(uint32_t *)(b.buf + def_off + 28) = static_values_offsets[i]; // static_values_off
    }

    // Write Map List at the end
    align_4(&b);
    uint32_t map_off = b.len;

    // Collect all map entries
    map_item_entry_t entries[16];
    uint32_t entry_count = 0;

    // Header
    entries[entry_count++] = (map_item_entry_t){0x0000, 1, 0};
    
    // String IDs
    if (string_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0001, string_count, string_ids_off};
    }
    // Type IDs
    if (type_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0002, type_count, type_ids_off};
    }
    // Proto IDs
    if (proto_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0003, proto_count, proto_ids_off};
    }
    // Field IDs
    if (field_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0004, field_count, field_ids_off};
    }
    // Method IDs
    if (method_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0005, method_count, method_ids_off};
    }
    // Class Defs
    if (class_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x0006, class_count, class_defs_off};
    }

    // Now for data section types:
    // String Data Items
    if (string_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2002, string_count, string_offsets[0]};
    }

    // Type Lists (from proto params and interfaces)
    uint32_t first_type_list_off = 0;
    uint32_t type_list_count = 0;
    for (uint32_t i = 0; i < proto_count; i++) {
        if (proto_param_offsets[i] != 0) {
            if (first_type_list_off == 0 || proto_param_offsets[i] < first_type_list_off) {
                first_type_list_off = proto_param_offsets[i];
            }
            type_list_count++;
        }
    }
    for (uint32_t i = 0; i < class_count; i++) {
        if (interfaces_offsets[i] != 0) {
            if (first_type_list_off == 0 || interfaces_offsets[i] < first_type_list_off) {
                first_type_list_off = interfaces_offsets[i];
            }
            type_list_count++;
        }
    }
    if (type_list_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x1001, type_list_count, first_type_list_off};
    }

    // Code Items
    uint32_t first_code_off = 0;
    uint32_t total_code_count = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (uint32_t j = 0; j < c->direct_method_count; j++) {
            if (direct_code_offsets[i][j] != 0) {
                if (first_code_off == 0 || direct_code_offsets[i][j] < first_code_off) {
                    first_code_off = direct_code_offsets[i][j];
                }
                total_code_count++;
            }
        }
        for (uint32_t j = 0; j < c->virtual_method_count; j++) {
            if (virtual_code_offsets[i][j] != 0) {
                if (first_code_off == 0 || virtual_code_offsets[i][j] < first_code_off) {
                    first_code_off = virtual_code_offsets[i][j];
                }
                total_code_count++;
            }
        }
    }
    if (total_code_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2001, total_code_count, first_code_off};
    }

    // Class Data Items
    if (class_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2000, class_count, class_data_offsets[0]};
    }

    if (annotation_item_count > 0) entries[entry_count++] = (map_item_entry_t){0x2004, annotation_item_count, first_annotation_item_off};
    if (annotation_set_item_count > 0) entries[entry_count++] = (map_item_entry_t){0x1003, annotation_set_item_count, first_annotation_set_item_off};
    if (annotation_set_ref_list_count > 0) entries[entry_count++] = (map_item_entry_t){0x1002, annotation_set_ref_list_count, first_annotation_set_ref_list_off};
    if (annotations_directory_count > 0) entries[entry_count++] = (map_item_entry_t){0x2006, annotations_directory_count, first_annotations_directory_off};

    // Encoded Array Items (Static field initial values)
    uint32_t first_static_val_off = 0;
    uint32_t static_val_count = 0;
    for (uint32_t i = 0; i < class_count; i++) {
        if (static_values_offsets[i] != 0) {
            if (first_static_val_off == 0 || static_values_offsets[i] < first_static_val_off) {
                first_static_val_off = static_values_offsets[i];
            }
            static_val_count++;
        }
    }
    if (static_val_count > 0) {
        entries[entry_count++] = (map_item_entry_t){0x2005, static_val_count, first_static_val_off};
    }

    // Map List itself
    entries[entry_count++] = (map_item_entry_t){0x1000, 1, map_off};

    // Sort entries by offset (as required by DEX spec)
    qsort(entries, entry_count, sizeof(map_item_entry_t), comp_map_entries);

    // Write map_list to buffer
    buf_write_u32(&b, entry_count);
    for (uint32_t i = 0; i < entry_count; i++) {
        write_map_item(&b, entries[i].type, entries[i].size, entries[i].offset);
    }

    // Fill Header fields
    memcpy(b.buf, "dex\n035\0", 8);
    *(uint32_t *)(b.buf + 32) = b.len; // file_size
    *(uint32_t *)(b.buf + 36) = header_size;
    *(uint32_t *)(b.buf + 40) = 0x12345678; // endian_tag
    
    // Write table counts and offsets in header
    uint32_t header_offsets[14] = {
        string_count, string_ids_off,
        type_count, type_ids_off,
        proto_count, proto_ids_off,
        field_count, field_ids_off,
        method_count, method_ids_off,
        class_count, class_defs_off,
        b.len - data_off, data_off
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
        if (direct_code_ends[i]) free(direct_code_ends[i]);
        if (virtual_code_ends[i]) free(virtual_code_ends[i]);
    }
    free(direct_code_offsets); free(virtual_code_offsets);
    free(direct_code_ends); free(virtual_code_ends); free(class_data_offsets);
    return fp ? 0 : -1;
}
