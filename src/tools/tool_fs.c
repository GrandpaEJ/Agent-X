#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

char* execute_memorize(cJSON* args) {
    cJSON* key_obj = cJSON_GetObjectItem(args, "key");
    cJSON* val_obj = cJSON_GetObjectItem(args, "value");
    if (!key_obj || !cJSON_IsString(key_obj) || !val_obj || !cJSON_IsString(val_obj)) {
        return strdup("{\"error\": \"Missing key or value\"}");
    }
    const char* key = key_obj->valuestring;
    const char* value = val_obj->valuestring;
    
    cJSON* mem = NULL;
    FILE* fp = fopen(".agent_memory.json", "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char* buf = malloc(sz + 1);
        if (buf) {
            size_t n = fread(buf, 1, sz, fp);
            buf[n] = '\0';
            mem = cJSON_Parse(buf);
            free(buf);
        }
        fclose(fp);
    }
    if (!mem) {
        mem = cJSON_CreateObject();
    }
    cJSON_DeleteItemFromObject(mem, key);
    cJSON_AddStringToObject(mem, key, value);
    
    char* print = cJSON_PrintUnformatted(mem);
    cJSON_Delete(mem);
    if (!print) return strdup("{\"error\": \"Failed to serialize memory\"}");
    
    fp = fopen(".agent_memory.json", "w");
    if (!fp) {
        free(print);
        return strdup("{\"error\": \"Failed to open memory file for writing\"}");
    }
    fputs(print, fp);
    fclose(fp);
    free(print);
    return strdup("{\"status\": \"success\"}");
}

char* execute_recall(cJSON* args) {
    cJSON* q_obj = cJSON_GetObjectItem(args, "query");
    if (!q_obj || !cJSON_IsString(q_obj)) {
        return strdup("{\"error\": \"Missing query\"}");
    }
    const char* query = q_obj->valuestring;
    
    FILE* fp = fopen(".agent_memory.json", "r");
    if (!fp) {
        return strdup("{\"results\": []}");
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        return strdup("{\"error\": \"Memory allocation failed\"}");
    }
    size_t n = fread(buf, 1, sz, fp);
    buf[n] = '\0';
    fclose(fp);
    
    cJSON* mem = cJSON_Parse(buf);
    free(buf);
    if (!mem) {
        return strdup("{\"results\": []}");
    }
    
    cJSON* res = cJSON_CreateObject();
    cJSON* matches = cJSON_CreateArray();
    cJSON_AddItemToObject(res, "matches", matches);
    
    char* query_lower = str_tolower(query);
    cJSON* item = mem->child;
    while (item) {
        char* k_lower = str_tolower(item->string);
        char* v_lower = item->valuestring ? str_tolower(item->valuestring) : strdup("");
        if ((k_lower && query_lower && strstr(k_lower, query_lower)) || 
            (v_lower && query_lower && strstr(v_lower, query_lower))) {
            cJSON* match = cJSON_CreateObject();
            cJSON_AddStringToObject(match, "key", item->string);
            cJSON_AddStringToObject(match, "value", item->valuestring ? item->valuestring : "");
            cJSON_AddItemToArray(matches, match);
        }
        if (k_lower) free(k_lower);
        if (v_lower) free(v_lower);
        item = item->next;
    }
    if (query_lower) free(query_lower);
    cJSON_Delete(mem);
    
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_read_file(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    
    if (!is_path_safe(path_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    FILE* fp = fopen(path_obj->valuestring, "rb");
    if (!fp) return strdup("{\"error\": \"Failed to open file\"}");
    
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Max 1MB read for safety
    if (sz > 1024 * 1024) {
        fclose(fp);
        return strdup("{\"error\": \"File too large\"}");
    }
    
    char* buf = malloc(sz + 1);
    if (buf) {
        size_t bytes_read = fread(buf, 1, sz, fp);
        (void)bytes_read;
        buf[sz] = '\0';
    }
    fclose(fp);
    
    if (!buf) return strdup("{\"error\": \"Memory allocation failed\"}");
    
    cJSON* res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "content", buf);
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(buf);
    return res_str;
}

char* execute_write_file(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    cJSON* content_obj = cJSON_GetObjectItem(args, "content");
    
    if (!path_obj || !cJSON_IsString(path_obj) || !content_obj || !cJSON_IsString(content_obj)) {
        return strdup("{\"error\": \"Missing path or content\"}");
    }
    
    if (!is_path_safe(path_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    FILE* fp = fopen(path_obj->valuestring, "wb");
    if (!fp) return strdup("{\"error\": \"Failed to open file for writing\"}");
    
    size_t len = strlen(content_obj->valuestring);
    size_t written = fwrite(content_obj->valuestring, 1, len, fp);
    fclose(fp);
    
    if (written != len) return strdup("{\"error\": \"Failed to write all content\"}");
    
    return strdup("{\"status\": \"success\"}");
}

char* execute_list_dir(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    
    if (!is_path_safe(path_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    DIR* d = opendir(path_obj->valuestring);
    if (!d) return strdup("{\"error\": \"Failed to open directory\"}");
    
    cJSON* res = cJSON_CreateObject();
    cJSON* files = cJSON_CreateArray();
    cJSON_AddItemToObject(res, "files", files);
    
    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        cJSON_AddItemToArray(files, cJSON_CreateString(dir->d_name));
    }
    closedir(d);
    
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    return res_str;
}

char* execute_delete_file(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    
    if (!is_path_safe(path_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    char act[1024];
    snprintf(act, sizeof(act), "delete %s", path_obj->valuestring);
    if (!check_safety(act)) {
        return strdup("{\"error\": \"User denied permission\"}");
    }
    
    if (unlink(path_obj->valuestring) == 0) {
        return strdup("{\"status\": \"success\"}");
    }
    return strdup("{\"error\": \"Failed to delete file\"}");
}

char* execute_search_file(cJSON* args) {
    cJSON* pat_obj = cJSON_GetObjectItem(args, "pattern");
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!pat_obj || !cJSON_IsString(pat_obj) || !path_obj || !cJSON_IsString(path_obj)) {
        return strdup("{\"error\": \"Missing pattern or path\"}");
    }
    
    if (!is_path_safe(path_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "grep -r -n -I \"%s\" \"%s\"", pat_obj->valuestring, path_obj->valuestring);
    
    cJSON* run_args = cJSON_CreateObject();
    cJSON_AddStringToObject(run_args, "command", cmd);
    char* res = execute_run_command(run_args);
    cJSON_Delete(run_args);
    return res;
}

