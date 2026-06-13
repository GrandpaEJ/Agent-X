#include "smali_debug.h"
#include "smali_pool.h"
#include <string.h>
#include <stdlib.h>

#define DBG_LINE_BASE (-4)
#define DBG_LINE_RANGE 15

uint32_t write_debug_info(smali_ctx_def_t *ctx, smali_buf_t *b, smali_method_def_t *m) {
    (void)ctx;
    int has_lines = 0, has_locals = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number >= 0) has_lines = 1;
    }
    if (m->local_count > 0) has_locals = 1;
    if (m->param_name_count == 0 && !has_lines && !m->prologue_set && !m->epilogue_set && !has_locals)
        return 0;

    align_4(b);
    uint32_t offset = b->len;

    int start_line = 1;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        if (m->insns[i].line_number > 0) {
            start_line = m->insns[i].line_number;
            break;
        }
    }

    buf_write_uleb128(b, (uint32_t)(start_line > 0 ? start_line : 1));
    buf_write_uleb128(b, m->param_name_count);
    for (uint32_t pi = 0; pi < m->param_name_count; pi++) {
        uint32_t idx = smali_pool_find(&ctx->strings, m->param_names[pi]);
        buf_write_uleb128(b, idx);
    }

    int cur_line = start_line;
    uint32_t cur_addr = 0;
    uint32_t insn_offsets[4096];
    for (uint32_t i = 0; i < m->insns_count && i < 4096; i++) {
        insn_offsets[i] = cur_addr;
        switch (m->insns[i].fmt) {
            case 0: case 1: case 21: case 2: case 3: case 4: cur_addr += 1; break;
            case 5: case 7: case 19: case 8: case 9: case 10: case 11: case 12: case 22: case 13: case 23: cur_addr += 2; break;
            case 6: case 14: case 15: case 16: case 18: case 24: case 25: cur_addr += 3; break;
            case 101: cur_addr += 4 + m->insns[i].payload_targets_count * 2; break;
            case 102: cur_addr += 2 + m->insns[i].payload_targets_count * 4; break;
            case 103: cur_addr += (m->insns[i].payload_data_len + 5) / 2 + 2; break;
            default: cur_addr += 1; break;
        }
    }

    if (m->prologue_set && m->insns_count > 0) {
        buf_write_u8(b, 0x07);
    }

    for (uint32_t li = 0; li < m->local_count; li++) {
        uint32_t reg = m->local_regs[li] & ~0x80000000;
        uint32_t name_idx = m->local_names[li] ? smali_pool_find(&ctx->strings, m->local_names[li]) : 0xFFFFFFFF;
        uint32_t type_idx = m->local_types[li] ? smali_pool_find(&ctx->types, m->local_types[li]) : 0xFFFFFFFF;
        if (name_idx == 0xFFFFFFFF) name_idx = 0;
        if (type_idx == 0xFFFFFFFF) type_idx = 0;

        uint32_t sig_idx = 0xFFFFFFFF;
        if (m->local_sigs[li]) sig_idx = smali_pool_find(&ctx->strings, m->local_sigs[li]);

        if (sig_idx != 0xFFFFFFFF) {
            buf_write_u8(b, 0x04);
            buf_write_uleb128(b, reg);
            buf_write_uleb128(b, name_idx);
            buf_write_uleb128(b, type_idx);
            buf_write_uleb128(b, sig_idx);
        } else {
            buf_write_u8(b, 0x03);
            buf_write_uleb128(b, reg);
            buf_write_uleb128(b, name_idx);
            buf_write_uleb128(b, type_idx);
        }
    }

    cur_line = start_line;
    cur_addr = 0;
    for (uint32_t i = 0; i < m->insns_count; i++) {
        uint32_t iaddr = insn_offsets[i];
        uint32_t addr_diff = iaddr - cur_addr;

        if (m->insns[i].line_number > 0) {
            int line_diff = m->insns[i].line_number - cur_line;

            while (addr_diff > 0 || line_diff != 0) {
                int use_special = 0;
                int adj = line_diff - DBG_LINE_BASE;
                if (adj >= 0 && addr_diff <= (255 - 0x0A) / DBG_LINE_RANGE) {
                    int max_adv = (255 - 0x0A) / DBG_LINE_RANGE;
                    if (addr_diff <= (uint32_t)max_adv && adj == line_diff - DBG_LINE_BASE) {
                        use_special = 1;
                    }
                }

                if (use_special && addr_diff > 0) {
                    uint8_t special = 0x0A + (addr_diff * DBG_LINE_RANGE + (line_diff - DBG_LINE_BASE));
                    buf_write_u8(b, special);
                    cur_line = m->insns[i].line_number;
                    cur_addr = iaddr;
                    addr_diff = 0;
                    line_diff = 0;
                } else if (addr_diff > 0) {
                    buf_write_u8(b, 0x01);
                    buf_write_uleb128(b, addr_diff);
                    cur_addr = iaddr;
                    addr_diff = 0;
                } else if (line_diff != 0) {
                    buf_write_u8(b, 0x02);
                    buf_write_sleb128(b, (int32_t)line_diff);
                    cur_line = m->insns[i].line_number;
                    line_diff = 0;
                } else {
                    break;
                }
            }
        } else if (addr_diff > 0) {
            buf_write_u8(b, 0x01);
            buf_write_uleb128(b, addr_diff);
            cur_addr = iaddr;
        }
    }

    buf_write_u8(b, 0x00);
    (void)has_locals;
    (void)m->epilogue_set;
    return offset;
}
