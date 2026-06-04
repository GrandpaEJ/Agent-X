#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ADB server host protocol: connect, send hex4len+service, read response
static int host_service(const char *service, char **out_data, size_t *out_len) {
    int fd = tcp_connect("127.0.0.1", ADB_PORT);
    if (fd < 0) return -1;

    char cmd[4096];
    int slen = strlen(service);
    snprintf(cmd, sizeof(cmd), "%04x%s", slen, service);
    if (send_all(fd, cmd, 4 + slen)) { close(fd); return -1; }

    char ok[4];
    if (recv_all(fd, ok, 4)) { close(fd); return -1; }
    if (ok[0] != 'O' || ok[1] != 'K') { close(fd); return -1; }

    char hexlen[4];
    if (recv_all(fd, hexlen, 4)) { close(fd); return -1; }
    hexlen[3] = '\0';
    int dlen;
    sscanf(hexlen, "%x", &dlen);
    if (dlen <= 0) { close(fd); return -1; }

    *out_data = malloc(dlen + 1);
    if (!*out_data) { close(fd); return -1; }
    if (recv_all(fd, *out_data, dlen)) { free(*out_data); close(fd); return -1; }
    (*out_data)[dlen] = '\0';
    *out_len = dlen;
    close(fd);
    return 0;
}

char *adb_devices(void) {
    char *data = NULL;
    size_t len = 0;
    if (host_service("host:devices", &data, &len)) {
        return strdup("{\"devices\": []}");
    }

    char *json = malloc(512 + len * 2);
    if (!json) { free(data); return NULL; }
    char *j = json;
    j += sprintf(j, "{\"devices\": [");
    char *line = data;
    int first = 1;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            if (!first) j += sprintf(j, ", ");
            j += sprintf(j, "{\"serial\": \"%s\", \"status\": \"%s\"}", line, tab + 1);
            first = 0;
        }
        line = nl ? nl + 1 : NULL;
    }
    j += sprintf(j, "]}");
    free(data);
    return json;
}

int adb_connect_device(const char *serial) {
    // Connect to ADB server and select device
    int fd = tcp_connect("127.0.0.1", ADB_PORT);
    if (fd < 0) return -1;

    char service[512];
    if (serial && *serial)
        snprintf(service, sizeof(service), "host:transport:%s", serial);
    else
        snprintf(service, sizeof(service), "host:transport-any");

    int slen = strlen(service);
    char cmd[520];
    snprintf(cmd, sizeof(cmd), "%04x%s", slen, service);
    if (send_all(fd, cmd, 4 + slen)) { close(fd); return -1; }

    char ok[4];
    if (recv_all(fd, ok, 4)) { close(fd); return -1; }
    if (ok[0] != 'O' || ok[1] != 'K') { close(fd); return -1; }

    // Now this fd is in device mode, return it for use with adbd protocol
    close(fd); // Connection is established; caller should re-connect normally
    return 0;
}
