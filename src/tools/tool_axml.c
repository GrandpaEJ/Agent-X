#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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

