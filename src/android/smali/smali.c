#include "smali_parser.h"
#include "smali_pool.h"
#include "smali_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

static int collect_smali_files(const char *dir, char ***files, int *count) {
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

int smali_assemble(const char *src_dir, const char *out_dex) {
    if (!src_dir || !out_dex) return -1;

    smali_ctx_def_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    char **files = NULL;
    int file_count = 0;
    collect_smali_files(src_dir, &files, &file_count);

    for (int i = 0; i < file_count; i++) {
        parse_smali_file_path(&ctx, files[i]);
        free(files[i]);
    }
    free(files);

    if (ctx.class_count == 0) {
        return -1;
    }

    smali_pool_build_all(&ctx);
    int ret = write_assembled_dex(&ctx, out_dex);

    smali_pool_free(&ctx.strings);
    smali_pool_free(&ctx.types);
    smali_pool_free(&ctx.protos);
    smali_pool_free(&ctx.fields);
    smali_pool_free(&ctx.methods);

    return ret;
}
