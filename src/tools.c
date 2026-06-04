#define _XOPEN_SOURCE 700
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "agent.h"
#include "logger.h"
#include "formats.h"

static int is_path_safe(const char* path) {
    const char* restrict_env = getenv("SANDBOX_RESTRICT");
    if (!restrict_env || strcmp(restrict_env, "1") != 0) {
        return 1;
    }
    char resolved[4096];
    if (!realpath(path, resolved)) {
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", path);
        char* last_slash = strrchr(parent, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (!realpath(parent, resolved)) {
                return 0;
            }
        } else {
            return 1;
        }
    }
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    size_t cwd_len = strlen(cwd);
    if (strncmp(resolved, cwd, cwd_len) == 0) {
        if (resolved[cwd_len] == '\0' || resolved[cwd_len] == '/') {
            return 1;
        }
    }
    return 0;
}

static char* str_tolower(const char* s) {
    char* copy = strdup(s);
    if (!copy) return NULL;
    for (int i = 0; copy[i]; i++) {
        if (copy[i] >= 'A' && copy[i] <= 'Z') {
            copy[i] += 32;
        }
    }
    return copy;
}

static char* execute_memorize(cJSON* args) {
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

static char* execute_recall(cJSON* args) {
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

static int find_skill_file(const char* dir_path, const char* name, char* out_path, size_t out_len) {
    DIR* d = opendir(dir_path);
    if (!d) return 0;
    struct dirent* dir;
    int found_type = 0;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                int res = find_skill_file(path, name, out_path, out_len);
                if (res > 0) {
                    found_type = res;
                    break;
                }
            } else {
                char sh_name[256];
                char py_name[256];
                snprintf(sh_name, sizeof(sh_name), "%s.sh", name);
                snprintf(py_name, sizeof(py_name), "%s.py", name);
                if (strcmp(dir->d_name, sh_name) == 0) {
                    if (access(path, X_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 1;
                        break;
                    }
                } else if (strcmp(dir->d_name, py_name) == 0) {
                    if (access(path, F_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 2;
                        break;
                    }
                } else if (strcmp(dir->d_name, name) == 0) {
                    if (access(path, X_OK) == 0) {
                        snprintf(out_path, out_len, "%s", path);
                        found_type = 3;
                        break;
                    }
                }
            }
        }
    }
    closedir(d);
    return found_type;
}

static char* execute_dynamic_skill(const char* name, cJSON* args) {
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

static void load_skills_from_dir(const char* dir_path, cJSON* array) {
    DIR* d = opendir(dir_path);
    if (!d) return;
    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                load_skills_from_dir(path, array);
            } else {
                size_t len = strlen(dir->d_name);
                if (len > 5 && strcmp(dir->d_name + len - 5, ".json") == 0) {
                    FILE* fp = fopen(path, "r");
                    if (fp) {
                        fseek(fp, 0, SEEK_END);
                        long sz = ftell(fp);
                        fseek(fp, 0, SEEK_SET);
                        char* buf = malloc(sz + 1);
                        if (buf) {
                            size_t n = fread(buf, 1, sz, fp);
                            buf[n] = '\0';
                            cJSON* skill_json = cJSON_Parse(buf);
                            if (skill_json) {
                                cJSON_AddItemToArray(array, skill_json);
                            }
                            free(buf);
                        }
                        fclose(fp);
                    }
                }
            }
        }
    }
    closedir(d);
}

