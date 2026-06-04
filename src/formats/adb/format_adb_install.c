#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNC_DATA 0x41544144
#define SYNC_DONE 0x454e4f44
#define SYNC_SEND 0x444e4553
#define SYNC_OKAY 0x59414b4f
#define SYNC_FAIL 0x4c494146

static int sync_send_payload(adb_conn *conn, uint32_t id,
                             const void *data, uint32_t len) {
    uint8_t hdr[8];
    uint32_t id_le = id;
    uint32_t len_le = len;
    memcpy(hdr, &id_le, 4);
    memcpy(hdr + 4, &len_le, 4);
    if (adb_write(conn, hdr, 8)) return -1;
    if (len && data && adb_write(conn, data, len)) return -1;
    return 0;
}

static int sync_read_status(adb_conn *conn) {
    uint8_t resp[8];
    size_t rlen = 8;
    if (adb_read(conn, resp, &rlen)) return -1;
    if (rlen < 8) return -1;
    uint32_t id;
    memcpy(&id, resp, 4);
    return (id == SYNC_OKAY) ? 0 : -1;
}

int adb_install(const char *apk_path) {
    adb_conn conn;
    if (adb_connect(&conn)) return -1;

    if (adb_open_service(&conn, "sync:")) {
        adb_disconnect(&conn);
        return -1;
    }

    // Build SEND command: SEND + path_len + path + mode
    const char *fname = strrchr(apk_path, '/');
    fname = fname ? fname + 1 : apk_path;
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "/data/local/tmp/%s", fname);
    uint32_t path_len = strlen(dest_path);
    uint32_t mode = 0644;

    uint8_t send_hdr[8];
    uint32_t send_id = SYNC_SEND;
    memcpy(send_hdr, &send_id, 4);
    memcpy(send_hdr + 4, &path_len, 4);
    if (adb_write(&conn, send_hdr, 8)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, dest_path, path_len)) { adb_disconnect(&conn); return -1; }
    uint8_t mode_buf[4];
    memcpy(mode_buf, &mode, 4);
    if (adb_write(&conn, mode_buf, 4)) { adb_disconnect(&conn); return -1; }

    // Read file and send DATA chunks
    FILE *fp = fopen(apk_path, "rb");
    if (!fp) { adb_disconnect(&conn); return -1; }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *fbuf = malloc(65536);
    long remain = fsize;
    while (remain > 0) {
        size_t chunk = remain > 65536 ? 65536 : remain;
        if (fread(fbuf, 1, chunk, fp) != chunk) break;
        if (sync_send_payload(&conn, SYNC_DATA, fbuf, chunk)) break;
        remain -= chunk;
    }
    free(fbuf);
    fclose(fp);

    // Send DONE
    uint32_t timestamp = 0;
    uint8_t done_hdr[8];
    uint32_t done_id = SYNC_DONE;
    memcpy(done_hdr, &done_id, 4);
    memcpy(done_hdr + 4, &timestamp, 4);
    adb_write(&conn, done_hdr, 8);

    // Read OKAY/FAIL
    sync_read_status(&conn);
    adb_disconnect(&conn);

    // Run pm install
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "exec:pm install -r %s", dest_path);
    if (adb_connect(&conn)) return -1;
    if (adb_open_service(&conn, cmd)) { adb_disconnect(&conn); return -1; }

    char output[16384];
    size_t olen = sizeof(output) - 1;
    adb_read(&conn, output, &olen);
    output[olen] = '\0';
    adb_disconnect(&conn);

    if (strstr(output, "Success") || strstr(output, "success")) return 0;
    fprintf(stderr, "ADB install failed: %s\n", output);
    return -1;
}

int adb_uninstall(const char *package) {
    adb_conn conn;
    if (adb_connect(&conn)) return -1;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "exec:pm uninstall %s", package);
    if (adb_open_service(&conn, cmd)) { adb_disconnect(&conn); return -1; }

    char output[4096];
    size_t olen = sizeof(output) - 1;
    adb_read(&conn, output, &olen);
    output[olen] = '\0';
    adb_disconnect(&conn);

    if (strstr(output, "Success") || strstr(output, "success")) return 0;
    fprintf(stderr, "ADB uninstall failed: %s\n", output);
    return -1;
}
