#include "format_arsc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint16_t r16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t r32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

static void w32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static const char* get_sp_string(const uint8_t *pool, uint32_t index) {
    if (!pool) return NULL;
    uint32_t string_count = r32(pool + 8);
    if (index >= string_count) return NULL;
    uint32_t flags = r32(pool + 16);
    uint32_t strings_start = r32(pool + 20);
    uint32_t hdr_size = r16(pool + 2);
    
    uint32_t off = r32(pool + hdr_size + index * 4);
    const uint8_t *p = pool + strings_start + off;
    
    static char buf[2048];
    if (flags & (1<<8)) {
        if (*p & 0x80) p += 2; else p += 1;
        if (*p & 0x80) p += 2; else p += 1;
        return (const char*)p;
    } else {
        uint16_t len;
        if (*p & 0x8000) { p += 4; } else { len = r16(p); p += 2; }
        char *out = buf;
        for (int i=0; i<len && i<2047; i++) {
            uint16_t c = r16(p + i*2);
            *out++ = (char)(c & 0x7F);
        }
        *out = 0;
        return buf;
    }
}

static void write_toml_string(FILE *f, const char *key, const char *val) {
    fprintf(f, "%s = \"", key);
    while (*val) {
        if (*val == '"') fprintf(f, "\\\"");
        else if (*val == '\\') fprintf(f, "\\\\");
        else if (*val == '\n') fprintf(f, "\\n");
        else if (*val == '\r') fprintf(f, "\\r");
        else fputc(*val, f);
        val++;
    }
    fprintf(f, "\"\n");
}

