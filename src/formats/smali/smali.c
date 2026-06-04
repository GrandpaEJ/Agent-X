#include "smali_internal.h"
#include "smali_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int parse_smali_file_path(smali_ctx_def_t *ctx, const char *filepath);
extern void smali_pool_build_all(smali_ctx_def_t *ctx);
extern int write_assembled_dex(smali_ctx_def_t *ctx, const char *out_dex);

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
    return ret;
}
