#include "smali_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void smali_pool_init(smali_pool_strings_t *p) {
    p->strings = NULL; p->count = 0; p->cap = 0;
}

void smali_pool_free(smali_pool_strings_t *p) {
    for (uint32_t i = 0; i < p->count; i++) free(p->strings[i]);
    free(p->strings); p->strings = NULL; p->count = 0; p->cap = 0;
}

uint32_t smali_pool_add(smali_pool_strings_t *p, const char *str) {
    if (!str) return 0xFFFFFFFF;
    for (uint32_t i = 0; i < p->count; i++) {
        if (strcmp(p->strings[i], str) == 0) return i;
    }
    if (p->count >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->strings = realloc(p->strings, p->cap * sizeof(char *));
    }
    p->strings[p->count] = strdup(str);
    return p->count++;
}

uint32_t smali_pool_find(smali_pool_strings_t *p, const char *str) {
    if (!str) return 0xFFFFFFFF;
    for (uint32_t i = 0; i < p->count; i++) {
        if (strcmp(p->strings[i], str) == 0) return i;
    }
    return 0xFFFFFFFF;
}

static int comp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void smali_pool_sort_strings(smali_pool_strings_t *p) {
    qsort(p->strings, p->count, sizeof(char *), comp_str);
}

static void add_sig_types(smali_pool_strings_t *strings, const char *sig) {
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') {
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
            else p++;
        } else {
            p++;
        }
        char *type = strndup(start, p - start);
        smali_pool_add(strings, type);
        free(type);
    }
    if (*p == ')') p++;
    if (*p) {
        smali_pool_add(strings, p);
    }
}

static void add_shorty_string(smali_pool_strings_t *strings, const char *sig) {
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
    smali_pool_add(strings, shorty);
}

static smali_ctx_def_t *s_sort_ctx = NULL;

static int comp_proto_sig(const void *a, const void *b) {
    const char *sigA = *(const char **)a;
    const char *sigB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    const char *closeA = strchr(sigA, ')');
    const char *closeB = strchr(sigB, ')');
    const char *retA = closeA ? closeA + 1 : "V";
    const char *retB = closeB ? closeB + 1 : "V";
    
    uint32_t typeA = smali_pool_find(&ctx->types, retA);
    uint32_t typeB = smali_pool_find(&ctx->types, retB);
    if (typeA != typeB) return (typeA < typeB) ? -1 : 1;
    
    uint32_t ptypesA[64], ptypesB[64];
    uint32_t countA = 0, countB = 0;
    
    const char *p = sigA;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
        else if (*p == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; } else p++; }
        else p++;
        char *tstr = strndup(start, p - start);
        ptypesA[countA++] = smali_pool_find(&ctx->types, tstr);
        free(tstr);
    }
    
    p = sigB;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        const char *start = p;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; }
        else if (*p == '[') { while (*p == '[') p++; if (*p == 'L') { while (*p && *p != ';') p++; if (*p) p++; } else p++; }
        else p++;
        char *tstr = strndup(start, p - start);
        ptypesB[countB++] = smali_pool_find(&ctx->types, tstr);
        free(tstr);
    }
    
    uint32_t min_count = (countA < countB) ? countA : countB;
    for (uint32_t i = 0; i < min_count; i++) {
        if (ptypesA[i] != ptypesB[i]) {
            return (ptypesA[i] < ptypesB[i]) ? -1 : 1;
        }
    }
    if (countA != countB) {
        return (countA < countB) ? -1 : 1;
    }
    return 0;
}

