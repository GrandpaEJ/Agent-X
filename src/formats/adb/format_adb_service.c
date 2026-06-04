#include "format_adb_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int adb_open_service(adb_conn *conn, const char *service) {
    conn->local_id = 1;
    if (write_msg(conn->fd, A_OPEN, conn->local_id, 0, service, strlen(service)))
        return -1;
    adb_msg msg;
    if (read_msg(conn->fd, &msg)) return -1;
    if (msg.command == A_OKAY) {
        conn->remote_id = msg.arg0;
        if (msg.data_length > 0) {
            uint8_t *tmp = malloc(msg.data_length);
            if (tmp) { recv_all(conn->fd, tmp, msg.data_length); free(tmp); }
        }
        return 0;
    }
    if (msg.command == A_CLSE) return -1;
    if (msg.data_length > 0) {
        uint8_t *tmp = malloc(msg.data_length);
        if (tmp) { recv_all(conn->fd, tmp, msg.data_length); free(tmp); }
    }
    return 0;
}

int adb_write(adb_conn *conn, const void *data, size_t len) {
    if (write_msg(conn->fd, A_WRTE, conn->local_id, conn->remote_id,
                  data, len)) return -1;
    adb_msg msg;
    if (read_msg(conn->fd, &msg)) return -1;
    if (msg.command != A_OKAY) return -1;
    if (msg.data_length > 0) {
        uint8_t *tmp = malloc(msg.data_length);
        if (tmp) { recv_all(conn->fd, tmp, msg.data_length); free(tmp); }
    }
    return 0;
}

int adb_read(adb_conn *conn, void *buf, size_t *len) {
    conn->buf_len = 0;
    size_t total = 0;
    while (1) {
        adb_msg msg;
        if (read_msg(conn->fd, &msg)) return -1;
        if (msg.command == A_WRTE) {
            conn->remote_id = msg.arg0;
            size_t to_read = msg.data_length;
            if (to_read > MAX_PAYLOAD) to_read = MAX_PAYLOAD;
            if (recv_all(conn->fd, conn->buf, to_read)) return -1;
            conn->buf_len = to_read;
            if (write_msg(conn->fd, A_OKAY, conn->local_id, conn->remote_id,
                          NULL, 0)) return -1;
            if (total + to_read > *len) to_read = *len - total;
            memcpy((uint8_t *)buf + total, conn->buf, to_read);
            total += to_read;
            if (total >= *len || to_read == 0) {
                *len = total;
                return 0;
            }
        } else if (msg.command == A_CLSE) {
            *len = total;
            return 0;
        } else {
            if (msg.data_length > 0) {
                uint8_t tmp[4096];
                size_t r = msg.data_length > 4096 ? 4096 : msg.data_length;
                recv_all(conn->fd, tmp, r);
            }
            *len = total;
            return 0;
        }
    }
    *len = total;
    return 0;
}
