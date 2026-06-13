#include "smali_buf.h"
#include <stdlib.h>
#include <string.h>

void buf_init(smali_buf_t *b) {
    b->buf = malloc(4096); b->len = 0; b->cap = 4096;
}

void buf_free(smali_buf_t *b) {
    free(b->buf);
    b->buf = NULL; b->len = 0; b->cap = 0;
}

void buf_write(smali_buf_t *b, const void *data, uint32_t size) {
    if (b->len + size > b->cap) {
        uint32_t old_cap = b->cap;
        while (b->len + size > b->cap) b->cap *= 2;
        b->buf = realloc(b->buf, b->cap);
        memset(b->buf + old_cap, 0, b->cap - old_cap);
    }
    memcpy(b->buf + b->len, data, size);
    b->len += size;
}

void buf_write_u32(smali_buf_t *b, uint32_t val) { buf_write(b, &val, 4); }
void buf_write_u16(smali_buf_t *b, uint16_t val) { buf_write(b, &val, 2); }
void buf_write_u8(smali_buf_t *b, uint8_t val) { buf_write(b, &val, 1); }

void buf_write_uleb128(smali_buf_t *b, uint32_t val) {
    uint8_t buf[5]; int len = 0;
    do {
        uint8_t byte = val & 0x7F; val >>= 7;
        if (val) byte |= 0x80;
        buf[len++] = byte;
    } while (val);
    buf_write(b, buf, len);
}

void buf_write_sleb128(smali_buf_t *b, int32_t val) {
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

void align_4(smali_buf_t *b) {
    uint32_t rem = b->len % 4;
    if (rem) {
        uint8_t pad[4] = {0};
        buf_write(b, pad, 4 - rem);
    }
}
