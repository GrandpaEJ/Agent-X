#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int adb_install(const char *apk_path) {
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "adb install -r \"%s\" 2>&1", apk_path);
    int ret = system(cmd);
    if (ret == 0) return 0;
    fprintf(stderr, "ADB install failed (exit %d)\n", WEXITSTATUS(ret));
    return -1;
}

int adb_uninstall(const char *package) {
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "adb uninstall \"%s\" 2>&1", package);
    int ret = system(cmd);
    if (ret == 0) return 0;
    fprintf(stderr, "ADB uninstall failed (exit %d)\n", WEXITSTATUS(ret));
    return -1;
}
