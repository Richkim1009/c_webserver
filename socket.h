#ifndef WEBSERVER_SOCKET_H
#define WEBSERVER_SOCKET_H

#include <stdlib.h>
#include <stdbool.h>

struct RecvBuffer {
    int fd;
    char *buf;
    int pos;
    int len;
    int cap;
};

extern char *recv_str_until(struct RecvBuffer *recv_buffer, char c);
extern char *recv_line(struct RecvBuffer *recv_buffer);

struct SendAllResult {
    size_t n;
    bool success;
};

extern struct SendAllResult send_all(int sockfd, const void *buf, size_t len, int flag);

#endif