static int comp_field_key(const void *a, const void *b) {
    const char *keyA = *(const char **)a;
    const char *keyB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    char *arrowA = strstr(keyA, "->");
    char *colonA = strchr(keyA, ':');
    char *clsA = strndup(keyA, arrowA - keyA);
    char *nameA = strndup(arrowA + 2, colonA - (arrowA + 2));
    char *typeA = colonA + 1;
    
    char *arrowB = strstr(keyB, "->");
    char *colonB = strchr(keyB, ':');
    char *clsB = strndup(keyB, arrowB - keyB);
    char *nameB = strndup(arrowB + 2, colonB - (arrowB + 2));
    char *typeB = colonB + 1;
    
    uint32_t cIdxA = smali_pool_find(&ctx->types, clsA);
    uint32_t cIdxB = smali_pool_find(&ctx->types, clsB);
    free(clsA); free(clsB);
    
    if (cIdxA != cIdxB) {
        free(nameA); free(nameB);
        return (cIdxA < cIdxB) ? -1 : 1;
    }
    
    uint32_t nIdxA = smali_pool_find(&ctx->strings, nameA);
    uint32_t nIdxB = smali_pool_find(&ctx->strings, nameB);
    free(nameA); free(nameB);
    
    if (nIdxA != nIdxB) {
        return (nIdxA < nIdxB) ? -1 : 1;
    }
    
    uint32_t tIdxA = smali_pool_find(&ctx->types, typeA);
    uint32_t tIdxB = smali_pool_find(&ctx->types, typeB);
    return (tIdxA < tIdxB) ? -1 : 1;
}

static int comp_method_key(const void *a, const void *b) {
    const char *keyA = *(const char **)a;
    const char *keyB = *(const char **)b;
    smali_ctx_def_t *ctx = s_sort_ctx;
    char *arrowA = strstr(keyA, "->");
    char *parenA = strchr(keyA, '(');
    char *clsA = strndup(keyA, arrowA - keyA);
    char *nameA = strndup(arrowA + 2, parenA - (arrowA + 2));
    char *protoA = parenA;
    
    char *arrowB = strstr(keyB, "->");
    char *parenB = strchr(keyB, '(');
    char *clsB = strndup(keyB, arrowB - keyB);
    char *nameB = strndup(arrowB + 2, parenB - (arrowB + 2));
    char *protoB = parenB;
    
    uint32_t cIdxA = smali_pool_find(&ctx->types, clsA);
    uint32_t cIdxB = smali_pool_find(&ctx->types, clsB);
    free(clsA); free(clsB);
    
    if (cIdxA != cIdxB) {
        free(nameA); free(nameB);
        return (cIdxA < cIdxB) ? -1 : 1;
    }
    
    uint32_t nIdxA = smali_pool_find(&ctx->strings, nameA);
    uint32_t nIdxB = smali_pool_find(&ctx->strings, nameB);
    free(nameA); free(nameB);
    
    if (nIdxA != nIdxB) {
        return (nIdxA < nIdxB) ? -1 : 1;
    }
    
    uint32_t pIdxA = smali_pool_find(&ctx->protos, protoA);
    uint32_t pIdxB = smali_pool_find(&ctx->protos, protoB);
    return (pIdxA < pIdxB) ? -1 : 1;
}

static void ensure_names_in_strings(smali_ctx_def_t *ctx) {
    if (ctx->fields.count > 100000 || ctx->methods.count > 100000) return;
    for (uint32_t i = 0; i < ctx->fields.count; i++) {
        const char *key = ctx->fields.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        char *colon = strchr(key, ':');
        if (arrow && colon && colon > arrow + 2) {
            char *name = strndup(arrow + 2, colon - (arrow + 2));
            if (name && name[0]) {
                smali_pool_add(&ctx->strings, name);
            }
            free(name);
        }
    }
    for (uint32_t i = 0; i < ctx->methods.count; i++) {
        const char *key = ctx->methods.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        if (arrow) {
            char *colon = strchr(key, ':');
            char *paren = strchr(key, '(');
            if (colon && colon > arrow + 2) {
                char *name = strndup(arrow + 2, colon - (arrow + 2));
                if (name && name[0]) {
                    smali_pool_add(&ctx->strings, name);
                }
                free(name);
            } else if (paren && paren > arrow + 2) {
                char *name = strndup(arrow + 2, paren - (arrow + 2));
                if (name && name[0]) {
                    smali_pool_add(&ctx->strings, name);
                }
                free(name);
            }
        }
    }
}

