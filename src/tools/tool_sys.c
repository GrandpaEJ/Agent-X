#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char* execute_dynamic_skill(const char* name, cJSON* args) {
    if (strstr(name, "..") || strchr(name, '/')) {
        return strdup("{\"error\": \"Invalid skill name\"}");
    }
    char script_path[1024];
    int found = find_skill_file("skills", name, script_path, sizeof(script_path));
    if (!found) {
        return strdup("{\"error\": \"Skill script not found or not executable\"}");
    }
    
    cJSON* arg = args ? args->child : NULL;
    while (arg) {
        char env_name[512];
        snprintf(env_name, sizeof(env_name), "ARG_%s", arg->string);
        if (cJSON_IsString(arg)) {
            setenv(env_name, arg->valuestring, 1);
            setenv(arg->string, arg->valuestring, 1);
        } else if (cJSON_IsNumber(arg)) {
            char val_str[64];
            snprintf(val_str, sizeof(val_str), "%g", arg->valuedouble);
            setenv(env_name, val_str, 1);
            setenv(arg->string, val_str, 1);
        } else if (cJSON_IsBool(arg)) {
            const char* val_str = cJSON_IsTrue(arg) ? "true" : "false";
            setenv(env_name, val_str, 1);
            setenv(arg->string, val_str, 1);
        }
        arg = arg->next;
    }
    char* args_json_str = cJSON_PrintUnformatted(args);
    if (args_json_str) {
        setenv("ARG_JSON", args_json_str, 1);
    }
    
    char cmd[4096];
    if (found == 1) {
        snprintf(cmd, sizeof(cmd), "bash \"%s\" '%s' 2>&1", script_path, args_json_str ? args_json_str : "{}");
    } else if (found == 2) {
        snprintf(cmd, sizeof(cmd), "python3 \"%s\" '%s' 2>&1", script_path, args_json_str ? args_json_str : "{}");
    } else {
        snprintf(cmd, sizeof(cmd), "\"%s\" '%s' 2>&1", script_path, args_json_str ? args_json_str : "{}");
    }
    if (args_json_str) free(args_json_str);
    
    FILE* fp = popen(cmd, "r");
    if (!fp) return strdup("{\"error\": \"Failed to run skill script\"}");
    
    size_t cap = 4096;
    size_t len = 0;
    char* buf = malloc(cap);
    if (!buf) {
        pclose(fp);
        return NULL;
    }
    while (1) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        if (n == 0) break;
        len += n;
        if (len + 1 == cap) {
            cap *= 2;
            char* nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = nb;
        }
    }
    buf[len] = '\0';
    int status = pclose(fp);
    
    cJSON* res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", buf);
    cJSON_AddNumberToObject(res, "exit_code", WEXITSTATUS(status));
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(buf);
    
    arg = args ? args->child : NULL;
    while (arg) {
        char env_name[512];
        snprintf(env_name, sizeof(env_name), "ARG_%s", arg->string);
        unsetenv(env_name);
        unsetenv(arg->string);
        arg = arg->next;
    }
    unsetenv("ARG_JSON");
    return res_str;
}

char* execute_run_command(cJSON* args) {
    cJSON* cmd_obj = cJSON_GetObjectItem(args, "command");
    if (!cmd_obj || !cJSON_IsString(cmd_obj)) return strdup("{\"error\": \"Missing command\"}");
    
    const char* restrict_env = getenv("SANDBOX_RESTRICT");
    if (restrict_env && strcmp(restrict_env, "1") == 0) {
        return strdup("{\"error\": \"run_command is disabled under sandbox restriction\"}");
    }
    
    if (!check_safety(cmd_obj->valuestring)) {
        return strdup("{\"error\": \"User denied permission\"}");
    }
    
    // Redirect stderr to stdout so we capture both
    char cmd_full[4096];
    snprintf(cmd_full, sizeof(cmd_full), "%s 2>&1", cmd_obj->valuestring);
    
    FILE* fp = popen(cmd_full, "r");
    if (!fp) return strdup("{\"error\": \"Failed to run command\"}");
    
    size_t cap = 4096;
    size_t len = 0;
    char* buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    
    while (1) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        if (n == 0) break;
        len += n;
        if (len + 1 == cap) {
            cap *= 2;
            char* nb = realloc(buf, cap);
            if (!nb) { free(buf); pclose(fp); return NULL; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    int status = pclose(fp);
    
    cJSON* res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", buf);
    cJSON_AddNumberToObject(res, "exit_code", WEXITSTATUS(status));
    
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(buf);
    return res_str;
}

char* execute_run_command_from(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return strdup("{\"error\": \"Failed to run command\"}");
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    for (;;) {
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        if (n == 0) break;
        len += n;
        if (len + 1 == cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) { free(buf); pclose(fp); return NULL; } buf = nb; }
    }
    buf[len] = '\0';
    int status = pclose(fp);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", buf);
    cJSON_AddNumberToObject(res, "exit_code", WEXITSTATUS(status));
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(buf);
    return res_str;
}