cJSON* tools_get_definitions(void) {
    const char* schema = "["
        "{\"type\": \"function\", \"function\": {\"name\": \"run_command\", \"description\": \"Execute a shell command. Operating System: Linux. Use this to compile code, search files, run scripts, etc.\", \"parameters\": {\"type\": \"object\", \"properties\": {\"command\": {\"type\": \"string\", \"description\": \"The shell command to run\"}}, \"required\": [\"command\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_file\", \"description\": \"Read the contents of a file\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"write_file\", \"description\": \"Write content to a file, overwriting it if it exists\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file\"}, \"content\": {\"type\": \"string\", \"description\": \"Content to write\"}}, \"required\": [\"path\", \"content\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"list_dir\", \"description\": \"List directory contents\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the directory\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"delete_file\", \"description\": \"Delete a file from the file system\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the file to delete\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"search_file\", \"description\": \"Search for a regex pattern inside files using grep\", \"parameters\": {\"type\": \"object\", \"properties\": {\"pattern\": {\"type\": \"string\", \"description\": \"Regex pattern\"}, \"path\": {\"type\": \"string\", \"description\": \"Directory or file to search\"}}, \"required\": [\"pattern\", \"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"download_file\", \"description\": \"Download a file from a URL to a local path\", \"parameters\": {\"type\": \"object\", \"properties\": {\"url\": {\"type\": \"string\", \"description\": \"The URL to download\"}, \"dest\": {\"type\": \"string\", \"description\": \"The local path to save the file to\"}}, \"required\": [\"url\", \"dest\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"memorize\", \"description\": \"Store a key-value fact in the long-term memory store\", \"parameters\": {\"type\": \"object\", \"properties\": {\"key\": {\"type\": \"string\", \"description\": \"Unique key or topic for this memory\"}, \"value\": {\"type\": \"string\", \"description\": \"The information to store\"}}, \"required\": [\"key\", \"value\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"recall\", \"description\": \"Retrieve information from long-term memory store matching the query\", \"parameters\": {\"type\": \"object\", \"properties\": {\"query\": {\"type\": \"string\", \"description\": \"The search term or keyword\"}}, \"required\": [\"query\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_install\", \"description\": \"Push an APK to an ADB-connected device and install it\", \"parameters\": {\"type\": \"object\", \"properties\": {\"apk_path\": {\"type\": \"string\", \"description\": \"Path to the APK file to install\"}}, \"required\": [\"apk_path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_shell\", \"description\": \"Run a shell command on an ADB-connected device and get its output\", \"parameters\": {\"type\": \"object\", \"properties\": {\"command\": {\"type\": \"string\", \"description\": \"The shell command to run on the device\"}}, \"required\": [\"command\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_devices\", \"description\": \"List all ADB-connected devices\", \"parameters\": {\"type\": \"object\", \"properties\": {}, \"required\": []}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_push\", \"description\": \"Push a local file to an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"local\": {\"type\": \"string\", \"description\": \"Local file path\"}, \"remote\": {\"type\": \"string\", \"description\": \"Remote destination path on device\"}}, \"required\": [\"local\", \"remote\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_pull\", \"description\": \"Pull a file from an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"remote\": {\"type\": \"string\", \"description\": \"Remote file path on device\"}, \"local\": {\"type\": \"string\", \"description\": \"Local destination path\"}}, \"required\": [\"remote\", \"local\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"adb_uninstall\", \"description\": \"Uninstall a package from an ADB-connected device\", \"parameters\": {\"type\": \"object\", \"properties\": {\"package\": {\"type\": \"string\", \"description\": \"Package name to uninstall\"}}, \"required\": [\"package\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_axml\", \"description\": \"Decode Android Binary XML (AXML) to human-readable XML text\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the binary XML file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"read_dex\", \"description\": \"Parse and dump DEX bytecode file contents\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the DEX file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"analyze_apk\", \"description\": \"Analyze an APK file: decode manifest, parse DEX classes, and list files\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK file\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"disasm_dex\", \"description\": \"Disassemble a DEX class to Smali\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the DEX file\"}, \"class\": {\"type\": \"number\", \"description\": \"Class index to disassemble (default: 0)\"}}, \"required\": [\"path\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"smali_assemble\", \"description\": \"Assemble smali directory into a DEX file\", \"parameters\": {\"type\": \"object\", \"properties\": {\"src_dir\": {\"type\": \"string\", \"description\": \"Path to smali source directory\"}, \"out_dex\": {\"type\": \"string\", \"description\": \"Output DEX file path\"}}, \"required\": [\"src_dir\", \"out_dex\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"repack_apk\", \"description\": \"Repack contents of an extracted APK folder back into a ZIP/APK\", \"parameters\": {\"type\": \"object\", \"properties\": {\"dir\": {\"type\": \"string\", \"description\": \"Path to directory with APK contents\"}, \"output\": {\"type\": \"string\", \"description\": \"Output APK path\"}}, \"required\": [\"dir\", \"output\"]}}},"
        "{\"type\": \"function\", \"function\": {\"name\": \"resign_apk\", \"description\": \"Resign an APK with a debug keystore\", \"parameters\": {\"type\": \"object\", \"properties\": {\"path\": {\"type\": \"string\", \"description\": \"Path to the APK file to sign\"}}, \"required\": [\"path\"]}}}"
        "]";
    cJSON* array = cJSON_Parse(schema);
    if (!array) return NULL;
    
    load_skills_from_dir("skills", array);
    return array;
}

static int check_safety(const char* action) {
    if (!is_cli_mode) return 1;
    const char* safe = getenv("SAFE_MODE");
    if (safe && strcmp(safe, "0") == 0) return 1;
    
    printf("\n\033[1;31m[Safety Check]\033[0m Agent wants to: \033[1;37m%s\033[0m. Allow? [y/N]: ", action);
    fflush(stdout);
    
    char ans[16];
    if (fgets(ans, sizeof(ans), stdin)) {
        if (ans[0] == 'y' || ans[0] == 'Y') return 1;
    }
    return 0;
}