void smali_pool_build_all(smali_ctx_def_t *ctx) {
    smali_pool_init(&ctx->strings);
    smali_pool_init(&ctx->types);
    smali_pool_init(&ctx->protos);
    smali_pool_init(&ctx->fields);
    smali_pool_init(&ctx->methods);

    // Seed primitive types
    const char *prims[] = {"V", "Z", "B", "S", "C", "I", "J", "F", "D"};
    for (int i = 0; i < 9; i++) smali_pool_add(&ctx->strings, prims[i]);

    // 1. Gather all raw strings from class headers and instructions
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        smali_pool_add(&ctx->strings, c->descriptor);
        if (c->super_class) smali_pool_add(&ctx->strings, c->super_class);
        if (c->source_file) smali_pool_add(&ctx->strings, c->source_file);
        for (uint32_t j = 0; j < c->interface_count; j++) smali_pool_add(&ctx->strings, c->interfaces[j]);

        for (uint32_t j = 0; j < c->static_field_count; j++) {
            smali_pool_add(&ctx->strings, c->static_fields[j].name);
            smali_pool_add(&ctx->strings, c->static_fields[j].type);
            if (c->static_fields[j].has_init_value) {
                smali_field_def_t *f = &c->static_fields[j];
                if (f->value_type == VALUE_TYPE_STRING && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
                else if (f->value_type == VALUE_TYPE_TYPE && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
                else if (f->value_type == VALUE_TYPE_ENUM && f->value_str)
                    smali_pool_add(&ctx->strings, f->value_str);
            }
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            smali_pool_add(&ctx->strings, c->instance_fields[j].name);
            smali_pool_add(&ctx->strings, c->instance_fields[j].type);
        }
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                smali_method_def_t *m = &m_arr[j];
                smali_pool_add(&ctx->strings, m->name);
                smali_pool_add(&ctx->strings, m->signature);
                add_sig_types(&ctx->strings, m->signature);
                add_shorty_string(&ctx->strings, m->signature);

                for (uint32_t c_idx = 0; c_idx < m->catches_count; c_idx++) {
                    if (m->catches[c_idx].type) {
                        smali_pool_add(&ctx->strings, m->catches[c_idx].type);
                    }
                }

                for (uint32_t k = 0; k < m->insns_count; k++) {
                    smali_insn_t *ins = &m->insns[k];
                    if (ins->ref_str) {
                        char *arrow = strstr(ins->ref_str, "->");
                        if (arrow) {
                            char *colon = strchr(ins->ref_str, ':');
                            char *paren = strchr(ins->ref_str, '(');
                            char *class_part = strndup(ins->ref_str, arrow - ins->ref_str);
                            smali_pool_add(&ctx->strings, class_part);
                            if (colon) {
                                char *name_part = strndup(arrow + 2, colon - (arrow + 2));
                                smali_pool_add(&ctx->strings, name_part);
                                smali_pool_add(&ctx->strings, colon + 1);
                                free(name_part);
                            } else if (paren) {
                                char *name_part = strndup(arrow + 2, paren - (arrow + 2));
                                smali_pool_add(&ctx->strings, name_part);
                                smali_pool_add(&ctx->strings, paren);
                                add_sig_types(&ctx->strings, paren);
                                add_shorty_string(&ctx->strings, paren);
                                free(name_part);
                            }
                            free(class_part);
                        } else {
                            smali_pool_add(&ctx->strings, ins->ref_str);
                        }
                    }
                }
            }
        }
    }

    smali_pool_sort_strings(&ctx->strings);

    // 2. Build Type Pool
    for (uint32_t i = 0; i < ctx->strings.count; i++) {
        const char *s = ctx->strings.strings[i];
        if ((s[0] == 'L' && strchr(s, ';') && !strstr(s, "->")) || s[0] == '[' || 
            (strlen(s) == 1 && (s[0] == 'V' || s[0] == 'Z' || s[0] == 'B' || s[0] == 'S' || 
                                s[0] == 'C' || s[0] == 'I' || s[0] == 'J' || s[0] == 'F' || s[0] == 'D'))) {
            smali_pool_add(&ctx->types, s);
        }
    }

    // 3. Build Proto/Field/Method Pools
    for (uint32_t i = 0; i < ctx->class_count; i++) {
        smali_class_def_t *c = &ctx->classes[i];
        for (int mtype = 0; mtype < 2; mtype++) {
            uint32_t mc = (mtype == 0) ? c->direct_method_count : c->virtual_method_count;
            smali_method_def_t *m_arr = (mtype == 0) ? c->direct_methods : c->virtual_methods;
            for (uint32_t j = 0; j < mc; j++) {
                smali_method_def_t *m = &m_arr[j];
                char key[1024];
                snprintf(key, sizeof(key), "%s->%s%s", c->descriptor, m->name, m->signature);
                smali_pool_add(&ctx->methods, key);
                smali_pool_add(&ctx->protos, m->signature);

                for (uint32_t k = 0; k < m->insns_count; k++) {
                    smali_insn_t *ins = &m->insns[k];
                    if (ins->ref_str) {
                        if (ins->kind == 3) {
                            smali_pool_add(&ctx->fields, ins->ref_str);
                        } else if (ins->kind == 4) {
                            smali_pool_add(&ctx->methods, ins->ref_str);
                            char *paren = strchr(ins->ref_str, '(');
                            if (paren) {
                                smali_pool_add(&ctx->protos, paren);
                            }
                        }
                    }
                }
            }
        }
        for (uint32_t j = 0; j < c->static_field_count; j++) {
            char key[1024];
            snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->static_fields[j].name, c->static_fields[j].type);
            smali_pool_add(&ctx->fields, key);
        }
        for (uint32_t j = 0; j < c->instance_field_count; j++) {
            char key[1024];
            snprintf(key, sizeof(key), "%s->%s:%s", c->descriptor, c->instance_fields[j].name, c->instance_fields[j].type);
            smali_pool_add(&ctx->fields, key);
        }
    }

    /* Add names from field/method pools to strings for writer lookups */
    for (uint32_t i = 0; i < ctx->fields.count; i++) {
        const char *key = ctx->fields.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        char *colon = strchr(key, ':');
        if (arrow && colon && colon > arrow + 2) {
            char *name = strndup(arrow + 2, colon - (arrow + 2));
            if (name && name[0]) smali_pool_add(&ctx->strings, name);
            free(name);
        }
    }
    for (uint32_t i = 0; i < ctx->methods.count; i++) {
        const char *key = ctx->methods.strings[i];
        if (!key) continue;
        char *arrow = strstr(key, "->");
        if (arrow) {
            char *colon = strchr(key, ':');
            char *paren = strchr(key, '(');
            if (colon && colon > arrow + 2) {
                char *name = strndup(arrow + 2, colon - (arrow + 2));
                if (name && name[0]) smali_pool_add(&ctx->strings, name);
                free(name);
            } else if (paren && paren > arrow + 2) {
                char *name = strndup(arrow + 2, paren - (arrow + 2));
                if (name && name[0]) smali_pool_add(&ctx->strings, name);
                free(name);
            }
        }
    }

    smali_pool_sort_strings(&ctx->strings);
    smali_pool_sort_strings(&ctx->types);

    s_sort_ctx = ctx;
    qsort(ctx->protos.strings, ctx->protos.count, sizeof(char *), comp_proto_sig);
    qsort(ctx->fields.strings, ctx->fields.count, sizeof(char *), comp_field_key);
    qsort(ctx->methods.strings, ctx->methods.count, sizeof(char *), comp_method_key);
    s_sort_ctx = NULL;
}
