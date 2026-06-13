#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static int collect_files(const char *base, char ***paths, int *count) {
    DIR *d = opendir(base);
    struct dirent *ent;
    if (!d) return -1;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p[4096];
        snprintf(p, sizeof(p), "%s/%s", base, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            collect_files(p, paths, count);
        } else {
            (*paths) = realloc(*paths, (*count + 1) * sizeof(char *));
            (*paths)[*count] = strdup(p);
            (*count)++;
        }
    }
    closedir(d);
    return 0;
}

char* execute_read_axml(cJSON* args) {
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

    axml_ctx *ctx = axml_parse(buf, sz);
    free(buf);
    if (!ctx) return strdup("{\"error\": \"Invalid AXML file\"}");

    char *xml = axml_get_xml(ctx);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "xml", xml ? xml : "");
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    axml_free(ctx);
    return res_str;
}

char* execute_axml_assemble(cJSON* args) {
    cJSON *src_obj = cJSON_GetObjectItem(args, "src");
    cJSON *out_obj = cJSON_GetObjectItem(args, "out");
    if (!src_obj || !cJSON_IsString(src_obj) || !out_obj || !cJSON_IsString(out_obj)) {
        return strdup("{\"error\": \"Missing src or out path\"}");
    }
    
    int ret = axml_assemble(src_obj->valuestring, out_obj->valuestring);
    if (ret != 0) {
        return strdup("{\"error\": \"Failed to assemble AXML\"}");
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"status\": 0, \"output\": \"%s\"}", out_obj->valuestring);
    return strdup(buf);
}

char* execute_decode_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* out_dir_obj = cJSON_GetObjectItem(args, "out_dir");
    if (!path_obj || !cJSON_IsString(path_obj) || !out_dir_obj || !cJSON_IsString(out_dir_obj))
        return strdup("{\"error\": \"Missing path or out_dir\"}");
    
    int ret = apk_decode(path_obj->valuestring, out_dir_obj->valuestring);
    if (ret != 0) return strdup("{\"error\": \"Failed to decode APK\"}");
    
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"status\": \"success\", \"output_dir\": \"%s\"}", out_dir_obj->valuestring);
    return strdup(buf);
}

char* execute_build_apk(cJSON* args) {
    cJSON* src_dir_obj = cJSON_GetObjectItem(args, "src_dir");
    cJSON* out_apk_obj = cJSON_GetObjectItem(args, "out_apk");
    cJSON* key_path_obj = cJSON_GetObjectItem(args, "key_path");
    cJSON* cert_path_obj = cJSON_GetObjectItem(args, "cert_path");
    
    if (!src_dir_obj || !cJSON_IsString(src_dir_obj) || !out_apk_obj || !cJSON_IsString(out_apk_obj))
        return strdup("{\"error\": \"Missing src_dir or out_apk\"}");
        
    const char *key_path = (key_path_obj && cJSON_IsString(key_path_obj)) ? key_path_obj->valuestring : NULL;
    const char *cert_path = (cert_path_obj && cJSON_IsString(cert_path_obj)) ? cert_path_obj->valuestring : NULL;

    int ret = apk_build(src_dir_obj->valuestring, out_apk_obj->valuestring, key_path, cert_path);
    if (ret != 0) return strdup("{\"error\": \"Failed to build APK\"}");
    
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"status\": \"success\", \"output_apk\": \"%s\"}", out_apk_obj->valuestring);
    return strdup(buf);
}

char* execute_analyze_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    char *dump = apk_analyze(path_obj->valuestring);
    if (!dump) return strdup("{\"error\": \"Failed to analyze APK\"}");
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "dump", dump);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    apk_free(dump);
    return res_str;
}

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

