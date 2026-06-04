#include "smali_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int smali_parse_method_body(smali_ctx_def_t *ctx, smali_method_def_t *m, char **p);

static uint32_t parse_access_flags(const char *tok) {
    if (strcmp(tok, "public") == 0) return 0x0001;
    if (strcmp(tok, "private") == 0) return 0x0002;
    if (strcmp(tok, "protected") == 0) return 0x0004;
    if (strcmp(tok, "static") == 0) return 0x0008;
    if (strcmp(tok, "final") == 0) return 0x0010;
    if (strcmp(tok, "synchronized") == 0) return 0x0020;
    if (strcmp(tok, "bridge") == 0) return 0x0040;
    if (strcmp(tok, "varargs") == 0) return 0x0080;
    if (strcmp(tok, "native") == 0) return 0x0100;
    if (strcmp(tok, "interface") == 0) return 0x0200;
    if (strcmp(tok, "abstract") == 0) return 0x0400;
    if (strcmp(tok, "synthetic") == 0) return 0x1000;
    if (strcmp(tok, "annotation") == 0) return 0x2000;
    if (strcmp(tok, "enum") == 0) return 0x4000;
    if (strcmp(tok, "constructor") == 0) return 0x10000; // Custom bit to indicate constructor
    return 0;
}

static uint32_t calculate_ins_count(const char *sig, uint32_t access_flags) {
    uint32_t count = 0;
    if (!(access_flags & 0x0008)) {
        count++; // 'this' pointer
    }
    const char *p = sig;
    if (*p == '(') p++;
    while (*p && *p != ')') {
        if (*p == 'J' || *p == 'D') {
            count += 2;
            p++;
        } else if (*p == 'L') {
            count += 1;
            while (*p && *p != ';') p++;
            if (*p) p++;
        } else if (*p == '[') {
            count += 1;
            while (*p == '[') p++;
            if (*p == 'L') {
                while (*p && *p != ';') p++;
                if (*p) p++;
            } else {
                p++;
            }
        } else {
            count += 1;
            p++;
        }
    }
    return count;
}

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text) {
    char *text_copy = strdup(text);
    char *p = text_copy;
    smali_class_def_t *curr = NULL;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        char *line_start = p;
        char *line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        
        if (*line_start == '#') {
            p = line_end;
            continue;
        }

        char *tok = smali_next_token(&p);
        if (!tok) {
            p = line_end;
            continue;
        }

        if (strcmp(tok, ".class") == 0) {
            if (ctx->class_count >= ctx->class_cap) {
                ctx->class_cap = ctx->class_cap ? ctx->class_cap * 2 : 16;
                ctx->classes = realloc(ctx->classes, ctx->class_cap * sizeof(smali_class_def_t));
            }
            curr = &ctx->classes[ctx->class_count++];
            memset(curr, 0, sizeof(smali_class_def_t));
            
            char *next = smali_next_token(&p);
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    curr->access_flags |= flags;
                    free(next);
                } else {
                    curr->descriptor = next;
                    break;
                }
                next = smali_next_token(&p);
            }
        } else if (strcmp(tok, ".super") == 0 && curr) {
            curr->super_class = smali_next_token(&p);
        } else if (strcmp(tok, ".source") == 0 && curr) {
            curr->source_file = smali_next_token(&p);
        } else if (strcmp(tok, ".implements") == 0 && curr) {
            char *iface = smali_next_token(&p);
            if (iface) {
                curr->interfaces = realloc(curr->interfaces, (curr->interface_count + 1) * sizeof(char *));
                curr->interfaces[curr->interface_count++] = iface;
            }
        } else if (strcmp(tok, ".field") == 0 && curr) {
            uint32_t access_flags = 0;
            char *next = smali_next_token(&p);
            char *name_type = NULL;
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    access_flags |= flags;
                    free(next);
                } else {
                    name_type = next;
                    break;
                }
                next = smali_next_token(&p);
            }
            if (name_type) {
                char *colon = strchr(name_type, ':');
                if (colon) {
                    *colon = '\0';
                    char *name = strdup(name_type);
                    char *type = strdup(colon + 1);
                    smali_field_def_t *field;
                    if (access_flags & 0x0008) {
                        if (curr->static_field_count >= curr->static_field_cap) {
                            curr->static_field_cap = curr->static_field_cap ? curr->static_field_cap * 2 : 8;
                            curr->static_fields = realloc(curr->static_fields, curr->static_field_cap * sizeof(smali_field_def_t));
                        }
                        field = &curr->static_fields[curr->static_field_count++];
                    } else {
                        if (curr->instance_field_count >= curr->instance_field_cap) {
                            curr->instance_field_cap = curr->instance_field_cap ? curr->instance_field_cap * 2 : 8;
                            curr->instance_fields = realloc(curr->instance_fields, curr->instance_field_cap * sizeof(smali_field_def_t));
                        }
                        field = &curr->instance_fields[curr->instance_field_count++];
                    }
                    uint32_t init_val = 0;
                    int has_init = 0;
                    char *eq_tok = smali_next_token(&p);
                    if (eq_tok) {
                        if (strcmp(eq_tok, "=") == 0) {
                            char *val_tok = smali_next_token(&p);
                            if (val_tok) {
                                init_val = (uint32_t)strtoul(val_tok, NULL, 0);
                                has_init = 1;
                                free(val_tok);
                            }
                        }
                        free(eq_tok);
                    }
                    field->access_flags = access_flags;
                    field->name = name;
                    field->type = type;
                    field->init_value = init_val;
                    field->has_init_value = has_init;
                }
                free(name_type);
            }
        } else if (strcmp(tok, ".method") == 0 && curr) {
            uint32_t access_flags = 0;
            char *next = smali_next_token(&p);
            char *name_sig = NULL;
            while (next) {
                uint32_t flags = parse_access_flags(next);
                if (flags) {
                    access_flags |= flags;
                    free(next);
                } else {
                    name_sig = next;
                    break;
                }
                next = smali_next_token(&p);
            }
            if (name_sig) {
                char *paren = strchr(name_sig, '(');
                if (paren) {
                    char *name = strndup(name_sig, paren - name_sig);
                    // Trim carriage returns or spaces from name
                    size_t name_l = strlen(name);
                    while (name_l > 0 && (name[name_l - 1] == '\r' || name[name_l - 1] == ' ' || name[name_l - 1] == '\t')) {
                        name[name_l - 1] = '\0';
                        name_l--;
                    }
                    char *sig = strdup(paren);
                    // Trim carriage returns or spaces from sig
                    size_t sig_l = strlen(sig);
                    while (sig_l > 0 && (sig[sig_l - 1] == '\r' || sig[sig_l - 1] == ' ' || sig[sig_l - 1] == '\t')) {
                        sig[sig_l - 1] = '\0';
                        sig_l--;
                    }
                    smali_method_def_t *method;
                    int is_direct = ((access_flags & 0x0008) || (access_flags & 0x0002) || strcmp(name, "<init>") == 0 || strcmp(name, "<clinit>") == 0 || (access_flags & 0x10000));
                    if (is_direct) {
                        if (curr->direct_method_count >= curr->direct_method_cap) {
                            curr->direct_method_cap = curr->direct_method_cap ? curr->direct_method_cap * 2 : 8;
                            curr->direct_methods = realloc(curr->direct_methods, curr->direct_method_cap * sizeof(smali_method_def_t));
                        }
                        method = &curr->direct_methods[curr->direct_method_count++];
                    } else {
                        if (curr->virtual_method_count >= curr->virtual_method_cap) {
                            curr->virtual_method_cap = curr->virtual_method_cap ? curr->virtual_method_cap * 2 : 8;
                            curr->virtual_methods = realloc(curr->virtual_methods, curr->virtual_method_cap * sizeof(smali_method_def_t));
                        }
                        method = &curr->virtual_methods[curr->virtual_method_count++];
                    }
                    memset(method, 0, sizeof(smali_method_def_t));
                    method->access_flags = access_flags;
                    method->name = name;
                    method->signature = sig;
                    method->ins_count = calculate_ins_count(sig, method->access_flags);
                    
                    smali_parse_method_body(ctx, method, &p);
                }
                free(name_sig);
            }
        } else if (strcmp(tok, ".annotation") == 0) {
            // Skip the entire annotation block
            int depth = 1;
            free(tok);
            while (depth > 0 && *p) {
                char *line_end_inner = strchr(p, '\n');
                if (!line_end_inner) line_end_inner = p + strlen(p);
                char *p_inner = p;
                char *tok_inner = smali_next_token(&p_inner);
                if (tok_inner) {
                    if (strcmp(tok_inner, ".annotation") == 0) {
                        depth++;
                    } else if (strcmp(tok_inner, ".end") == 0) {
                        char *next_inner = smali_next_token(&p_inner);
                        if (next_inner && strcmp(next_inner, "annotation") == 0) {
                            depth--;
                        }
                        if (next_inner) free(next_inner);
                    }
                    free(tok_inner);
                }
                p = line_end_inner;
                if (*p == '\n') p++;
            }
            continue;
        }
        int is_method = (strcmp(tok, ".method") == 0);
        free(tok);
        if (!is_method) {
            p = line_end;
        }
    }
    free(text_copy);
    return 0;
}

int parse_smali_file_path(smali_ctx_def_t *ctx, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    size_t read_bytes = fread(buf, 1, sz, fp);
    buf[read_bytes] = '\0';
    fclose(fp);
    int ret = parse_smali_file_content(ctx, buf);
    free(buf);
    return ret;
}

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

int collect_smali_files(const char *dir, char ***files, int *count) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s%c%s", dir, PATH_SEP, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            collect_smali_files(path, files, count);
        } else if (strstr(ent->d_name, ".smali")) {
            (*files) = (char **)realloc(*files, (*count + 1) * sizeof(char *));
            (*files)[*count] = strdup(path);
            (*count)++;
        }
    }
    closedir(d);
    return 0;
}
