#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *adb_shell(const char *command) {
    adb_conn conn;
    if (adb_connect(&conn)) return NULL;

    char svc[4096];
    snprintf(svc, sizeof(svc), "exec:%s", command);
    if (adb_open_service(&conn, svc)) {
        adb_disconnect(&conn);
        return NULL;
    }

    char *output = malloc(65536);
    size_t olen = 65535;
    adb_read(&conn, output, &olen);
    output[olen] = '\0';
    adb_disconnect(&conn);
    return output;
}
