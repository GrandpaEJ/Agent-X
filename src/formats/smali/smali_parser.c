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

smali_encoded_value_t *smali_parse_encoded_value(char **p);

smali_annotation_t *smali_parse_annotation(char **p) {
    char *visibility_tok = smali_next_token(p);
    char *type_tok = smali_next_token(p);
    
    if (!visibility_tok || !type_tok) return NULL;
    
    smali_annotation_t *annot = calloc(1, sizeof(smali_annotation_t));
    if (strcmp(visibility_tok, "build") == 0) annot->visibility = 0;
    else if (strcmp(visibility_tok, "runtime") == 0) annot->visibility = 1;
    else if (strcmp(visibility_tok, "system") == 0) annot->visibility = 2;
    
    annot->type = strdup(type_tok);
    if (visibility_tok) free(visibility_tok);
    if (type_tok) free(type_tok);
    
    while (**p) {
        while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
        if (!**p) break;
        
        char *tok = smali_next_token(p);
        if (!tok) continue;
        
        if (strcmp(tok, ".end") == 0) {
            char *next = smali_next_token(p);
            if (next) free(next);
            free(tok);
            break;
        }
        
        char *eq_tok = smali_next_token(p);
        if (eq_tok && strcmp(eq_tok, "=") == 0) {
            smali_encoded_value_t *val = smali_parse_encoded_value(p);
            if (val) {
                if (annot->element_count >= annot->element_cap) {
                    annot->element_cap = annot->element_cap ? annot->element_cap * 2 : 4;
                    annot->elements = realloc(annot->elements, annot->element_cap * sizeof(smali_annotation_element_t));
                }
                annot->elements[annot->element_count].name = strdup(tok);
                annot->elements[annot->element_count].value = *val;
                annot->element_count++;
                free(val);
            }
        }
        if (eq_tok) free(eq_tok);
        free(tok);
    }
    return annot;
}

smali_encoded_value_t *smali_parse_encoded_value(char **p) {
    char *start = *p;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    
    if (*start == '"') {
        char *str = smali_parse_string_literal(&start);
        *p = start;
        smali_encoded_value_t *val = calloc(1, sizeof(smali_encoded_value_t));
        val->type = 0x17; // String
        val->v_string = str;
        return val;
    } else if (*start == '{') {
        start++;
        smali_encoded_value_t *val = calloc(1, sizeof(smali_encoded_value_t));
        val->type = 0x1c; // Array
        while (*start && *start != '}') {
            char *tmp = start;
            smali_encoded_value_t *elem = smali_parse_encoded_value(&tmp);
            if (elem) {
                if (val->v_array.count == 0) val->v_array.elements = malloc(sizeof(smali_encoded_value_t));
                else val->v_array.elements = realloc(val->v_array.elements, (val->v_array.count + 1) * sizeof(smali_encoded_value_t));
                val->v_array.elements[val->v_array.count++] = *elem;
                free(elem);
            }
            start = tmp;
            while (*start && (*start == ' ' || *start == '\t' || *start == ',' || *start == '\n' || *start == '\r')) start++;
        }
        if (*start == '}') start++;
        *p = start;
        return val;
    } else {
        char *tok = smali_next_token(&start);
        *p = start;
        if (!tok) return NULL;
        
        smali_encoded_value_t *val = calloc(1, sizeof(smali_encoded_value_t));
        if (strcmp(tok, ".subannotation") == 0) {
            val->type = 0x1d; // Annotation
            val->v_annotation = smali_parse_annotation(p);
        } else if (strcmp(tok, ".enum") == 0) {
            val->type = 0x1b; // Enum
            char *enum_val = smali_next_token(p);
            if (enum_val) {
                val->v_string = strdup(enum_val);
                printf("PARSED ENUM: %s\n", enum_val);
                free(enum_val);
            }
        } else if (strcmp(tok, "true") == 0) {
            val->type = 0x1f; // Boolean
            val->v_int = 1;
        } else if (strcmp(tok, "false") == 0) {
            val->type = 0x1f;
            val->v_int = 0;
        } else if (strcmp(tok, "null") == 0) {
            val->type = 0x1e; // Null
        } else if (strchr(tok, '(') && strstr(tok, "->")) {
            val->type = 0x1a; // Method
            val->v_string = strdup(tok);
        } else if (strchr(tok, ':') && strstr(tok, "->")) {
            val->type = 0x19; // Field
            val->v_string = strdup(tok);
        } else if (tok[0] == 'L' && strchr(tok, ';')) {
            val->type = 0x18; // Type
            val->v_string = strdup(tok);
        } else if ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '-' || tok[0] == '+') {
            size_t len = strlen(tok);
            char last_c = tok[len-1];
            if (last_c == 'L' || last_c == 'l') {
                val->type = 0x06; // Long
                tok[len-1] = '\0';
                val->v_int = strtoull(tok, NULL, 0);
            } else if (last_c == 'f' || last_c == 'F') {
                val->type = 0x10; // Float
                tok[len-1] = '\0';
                float f = strtof(tok, NULL);
                uint32_t fv; memcpy(&fv, &f, 4);
                val->v_int = fv;
            } else if (last_c == 'd' || last_c == 'D') {
                val->type = 0x11; // Double
                tok[len-1] = '\0';
                double d = strtod(tok, NULL);
                uint64_t dv; memcpy(&dv, &d, 8);
                val->v_int = dv;
            } else if (last_c == 't' || last_c == 'T') {
                val->type = 0x00; // Byte
                tok[len-1] = '\0';
                val->v_int = strtoul(tok, NULL, 0);
            } else if (last_c == 's' || last_c == 'S') {
                val->type = 0x02; // Short
                tok[len-1] = '\0';
                val->v_int = strtoul(tok, NULL, 0);
            } else if (last_c == 'c' || last_c == 'C') {
                val->type = 0x03; // Char
                tok[len-1] = '\0';
                val->v_int = strtoul(tok, NULL, 0);
            } else {
                val->type = 0x04; // Int
                val->v_int = strtoul(tok, NULL, 0);
            }
        }
        free(tok);
        return val;
    }
}

