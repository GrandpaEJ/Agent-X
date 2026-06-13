#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

char* execute_disasm_dex(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* cls_obj = cJSON_GetObjectItem(args, "class");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    uint32_t cls_idx = 0;
    if (cls_obj && cJSON_IsNumber(cls_obj)) cls_idx = (uint32_t)cJSON_GetNumberValue(cls_obj);
    else if (cls_obj && cJSON_IsString(cls_obj)) cls_idx = (uint32_t)atol(cls_obj->valuestring);
    FILE *fp = fopen(path_obj->valuestring, "rb");
    if (!fp) return strdup("{\"error\": \"Failed to open file\"}");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz > 10 * 1024 * 1024) { fclose(fp); return strdup("{\"error\": \"File too large\"}"); }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return strdup("{\"error\": \"Memory allocation failed\"}"); }
    fread(buf, 1, sz, fp);
    fclose(fp);

    dex_ctx *ctx = dex_parse(buf, sz);
    if (!ctx) { free(buf); return strdup("{\"error\": \"Invalid DEX file\"}"); }

    char *smali = dex_to_smali_class(ctx, cls_idx);
    dex_free(ctx);
    free(buf);
    if (!smali) return strdup("{\"error\": \"Failed to generate smali\"}");

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "smali", smali);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(smali);
    return res_str;
}

char* execute_smali_assemble(cJSON* args) {
    cJSON* src_dir_obj = cJSON_GetObjectItem(args, "src_dir");
    cJSON* out_dex_obj = cJSON_GetObjectItem(args, "out_dex");
    if (!src_dir_obj || !cJSON_IsString(src_dir_obj) || !out_dex_obj || !cJSON_IsString(out_dex_obj))
        return strdup("{\"error\": \"Missing src_dir or out_dex path\"}");
    int ret = smali_assemble(src_dir_obj->valuestring, out_dex_obj->valuestring);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", ret);
    if (ret == 0) {
        cJSON_AddStringToObject(res, "output", out_dex_obj->valuestring);
    } else {
        cJSON_AddStringToObject(res, "error", "Assembly failed");
    }
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_read_dex(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    FILE *fp = fopen(path_obj->valuestring, "rb");
    if (!fp) return strdup("{\"error\": \"Failed to open file\"}");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz > 10 * 1024 * 1024) { fclose(fp); return strdup("{\"error\": \"File too large\"}"); }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(fp); return strdup("{\"error\": \"Memory allocation failed\"}"); }
    fread(buf, 1, sz, fp);
    fclose(fp);

    dex_ctx *ctx = dex_parse(buf, sz);
    if (!ctx) { free(buf); return strdup("{\"error\": \"Invalid DEX file\"}"); }

    cJSON* out_dir_obj = cJSON_GetObjectItem(args, "out_dir");
    if (out_dir_obj && cJSON_IsString(out_dir_obj)) {
        const char *out_dir = out_dir_obj->valuestring;
        uint32_t ci = 0;
        while (1) {
            char *smali_code = dex_to_smali_class(ctx, ci);
            if (!smali_code) break;
            
            char *class_line = strdup(smali_code);
            char *nl = strchr(class_line, '\n');
            if (nl) *nl = '\0';
            
            char *semi = strchr(class_line, ';');
            if (semi) {
                *semi = '\0';
                char *desc = strrchr(class_line, 'L');
                if (desc) {
                    desc++; // Skip the 'L'
                    char filepath[1024];
                    snprintf(filepath, sizeof(filepath), "%s/%s.smali", out_dir, desc);
                    
                    char dirpath[1024];
                    snprintf(dirpath, sizeof(dirpath), "%s/%s.smali", out_dir, desc);
                    char *last_slash = strrchr(dirpath, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        char tmp[1024];
                        snprintf(tmp, sizeof(tmp), "%s", dirpath);
                        for (char *p = tmp + 1; *p; p++) {
                            if (*p == '/') {
                                *p = '\0';
                                #if defined(_WIN32)
                                mkdir(tmp);
                                #else
                                mkdir(tmp, 0777);
                                #endif
                                *p = '/';
                            }
                        }
                        #if defined(_WIN32)
                        mkdir(tmp);
                        #else
                        mkdir(tmp, 0777);
                        #endif
                    }
                    
                    FILE *out_fp = fopen(filepath, "w");
                    if (out_fp) {
                        fputs(smali_code, out_fp);
                        fclose(out_fp);
                    }
                }
            }
            free(class_line);
            free(smali_code);
            ci++;
        }
        
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "status", "success");
        cJSON_AddNumberToObject(res, "class_count", ci);
        char *res_str = cJSON_PrintUnformatted(res);
        cJSON_Delete(res);
        dex_free(ctx);
        free(buf);
        return res_str;
    }

    char *dump = dex_dump(ctx);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "dump", dump ? dump : "");
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(dump);
    dex_free(ctx);
    free(buf);
    return res_str;
}

