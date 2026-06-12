#ifndef SMALI_POOL_H
#define SMALI_POOL_H

#include "smali_types.h"

void smali_pool_init(smali_pool_strings_t *p);
void smali_pool_free(smali_pool_strings_t *p);
uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str);
uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str);
void smali_pool_sort_strings(smali_pool_strings_t *p);
void smali_pool_build_all(smali_ctx_def_t *ctx);

/* Hash table for O(1) string pool lookups */
#define POOL_HASH_SIZE 4096

struct smali_pool_entry {
    uint32_t idx;
    smali_pool_entry_t *next;
};

#endif
