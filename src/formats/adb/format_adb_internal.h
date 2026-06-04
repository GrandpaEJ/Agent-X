#ifndef FORMAT_ADB_INTERNAL_H
#define FORMAT_ADB_INTERNAL_H

#include "formats.h"
#include <stdint.h>
#include <stddef.h>

#define ADB_PORT 5037
#define CONNECT_TIMEOUT 5
#define MAX_PAYLOAD (256 * 1024)

#define A_CNXN 0x4e584e43
#define A_AUTH 0x48545541
#define A_OPEN 0x4e45504f
#define A_OKAY 0x59414b4f
#define A_CLSE 0x45534c43
#define A_WRTE 0x45545257

#define AUTH_TOKEN 1
#define AUTH_SIGNATURE 2
#define AUTH_RSAPUBLICKEY 3

#define ADB_VERSION 0x01000001

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t data_length;
    uint32_t data_crc32;
    uint32_t magic;
} adb_msg;

typedef struct {
    int fd;
    int local_id;
    int remote_id;
    uint8_t buf[MAX_PAYLOAD];
    size_t buf_len;
} adb_conn;

int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);
int tcp_connect(const char *host, int port);
int read_msg(int fd, adb_msg *msg);
int write_msg(int fd, uint32_t cmd, uint32_t arg0, uint32_t arg1,
              const void *data, uint32_t len);

int adb_connect(adb_conn *conn);
void adb_disconnect(adb_conn *conn);
int adb_open_service(adb_conn *conn, const char *service);
int adb_write(adb_conn *conn, const void *data, size_t len);
int adb_read(adb_conn *conn, void *buf, size_t *len);

#endif
