#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT(_op, _fmt, _kind, _name) {_op, _fmt, _kind, _name}

typedef enum {
    K_NONE, K_STRING, K_TYPE, K_FIELD, K_METHOD, K_PROTO
} ref_kind;

typedef enum {
    F_10x, F_12x, F_11n, F_11x, F_10t, F_20t, F_30t,
    F_21c, F_22c, F_21t, F_22t, F_21s, F_22b, F_23x,
    F_35c, F_3rc, F_31i, F_51l, F_31t, F_21h, F_22s,
    F_22cs, F_35ms, F_3rms,
} insn_fmt;

typedef struct { uint8_t op, fmt, kind; const char *name; } op_entry;

static const op_entry optab[] = {
    FMT(0x00, F_10x, K_NONE, "nop"),
    FMT(0x01, F_12x, K_NONE, "move"),
    FMT(0x02, F_12x, K_NONE, "move-wide"),
    FMT(0x03, F_12x, K_NONE, "move-object"),
    FMT(0x0A, F_11x, K_NONE, "move-result"),
    FMT(0x0B, F_11x, K_NONE, "move-result-wide"),
    FMT(0x0C, F_11x, K_NONE, "move-result-object"),
    FMT(0x0D, F_11x, K_NONE, "move-exception"),
    FMT(0x0E, F_10x, K_NONE, "return-void"),
    FMT(0x0F, F_11x, K_NONE, "return"),
    FMT(0x10, F_11x, K_NONE, "return-wide"),
    FMT(0x11, F_11x, K_NONE, "return-object"),
    FMT(0x12, F_11n, K_NONE, "const/4"),
    FMT(0x13, F_21s, K_NONE, "const/16"),
    FMT(0x14, F_31i, K_NONE, "const"),
    FMT(0x15, F_21s, K_NONE, "const/high16"),
    FMT(0x1A, F_21c, K_STRING, "const-string"),
    FMT(0x1B, F_21c, K_STRING, "const-string-jumbo"),
    FMT(0x1C, F_21c, K_TYPE, "const-class"),
    FMT(0x1D, F_11x, K_NONE, "monitor-enter"),
    FMT(0x1E, F_11x, K_NONE, "monitor-exit"),
    FMT(0x1F, F_21c, K_TYPE, "check-cast"),
    FMT(0x20, F_22c, K_TYPE, "instance-of"),
    FMT(0x21, F_12x, K_NONE, "array-length"),
    FMT(0x22, F_21c, K_TYPE, "new-instance"),
    FMT(0x23, F_22c, K_TYPE, "new-array"),
    FMT(0x24, F_35c, K_TYPE, "filled-new-array"),
    FMT(0x25, F_3rc, K_TYPE, "filled-new-array/range"),
    FMT(0x26, F_31t, K_NONE, "fill-array-data"),
    FMT(0x27, F_11x, K_NONE, "throw"),
    FMT(0x28, F_10t, K_NONE, "goto"),
    FMT(0x29, F_20t, K_NONE, "goto/16"),
    FMT(0x2A, F_30t, K_NONE, "goto/32"),
    FMT(0x2B, F_31t, K_NONE, "packed-switch"),
    FMT(0x2C, F_31t, K_NONE, "sparse-switch"),
    FMT(0x2D, F_23x, K_NONE, "cmpl-float"),
    FMT(0x2E, F_23x, K_NONE, "cmpg-float"),
    FMT(0x2F, F_23x, K_NONE, "cmpl-double"),
    FMT(0x30, F_23x, K_NONE, "cmpg-double"),
    FMT(0x31, F_23x, K_NONE, "cmp-long"),
    FMT(0x32, F_22t, K_NONE, "if-eq"),
    FMT(0x33, F_22t, K_NONE, "if-ne"),
    FMT(0x34, F_22t, K_NONE, "if-lt"),
    FMT(0x35, F_22t, K_NONE, "if-ge"),
    FMT(0x36, F_22t, K_NONE, "if-gt"),
    FMT(0x37, F_22t, K_NONE, "if-le"),
    FMT(0x38, F_21t, K_NONE, "if-eqz"),
    FMT(0x39, F_21t, K_NONE, "if-nez"),
    FMT(0x3A, F_21t, K_NONE, "if-ltz"),
    FMT(0x3B, F_21t, K_NONE, "if-gez"),
    FMT(0x3C, F_21t, K_NONE, "if-gtz"),
    FMT(0x3D, F_21t, K_NONE, "if-lez"),
    FMT(0x44, F_23x, K_NONE, "aget"),
    FMT(0x45, F_23x, K_NONE, "aget-wide"),
    FMT(0x46, F_23x, K_NONE, "aget-object"),
    FMT(0x47, F_23x, K_NONE, "aget-boolean"),
    FMT(0x48, F_23x, K_NONE, "aget-byte"),
    FMT(0x49, F_23x, K_NONE, "aget-char"),
    FMT(0x4A, F_23x, K_NONE, "aget-short"),
    FMT(0x4B, F_23x, K_NONE, "aput"),
    FMT(0x4C, F_23x, K_NONE, "aput-wide"),
    FMT(0x4D, F_23x, K_NONE, "aput-object"),
    FMT(0x4E, F_23x, K_NONE, "aput-boolean"),
    FMT(0x4F, F_23x, K_NONE, "aput-byte"),
    FMT(0x50, F_23x, K_NONE, "aput-char"),
    FMT(0x51, F_23x, K_NONE, "aput-short"),
    FMT(0x52, F_22c, K_FIELD, "iget"),
    FMT(0x53, F_22c, K_FIELD, "iget-wide"),
    FMT(0x54, F_22c, K_FIELD, "iget-object"),
    FMT(0x55, F_22c, K_FIELD, "iget-boolean"),
    FMT(0x56, F_22c, K_FIELD, "iget-byte"),
    FMT(0x57, F_22c, K_FIELD, "iget-char"),
    FMT(0x58, F_22c, K_FIELD, "iget-short"),
    FMT(0x59, F_22c, K_FIELD, "iput"),
    FMT(0x5A, F_22c, K_FIELD, "iput-wide"),
    FMT(0x5B, F_22c, K_FIELD, "iput-object"),
    FMT(0x5C, F_22c, K_FIELD, "iput-boolean"),
    FMT(0x5D, F_22c, K_FIELD, "iput-byte"),
    FMT(0x5E, F_22c, K_FIELD, "iput-char"),
    FMT(0x5F, F_22c, K_FIELD, "iput-short"),
    FMT(0x60, F_21c, K_FIELD, "sget"),
    FMT(0x61, F_21c, K_FIELD, "sget-wide"),
    FMT(0x62, F_21c, K_FIELD, "sget-object"),
    FMT(0x63, F_21c, K_FIELD, "sget-boolean"),
    FMT(0x64, F_21c, K_FIELD, "sget-byte"),
    FMT(0x65, F_21c, K_FIELD, "sget-char"),
    FMT(0x66, F_21c, K_FIELD, "sget-short"),
    FMT(0x67, F_21c, K_FIELD, "sput"),
    FMT(0x68, F_21c, K_FIELD, "sput-wide"),
    FMT(0x69, F_21c, K_FIELD, "sput-object"),
    FMT(0x6A, F_21c, K_FIELD, "sput-boolean"),
    FMT(0x6B, F_21c, K_FIELD, "sput-byte"),
    FMT(0x6C, F_21c, K_FIELD, "sput-char"),
    FMT(0x6D, F_21c, K_FIELD, "sput-short"),
    FMT(0x6E, F_35c, K_METHOD, "invoke-virtual"),
    FMT(0x6F, F_35c, K_METHOD, "invoke-super"),
    FMT(0x70, F_35c, K_METHOD, "invoke-direct"),
    FMT(0x71, F_35c, K_METHOD, "invoke-static"),
    FMT(0x72, F_35c, K_METHOD, "invoke-interface"),
    FMT(0x74, F_3rc, K_METHOD, "invoke-virtual/range"),
    FMT(0x75, F_3rc, K_METHOD, "invoke-super/range"),
    FMT(0x76, F_3rc, K_METHOD, "invoke-direct/range"),
    FMT(0x77, F_3rc, K_METHOD, "invoke-static/range"),
    FMT(0x78, F_3rc, K_METHOD, "invoke-interface/range"),
    FMT(0xD0, F_22s, K_NONE, "add-int/lit16"),
    FMT(0xD8, F_22b, K_NONE, "add-int/lit8"),
};