static char* execute_run_command(cJSON* args) {
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

static char* execute_read_file(cJSON* args) {
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

static char* execute_write_file(cJSON* args) {
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

static char* execute_list_dir(cJSON* args) {
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

static char* execute_delete_file(cJSON* args) {
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

static char* execute_download_file(cJSON* args) {
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

static char* execute_search_file(cJSON* args) {
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

static char* execute_adb_install(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "apk_path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing apk_path\"}");
    int ret = adb_install(path_obj->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\", \"message\": \"APK installed successfully\"}");
    return strdup("{\"error\": \"ADB install failed\"}");
}

static char* execute_adb_shell(cJSON* args) {
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

static char* execute_adb_devices(cJSON* args) {
    (void)args;
    char* devices_json = adb_devices();
    return devices_json ? devices_json : strdup("{\"devices\": []}");
}

static char* execute_adb_push(cJSON* args) {
    cJSON* local = cJSON_GetObjectItem(args, "local");
    cJSON* remote = cJSON_GetObjectItem(args, "remote");
    if (!local || !cJSON_IsString(local) || !remote || !cJSON_IsString(remote))
        return strdup("{\"error\": \"Missing local or remote path\"}");
    int ret = adb_push_file(local->valuestring, remote->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB push failed\"}");
}

static char* execute_adb_pull(cJSON* args) {
    cJSON* remote = cJSON_GetObjectItem(args, "remote");
    cJSON* local = cJSON_GetObjectItem(args, "local");
    if (!remote || !cJSON_IsString(remote) || !local || !cJSON_IsString(local))
        return strdup("{\"error\": \"Missing remote or local path\"}");
    int ret = adb_pull_file(remote->valuestring, local->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB pull failed\"}");
}

static char* execute_adb_uninstall(cJSON* args) {
    cJSON* pkg = cJSON_GetObjectItem(args, "package");
    if (!pkg || !cJSON_IsString(pkg)) return strdup("{\"error\": \"Missing package\"}");
    int ret = adb_uninstall(pkg->valuestring);
    if (ret == 0) return strdup("{\"status\": \"success\"}");
    return strdup("{\"error\": \"ADB uninstall failed\"}");
}

static char* execute_read_axml(cJSON* args) {
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

static char* execute_analyze_apk(cJSON* args) {
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

static char* execute_disasm_dex(cJSON* args) {
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

static char* execute_run_command_from(const char *cmd);

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

static char* execute_repack_apk(cJSON* args) {
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
        zip_writer_add(zw, name, buf, sz, 1);
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

static char* execute_smali_assemble(cJSON* args) {
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

static char* execute_resign_apk(cJSON* args) {
    cJSON* path_obj = cJSON_GetObjectItem(args, "path");
    if (!path_obj || !cJSON_IsString(path_obj)) return strdup("{\"error\": \"Missing path\"}");
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "jarsigner -keystore test.jks -storepass testpass -keypass testpass -signedjar \"%s.signed\" \"%s\" test", path_obj->valuestring, path_obj->valuestring);
    return execute_run_command_from(cmd);
}

static char* execute_run_command_from(const char *cmd) {
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

static char* execute_read_dex(cJSON* args) {
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
    free(buf);
    if (!ctx) return strdup("{\"error\": \"Invalid DEX file\"}");

    char *dump = dex_dump(ctx);
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "dump", dump ? dump : "");
    char *res_str = cJSON_PrintUnformatted(res);
    cJSON_Delete(res);
    free(dump);
    dex_free(ctx);
    return res_str;
}

char* tools_execute(const char* name, cJSON* args) {
    char details[256];
    snprintf(details, sizeof(details), "tool: %s", name);
    log_info("tool_call", details);
    
    if (strcmp(name, "run_command") == 0) return execute_run_command(args);
    if (strcmp(name, "read_file") == 0) return execute_read_file(args);
    if (strcmp(name, "write_file") == 0) return execute_write_file(args);
    if (strcmp(name, "list_dir") == 0) return execute_list_dir(args);
    if (strcmp(name, "delete_file") == 0) return execute_delete_file(args);
    if (strcmp(name, "search_file") == 0) return execute_search_file(args);
    if (strcmp(name, "download_file") == 0) return execute_download_file(args);
    if (strcmp(name, "memorize") == 0) return execute_memorize(args);
    if (strcmp(name, "recall") == 0) return execute_recall(args);
    if (strcmp(name, "adb_install") == 0) return execute_adb_install(args);
    if (strcmp(name, "adb_shell") == 0) return execute_adb_shell(args);
    if (strcmp(name, "adb_devices") == 0) return execute_adb_devices(args);
    if (strcmp(name, "adb_push") == 0) return execute_adb_push(args);
    if (strcmp(name, "adb_pull") == 0) return execute_adb_pull(args);
    if (strcmp(name, "adb_uninstall") == 0) return execute_adb_uninstall(args);
    if (strcmp(name, "read_axml") == 0) return execute_read_axml(args);
    if (strcmp(name, "read_dex") == 0) return execute_read_dex(args);
    if (strcmp(name, "analyze_apk") == 0) return execute_analyze_apk(args);
    if (strcmp(name, "disasm_dex") == 0) return execute_disasm_dex(args);
    if (strcmp(name, "smali_assemble") == 0) return execute_smali_assemble(args);
    if (strcmp(name, "repack_apk") == 0) return execute_repack_apk(args);
    if (strcmp(name, "resign_apk") == 0) return execute_resign_apk(args);
    
    return execute_dynamic_skill(name, args);
}
