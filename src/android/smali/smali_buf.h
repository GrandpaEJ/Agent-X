#ifndef SMALI_BUF_H
#define SMALI_BUF_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buf;
    uint32_t len;
    uint32_t cap;
} smali_buf_t;

void buf_init(smali_buf_t *b);
void buf_free(smali_buf_t *b);
void buf_write(smali_buf_t *b, const void *data, uint32_t size);
void buf_write_u32(smali_buf_t *b, uint32_t val);
void buf_write_u16(smali_buf_t *b, uint16_t val);
void buf_write_u8(smali_buf_t *b, uint8_t val);
void buf_write_uleb128(smali_buf_t *b, uint32_t val);
void buf_write_sleb128(smali_buf_t *b, int32_t val);
void align_4(smali_buf_t *b);

#endif