static const op_entry *find_op(uint8_t op) {
    for (size_t i = 0; i < sizeof(optab)/sizeof(optab[0]); i++)
        if (optab[i].op == op) return &optab[i];
    return NULL;
}

// Decode an instruction at insns[offset]. Returns number of 16-bit units consumed.
int dex_decode_insn(const uint16_t *insns, int offset, uint32_t *out_op,
                    uint32_t *out_vA, uint32_t *out_vB, uint32_t *out_vC,
                    uint32_t *out_ref, int32_t *out_lit) {
    uint16_t word = insns[offset];
    uint8_t op = word & 0xFF;
    uint8_t arg = (word >> 8) & 0xFF;
    uint8_t nib0 = word & 0x0F;
    (void)nib0;
    const op_entry *e = find_op(op);
    if (!e) return -1;

    *out_op = op;
    uint32_t vA = 0, vB = 0, vC = 0, ref = 0;
    int32_t lit = 0;
    int words = 1;

    switch (e->fmt) {
    case F_10x:
        break;
    case F_12x: {
        uint8_t nA = (arg >> 4) & 0x0F;
        uint8_t nB = arg & 0x0F;
        vA = nA; vB = nB;
        break;
    }
    case F_11n: {
        uint8_t nA = (arg >> 4) & 0x0F;
        int8_t nB = (int8_t)(arg << 4) >> 4; // sign-extend 4-bit
        vA = nA; lit = nB;
        break;
    }
    case F_11x:
        vA = arg;
        break;
    case F_10t: {
        int8_t off = (int8_t)arg;
        lit = off;
        break;
    }
    case F_20t: {
        int16_t off = (int16_t)insns[offset + 1];
        lit = off; words = 2;
        break;
    }
    case F_30t: {
        uint32_t off = (uint32_t)insns[offset + 1] | ((uint32_t)insns[offset + 2] << 16);
        lit = (int32_t)off; words = 3;
        break;
    }
    case F_21c:
    case F_21t:
    case F_21s:
    case F_21h: {
        vA = arg;
        ref = insns[offset + 1];
        words = 2;
        break;
    }
    case F_22c:
    case F_22t:
    case F_22s: {
        vA = (arg >> 4) & 0x0F;
        vB = arg & 0x0F;
        ref = insns[offset + 1];
        words = 2;
        break;
    }
    case F_22b: {
        vA = arg;
        vB = insns[offset + 1] & 0xFF;
        lit = (int8_t)((insns[offset + 1] >> 8) & 0xFF);
        words = 2;
        break;
    }
    case F_23x: {
        vA = arg;
        vB = insns[offset + 1] & 0xFF;
        vC = (insns[offset + 1] >> 8) & 0xFF;
        words = 2;
        break;
    }
    case F_35c: {
        uint16_t w1 = insns[offset + 1];
        uint16_t w2 = insns[offset + 2];
        vA = (arg >> 4) & 0x0F; // A = argument count (bits 12-15)
        vC = arg & 0x0F;        // C = first argument register (bits 8-11)
        ref = w1;               // reference index
        vB = w2 & 0xFF;         // D (low nibble), E (high nibble)
        lit = (int16_t)(w2 >> 8); // F (low nibble), G (high nibble)
        words = 3;
        break;
    }
    case F_3rc: {
        vA = arg;        // number of arguments
        ref = insns[offset + 1];
        vC = insns[offset + 2];
        words = 3;
        break;
    }
    case F_31i: {
        vA = arg;
        uint32_t lo = insns[offset + 1];
        uint32_t hi = insns[offset + 2];
        lit = (int32_t)(lo | (hi << 16));
        words = 3;
        break;
    }
    case F_51l: {
        vA = arg;
        uint64_t val = 0;
        for (int i = 0; i < 4; i++)
            val |= (uint64_t)insns[offset + 1 + i] << (i * 16);
        lit = (int32_t)(val & 0xFFFFFFFF);
        words = 5;
        break;
    }
    case F_31t:
        vA = arg;
        ref = (uint32_t)insns[offset + 1] | ((uint32_t)insns[offset + 2] << 16);
        words = 3;
        break;
    case F_22cs:
    case F_35ms:
    case F_3rms:
        vA = arg;
        words = 2;
        break;
    }

    *out_vA = vA; *out_vB = vB; *out_vC = vC;
    *out_ref = ref; *out_lit = lit;
    return words;
}

const char *dex_insn_name(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->name : NULL;
}

int dex_insn_kind(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->kind : 0;
}

int dex_insn_fmt(uint8_t op) {
    const op_entry *e = find_op(op);
    return e ? e->fmt : -1;
}
