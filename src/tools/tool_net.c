#include "tools.h"
#include "tools_internal.h"
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char* execute_download_file(cJSON* args) {
    cJSON* url_obj = cJSON_GetObjectItem(args, "url");
    cJSON* dest_obj = cJSON_GetObjectItem(args, "dest");
    if (!url_obj || !cJSON_IsString(url_obj) || !dest_obj || !cJSON_IsString(dest_obj)) return strdup("{\"error\": \"Missing url or dest\"}");
    
    if (!is_path_safe(dest_obj->valuestring)) {
        return strdup("{\"error\": \"Path is outside the restricted sandbox workspace\"}");
    }
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -s -L -o \"%s\" \"%s\"", dest_obj->valuestring, url_obj->valuestring);
    int ret = system(cmd);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"Failed to download file\"}");
}