int parse_smali_file_content(smali_ctx_def_t *ctx, const char *text) {
    char *text_copy = strdup(text);
    char *p = text_copy;
    smali_class_def_t *curr = NULL;
    smali_field_def_t *last_field = NULL;

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
                    uint64_t init_val = 0;
                    int has_init = 0;
                    char *init_string = NULL;
                    char *eq_tok = smali_next_token(&p);
                    if (eq_tok) {
                        if (strcmp(eq_tok, "=") == 0) {
                            char *start = p;
                            while (start && (*start == ' ' || *start == '\t' || *start == '\r')) start++;
                            int is_str = (start && *start == '"');
                            char *val_tok = smali_next_token(&p);
                            if (val_tok) {
                                if (is_str) {
                                    init_string = val_tok;
                                    has_init = 1;
                                } else if (strcmp(val_tok, "null") == 0) {
                                    has_init = 1;
                                    free(val_tok);
                                } else if (strcmp(val_tok, "true") == 0) {
                                    init_val = 1;
                                    has_init = 1;
                                    free(val_tok);
                                } else if (strcmp(val_tok, "false") == 0) {
                                    init_val = 0;
                                    has_init = 1;
                                    free(val_tok);
                                } else {
                                    size_t len = strlen(val_tok);
                                    char last_c = val_tok[len-1];
                                    if (type[0] == 'F') {
                                        if (last_c == 'f' || last_c == 'F') val_tok[len-1] = '\0';
                                        float f = strtof(val_tok, NULL);
                                        uint32_t fv; memcpy(&fv, &f, 4);
                                        init_val = fv;
                                    } else if (type[0] == 'D') {
                                        if (last_c == 'd' || last_c == 'D') val_tok[len-1] = '\0';
                                        double d = strtod(val_tok, NULL);
                                        uint64_t dv; memcpy(&dv, &d, 8);
                                        init_val = dv;
                                    } else {
                                        if ((last_c >= 'a' && last_c <= 'z') || (last_c >= 'A' && last_c <= 'Z')) {
                                            if (val_tok[0] != '0' || (val_tok[1] != 'x' && val_tok[1] != 'X')) {
                                                // Only strip suffix if it's not a hex string, or if we know it's a suffix.
                                                // Wait, long suffix 'L' or 's' or 't'
                                                if (last_c == 'L' || last_c == 'l' || last_c == 't' || last_c == 'T' || last_c == 's' || last_c == 'S' || last_c == 'c' || last_c == 'C') {
                                                    val_tok[len-1] = '\0';
                                                }
                                            } else if (last_c == 'L' || last_c == 'l' || last_c == 't' || last_c == 'T' || last_c == 's' || last_c == 'S') {
                                                if (last_c != 'F' && last_c != 'f' && last_c != 'd' && last_c != 'D' && last_c != 'c' && last_c != 'C' && last_c != 'a' && last_c != 'A' && last_c != 'b' && last_c != 'B' && last_c != 'e' && last_c != 'E') {
                                                    val_tok[len-1] = '\0';
                                                }
                                            }
                                        }
                                        init_val = (uint64_t)strtoull(val_tok, NULL, 0);
                                        // printf("PARSED FIELD %s val=%s -> %llu\n", name, val_tok, (unsigned long long)init_val);
                                    }
                                    has_init = 1;
                                    free(val_tok);
                                }
                            }
                        }
                        free(eq_tok);
                    }
                    field->access_flags = access_flags;
                    field->name = name;
                    field->type = type;
                    field->init_value = init_val;
                    field->has_init_value = has_init;
                    field->init_string = init_string;
                    last_field = field;
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
            smali_annotation_t *annot = smali_parse_annotation(&p);
            if (annot) {
                if (last_field) {
                    if (last_field->annotation_count >= last_field->annotation_cap) {
                        last_field->annotation_cap = last_field->annotation_cap ? last_field->annotation_cap * 2 : 4;
                        last_field->annotations = realloc(last_field->annotations, last_field->annotation_cap * sizeof(smali_annotation_t));
                    }
                    last_field->annotations[last_field->annotation_count++] = *annot;
                } else if (curr) {
                    if (curr->annotation_count >= curr->annotation_cap) {
                        curr->annotation_cap = curr->annotation_cap ? curr->annotation_cap * 2 : 4;
                        curr->annotations = realloc(curr->annotations, curr->annotation_cap * sizeof(smali_annotation_t));
                    }
                    curr->annotations[curr->annotation_count++] = *annot;
                }
                free(annot);
            }
            free(tok);
            continue;
        } else if (strcmp(tok, ".end") == 0) {
            char *next = smali_next_token(&p);
            if (next) {
                if (strcmp(next, "field") == 0) last_field = NULL;
                free(next);
            }
            free(tok);
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