char* execute_repack_apk(cJSON* args) {
    cJSON* dir_obj = cJSON_GetObjectItem(args, "dir");
    cJSON* out_obj = cJSON_GetObjectItem(args, "output");
    if (!dir_obj || !cJSON_IsString(dir_obj) || !out_obj || !cJSON_IsString(out_obj))
        return strdup("{\"error\": \"Missing dir or output path\"}");
    zip_writer *zw = zip_writer_open(out_obj->valuestring);
    if (!zw) return strdup("{\"error\": \"Failed to open output APK\"}");
    char **files = NULL;
    int count = 0;
    collect_files(dir_obj->valuestring, &files, &count);
    for (int i = 0; i < count; i++) {
        const char *path = files[i];
        const char *name = path + strlen(dir_obj->valuestring) + 1;
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *buf = malloc(sz);
        fread(buf, 1, sz, fp);
        fclose(fp);
        zip_writer_add(zw, name, buf, sz, 1, 0);
        free(buf);
        free(files[i]);
    }
    free(files);
    zip_writer_close(zw);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", out_obj->valuestring);
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_resign_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    
    char out_path[4096];
    snprintf(out_path, sizeof(out_path), "%s.signed", path_obj->valuestring);

    cJSON* cert_obj = cJSON_GetObjectItem(args, "cert");
    cJSON* key_obj = cJSON_GetObjectItem(args, "key");
    const char *cert_path = (cert_obj && cJSON_IsString(cert_obj)) ? cert_obj->valuestring : NULL;
    const char *key_path = (key_obj && cJSON_IsString(key_obj)) ? key_obj->valuestring : "testkey.pem";

    rsa_key key;
    if (rsa_load_key(key_path, &key) != 0) {
        char err[512];
        snprintf(err, sizeof(err), "{\"error\": \"Failed to load private key: %s\"}", key_path);
        return strdup(err);
    }
    
    uint8_t *custom_cert = NULL;
    size_t custom_cert_len = 0;
    
    if (cert_path) {
        FILE *f = fopen(cert_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            custom_cert_len = ftell(f);
            fseek(f, 0, SEEK_SET);
            custom_cert = malloc(custom_cert_len);
            if (custom_cert) {
                size_t read_bytes = fread(custom_cert, 1, custom_cert_len, f);
                (void)read_bytes;
            }
            fclose(f);
        } else {
            char err[512];
            snprintf(err, sizeof(err), "{\"error\": \"Failed to load custom certificate: %s\"}", cert_path);
            return strdup(err);
        }
    }

    cJSON* scheme_obj = cJSON_GetObjectItem(args, "scheme");
    const char *scheme = (scheme_obj && cJSON_IsString(scheme_obj)) ? scheme_obj->valuestring : "v1";
    int do_v1 = strstr(scheme, "v1") != NULL;
    int do_v2 = strstr(scheme, "v2") != NULL;
    int do_v3 = strstr(scheme, "v3") != NULL;
    if (!do_v1 && !do_v2 && !do_v3) do_v1 = 1; // default
    
    if (custom_cert && do_v1) {
        printf("[WARN] V1 signing does not currently support dynamic certificates. Disabling V1 and using V2/V3 only.\n");
        do_v1 = 0;
        if (!do_v2 && !do_v3) {
            do_v2 = 1; // default to v2 if they requested custom key without schemes
        }
    }
    
    int needs_v2_v3 = do_v2 || do_v3;

    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.apk", path_obj->valuestring);
    
    if (do_v1) {
        if (apk_sign_v1(path_obj->valuestring, needs_v2_v3 ? tmp_path : out_path, &key, do_v2, do_v3) != 0) {
            return strdup("{\"error\": \"Native APK v1 signing failed\"}");
        }
    }
    
    if (needs_v2_v3) {
        const char *in_file = do_v1 ? tmp_path : path_obj->valuestring;
        if (apk_sign_v2_v3(in_file, out_path, &key, do_v2, do_v3, custom_cert, custom_cert_len) != 0) {
            if (custom_cert) free(custom_cert);
            if (do_v1) remove(tmp_path);
            return strdup("{\"error\": \"Native APK v2/v3 signing failed\"}");
        }
        if (do_v1) remove(tmp_path);
    }
    
    if (custom_cert) free(custom_cert);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddStringToObject(res, "output", out_path);
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

char* execute_zipalign(cJSON* args) {
    cJSON* in_obj = cJSON_GetObjectItem(args, "in");
    cJSON* out_obj = cJSON_GetObjectItem(args, "out");
    cJSON* align_obj = cJSON_GetObjectItem(args, "align");
    
    if (!in_obj || !cJSON_IsString(in_obj) || !out_obj || !cJSON_IsString(out_obj))
        return strdup("{\"error\": \"Missing 'in' or 'out' path\"}");
        
    int alignment = 4;
    if (align_obj && cJSON_IsNumber(align_obj)) {
        alignment = align_obj->valueint;
    }
    
    if (zipalign_file(in_obj->valuestring, out_obj->valuestring, alignment) == 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "status", "success");
        cJSON_AddStringToObject(res, "output", out_obj->valuestring);
        char *res_str = cJSON_PrintUnformatted(res);
        cJSON_Delete(res);
        return res_str;
    } else {
        return strdup("{\"error\": \"Failed to zipalign APK\"}");
    }
}

