#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNC_DATA 0x41544144
#define SYNC_DONE 0x454e4f44
#define SYNC_SEND 0x444e4553
#define SYNC_RECV 0x56434552
#define SYNC_OKAY 0x59414b4f
#define SYNC_FAIL 0x4c494146

static int sync_write_cmd(adb_conn *conn, uint32_t id,
                          const void *data, uint32_t len) {
    uint8_t hdr[8];
    memcpy(hdr, &id, 4);
    memcpy(hdr + 4, &len, 4);
    if (adb_write(conn, hdr, 8)) return -1;
    if (len && data && adb_write(conn, data, len)) return -1;
    return 0;
}

int adb_push_file(const char *local_path, const char *remote_path) {
    adb_conn conn;
    if (adb_connect(&conn)) return -1;
    if (adb_open_service(&conn, "sync:")) { adb_disconnect(&conn); return -1; }

    uint32_t path_len = strlen(remote_path);
    uint32_t mode = 0644;
    uint32_t send_id = SYNC_SEND;
    if (adb_write(&conn, &send_id, 4)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, &path_len, 4)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, remote_path, path_len)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, &mode, 4)) { adb_disconnect(&conn); return -1; }

    FILE *fp = fopen(local_path, "rb");
    if (!fp) { adb_disconnect(&conn); return -1; }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint8_t *buf = malloc(65536);
    long remain = fsize;
    while (remain > 0) {
        size_t chunk = remain > 65536 ? 65536 : remain;
        if (fread(buf, 1, chunk, fp) != chunk) break;
        if (sync_write_cmd(&conn, SYNC_DATA, buf, chunk)) break;
        remain -= chunk;
    }
    free(buf);
    fclose(fp);

    uint32_t ts = 0;
    sync_write_cmd(&conn, SYNC_DONE, &ts, 4);

    // Read OKAY
    uint8_t resp[8];
    size_t rlen = 8;
    adb_read(&conn, resp, &rlen);
    adb_disconnect(&conn);

    uint32_t r_id;
    if (rlen >= 4) memcpy(&r_id, resp, 4);
    else r_id = SYNC_FAIL;

    return (r_id == SYNC_OKAY) ? 0 : -1;
}

int adb_pull_file(const char *remote_path, const char *local_path) {
    adb_conn conn;
    if (adb_connect(&conn)) return -1;
    if (adb_open_service(&conn, "sync:")) { adb_disconnect(&conn); return -1; }

    uint32_t path_len = strlen(remote_path);
    uint32_t recv_id = SYNC_RECV;
    if (adb_write(&conn, &recv_id, 4)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, &path_len, 4)) { adb_disconnect(&conn); return -1; }
    if (adb_write(&conn, remote_path, path_len)) { adb_disconnect(&conn); return -1; }

    FILE *fp = fopen(local_path, "wb");
    if (!fp) { adb_disconnect(&conn); return -1; }

    int ret = -1;
    uint8_t *buf = malloc(65536);
    while (1) {
        uint8_t hdr[8];
        size_t hlen = 8;
        if (adb_read(&conn, hdr, &hlen)) break;
        if (hlen < 8) break;
        uint32_t r_id, r_len;
        memcpy(&r_id, hdr, 4);
        memcpy(&r_len, hdr + 4, 4);
        if (r_id == SYNC_DATA) {
            while (r_len > 0) {
                size_t chunk = r_len > 65536 ? 65536 : r_len;
                size_t clen = chunk;
                if (adb_read(&conn, buf, &clen)) break;
                fwrite(buf, 1, clen, fp);
                r_len -= clen;
            }
        } else if (r_id == SYNC_DONE) {
            ret = 0;
            break;
        } else {
            break;
        }
    }
    free(buf);
    fclose(fp);
    adb_disconnect(&conn);
    return ret;
}
