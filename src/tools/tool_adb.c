#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

char* execute_adb_install(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "apk_path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing apk_path\"}");
    int ret = adb_install(path_obj->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\", \"message\": \"APK installed successfully\"}");
    return strdup("{\"error\": \"ADB install failed\"}");
}

char* execute_adb_shell(cJSON* args) {
    cJSON* cmd_obj = cJSON_GetObjectItem(args, "command");
    if (!cmd_obj || !cJSON_IsString(cmd_obj)) return strdup("{\"error\": \"Missing command\"}");
    char* output = adb_shell(cmd_obj->valuestring);
    if (!output) return strdup("{\"error\": \"ADB shell command failed\"}");
    cJSON* res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "output", output);
    char* res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(output);
    return res_str;
}

char* execute_adb_devices(cJSON* args) {
    (void)args;
    char* devices_json = adb_devices();
    return devices_json ? devices_json : strdup("{\"devices\": []}");
}

char* execute_adb_push(cJSON* args) {
    cJSON* local = cJSON_GetObjectItem(args, "local");
    cJSON* remote = cJSON_GetObjectItem(args, "remote");
    if (!local || !cJSON_IsString(local) || !remote || !cJSON_IsString(remote))
        return strdup("{\"error\": \"Missing local or remote path\"}");
    int ret = adb_push_file(local->valuestring, remote->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB push failed\"}");
}

char* execute_adb_pull(cJSON* args) {
    cJSON* remote = cJSON_GetObjectItem(args, "remote");
    cJSON* local = cJSON_GetObjectItem(args, "local");
    if (!remote || !cJSON_IsString(remote) || !local || !cJSON_IsString(local))
        return strdup("{\"error\": \"Missing remote or local path\"}");
    int ret = adb_pull_file(remote->valuestring, local->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB pull failed\"}");
}

char* execute_adb_uninstall(cJSON* args) {
    cJSON* pkg = cJSON_GetObjectItem(args, "package");
    if (!pkg || !cJSON_IsString(pkg)) return strdup("{\"error\": \"Missing package\"}");
    int ret = adb_uninstall(pkg->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB uninstall failed\"}");
}