int arsc_dump_toml(const char *arsc_path, const char *toml_path) {
    FILE *f = fopen(arsc_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    arsc_ctx *ctx = arsc_parse(data, size);
    if (!ctx) { free(data); return -1; }

    FILE *out = fopen(toml_path, "w");
    if (!out) { arsc_free(ctx); free(data); return -1; }

    fprintf(out, "[global_string_pool]\n");
    for (uint32_t i = 0; i < ctx->string_count; i++) {
        const char *str = arsc_get_string(ctx, i);
        if (str) {
            fprintf(out, "str_%04X = \"", i);
            const char *val = str;
            while (*val) {
                if (*val == '"') fprintf(out, "\\\"");
                else if (*val == '\\') fprintf(out, "\\\\");
                else if (*val == '\n') fprintf(out, "\\n");
                else if (*val == '\r') fprintf(out, "\\r");
                else fputc(*val, out);
                val++;
            }
            fprintf(out, "\"\n");
        }
    }
    fprintf(out, "\n");

    uint32_t off = r16(data + 2);
    while (off + 8 <= size) {
        uint16_t type = r16(data + off);
        uint16_t hsize = r16(data + off + 2);
        uint32_t csize = r32(data + off + 4);
        
        if (type == RES_TABLE_PACKAGE_TYPE) {
            uint32_t typeStrings = r32(data + off + 268);
            uint32_t keyStrings = r32(data + off + 276);
            const uint8_t *type_sp = data + off + typeStrings;
            const uint8_t *key_sp = data + off + keyStrings;
            
            char pkg[256] = {0};
            for (int i=0; i<128; i++) {
                uint16_t c = r16(data + off + 12 + i*2);
                if (!c) break;
                pkg[i] = (char)c;
            }
            if (!pkg[0]) strcpy(pkg, "unknown");

            fprintf(out, "[package.\"%s\"]\n\n", pkg);

            uint32_t poff = off + hsize;
            while (poff + 8 <= off + csize) {
                uint16_t ptype = r16(data + poff);
                uint16_t phsize = r16(data + poff + 2);
                uint32_t pcsize = r32(data + poff + 4);
                
                if (ptype == RES_TABLE_TYPE_TYPE) {
                    uint8_t id = data[poff + 8];
                    const char *tname = get_sp_string(type_sp, id - 1);
                    if (tname) {
                        uint32_t entryCount = r32(data + poff + 12);
                        uint32_t entriesStart = r32(data + poff + 16);
                        
                        fprintf(out, "[package.\"%s\".%s.config_%X]\n", pkg, tname, poff);
                        
                        const uint32_t *offsets = (const uint32_t*)(data + poff + phsize);
                        const uint8_t *entries = data + poff + entriesStart;
                        
                        for (uint32_t i = 0; i < entryCount; i++) {
                            if (offsets[i] == 0xFFFFFFFF) continue;
                            const uint8_t *entry = entries + offsets[i];
                            uint16_t esize = r16(entry);
                            uint16_t eflags = r16(entry + 2);
                            uint32_t key_idx = r32(entry + 4);
                            
                            const char *kname = get_sp_string(key_sp, key_idx);
                            
                            if ((eflags & 0x0001) == 0 && kname) {
                                const uint8_t *val = entry + esize;
                                uint8_t dataType = val[3];
                                uint32_t dataVal = r32(val + 4);
                                
                                if (dataType == 0x03) {
                                    const char *str = arsc_get_string(ctx, dataVal);
                                    if (str) write_toml_string(out, kname, str);
                                } else if (dataType == 0x12) {
                                    fprintf(out, "%s = %s\n", kname, dataVal ? "true" : "false");
                                } else if (dataType >= 0x10 && dataType <= 0x11) {
                                    fprintf(out, "%s = %u\n", kname, dataVal);
                                } else if (dataType >= 0x1C && dataType <= 0x1F) {
                                    fprintf(out, "%s = \"#%08X\"\n", kname, dataVal);
                                } else if (dataType == 0x01) {
                                    fprintf(out, "%s = \"@ref/0x%08X\"\n", kname, dataVal);
                                } else if (dataType == 0x05) {
                                    fprintf(out, "%s = \"@dimen/0x%08X\"\n", kname, dataVal);
                                }
                            }
                        }
                        fprintf(out, "\n");
                    }
                }
                poff += pcsize;
            }
        }
        off += csize;
    }

    fclose(out);
    arsc_free(ctx);
    free(data);
    return 0;
}

static uint8_t* arsc_find_primitive(uint8_t *data, size_t size, const char *t_pkg, const char *t_type, uint32_t config_off, const char *t_key) {
    if (config_off + 8 > size) return NULL;
    uint16_t ptype = r16(data + config_off);
    uint16_t phsize = r16(data + config_off + 2);
    if (ptype != RES_TABLE_TYPE_TYPE) return NULL;
    
    // Reverse find the package header to get key_sp
    uint32_t root_off = r16(data + 2);
    uint32_t pkg_off = 0;
    while (root_off + 8 <= size) {
        if (root_off <= config_off && (root_off + r32(data + root_off + 4)) > config_off) {
            pkg_off = root_off;
            break;
        }
        root_off += r32(data + root_off + 4);
    }
    
    if (pkg_off == 0) return NULL;
    
    uint32_t keyStrings = r32(data + pkg_off + 276);
    const uint8_t *key_sp = data + pkg_off + keyStrings;
    
    uint32_t entryCount = r32(data + config_off + 12);
    uint32_t entriesStart = r32(data + config_off + 16);
    const uint32_t *offsets = (const uint32_t*)(data + config_off + phsize);
    uint8_t *entries = data + config_off + entriesStart;
    
    for (uint32_t i = 0; i < entryCount; i++) {
        if (offsets[i] == 0xFFFFFFFF) continue;
        uint8_t *entry = entries + offsets[i];
        uint16_t esize = r16(entry);
        uint16_t eflags = r16(entry + 2);
        uint32_t key_idx = r32(entry + 4);
        
        const char *kname = get_sp_string(key_sp, key_idx);
        if ((eflags & 0x0001) == 0 && kname && strcmp(kname, t_key) == 0) {
            return entry + esize; // returns pointer to Res_value
        }
    }
    return NULL;
}

static char* trim(char *s) {
    while(isspace((unsigned char)*s)) s++;
    if(*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while(end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int arsc_compile_toml(const char *arsc_path, const char *toml_path, const char *out_arsc) {
    FILE *f = fopen(arsc_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    arsc_ctx *ctx = arsc_parse(data, size);
    if (!ctx) { free(data); return -1; }

    FILE *t = fopen(toml_path, "r");
    if (!t) { arsc_free(ctx); free(data); return -1; }

    char line[4096];
    char cur_pkg[256] = {0};
    char cur_type[256] = {0};
    uint32_t cur_config = 0;

    while (fgets(line, sizeof(line), t)) {
        char *p = trim(line);
        if (p[0] == '#' || p[0] == '\0') continue;
        
        if (p[0] == '[') {
            char pkg[256] = {0}, type[256] = {0};
            uint32_t conf = 0;
            if (strcmp(p, "[global_string_pool]") == 0) {
                strcpy(cur_pkg, "GLOBAL");
                cur_type[0] = '\0';
            } else if (sscanf(p, "[package.\"%255[^\"]\".%255[^.].config_%x]", pkg, type, &conf) == 3) {
                strcpy(cur_pkg, pkg); strcpy(cur_type, type); cur_config = conf;
            }
        } else if (cur_pkg[0]) {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                char *k = trim(p);
                char *v = trim(eq + 1);
                
                if (strcmp(cur_pkg, "GLOBAL") == 0) {
                    uint32_t str_idx = 0;
                    if (sscanf(k, "str_%x", &str_idx) == 1 && v[0] == '"') {
                        v++;
                        int vlen = strlen(v);
                        if (vlen > 0 && v[vlen-1] == '"') v[vlen-1] = '\0';
                        char *write = v, *read = v;
                        while (*read) {
                            if (*read == '\\' && *(read+1)) {
                                read++;
                                if (*read == 'n') *write++ = '\n'; else if (*read == 'r') *write++ = '\r';
                                else if (*read == '\\') *write++ = '\\'; else if (*read == '"') *write++ = '"';
                                else *write++ = *read;
                            } else *write++ = *read;
                            read++;
                        }
                        *write = '\0';
                        printf("Patching GLOBAL string %u with '%s'\n", str_idx, v);
                        int ret = arsc_patch_string(ctx, str_idx, v);
                        printf("Patch ret = %d\n", ret);
                    } else {
                        printf("Failed to parse key: %s\n", k);
                    }
                } else if (cur_type[0]) {
                    uint8_t *val_ptr = arsc_find_primitive(data, size, cur_pkg, cur_type, cur_config, k);
                    if (val_ptr) {
                        uint8_t dataType = val_ptr[3];
                        if (dataType == 0x03 && v[0] == '"') {
                            v++;
                            int vlen = strlen(v);
                            if (vlen > 0 && v[vlen-1] == '"') v[vlen-1] = '\0';
                            char *write = v, *read = v;
                            while (*read) {
                                if (*read == '\\' && *(read+1)) {
                                    read++;
                                    if (*read == 'n') *write++ = '\n'; else if (*read == 'r') *write++ = '\r';
                                    else if (*read == '\\') *write++ = '\\'; else if (*read == '"') *write++ = '"';
                                    else *write++ = *read;
                                } else *write++ = *read;
                                read++;
                            }
                            *write = '\0';
                            arsc_patch_string(ctx, r32(val_ptr + 4), v);
                        } else if (dataType == 0x12) {
                            w32(val_ptr + 4, strcmp(v, "true") == 0 ? 0xFFFFFFFF : 0);
                        } else if ((dataType >= 0x10 && dataType <= 0x11) && isdigit(v[0])) {
                            w32(val_ptr + 4, (uint32_t)strtoul(v, NULL, 10));
                        } else if ((dataType >= 0x1C && dataType <= 0x1F) && v[0] == '"' && v[1] == '#') {
                            w32(val_ptr + 4, (uint32_t)strtoul(v + 2, NULL, 16));
                        } else if (dataType == 0x01 && v[0] == '"' && strncmp(v + 1, "@ref/0x", 7) == 0) {
                            w32(val_ptr + 4, (uint32_t)strtoul(v + 8, NULL, 16));
                        }
                    }
                }
            }
        }
    }
    fclose(t);

    size_t out_size = 0;
    uint8_t *new_data = arsc_build(ctx, &out_size);
    if (!new_data) {
        new_data = malloc(size);
        memcpy(new_data, data, size); // fallback if no strings were patched
        out_size = size;
    } else {
        // We must copy our primitive patches (which modified `data`) over to `new_data`
        // Wait, arsc_build reconstructs from `ctx->data` which points to `data`!
        // So the primitive patches in `data` are already included in `new_data`!
    }
    
    FILE *out = fopen(out_arsc, "wb");
    if (out) { fwrite(new_data, 1, out_size, out); fclose(out); }

    free(new_data);
    arsc_free(ctx);
    free(data);
    return 0;
}
