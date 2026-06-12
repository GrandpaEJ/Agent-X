#ifndef SMALI_INTERNAL_H
#define SMALI_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

uint32_t adler32(const uint8_t *data, size_t len);
void smali_sha1(const uint8_t *data, size_t len, uint8_t *out);

#endif
