#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>

#define BACKLOG 32
#define INIT_LINE_BUF_CAPACITY 64
#define RECV_BUF_CAPACITY 2048
static bool server_stopped = false;

struct socket_handler_data {
    int fd;
};

void log_debug(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

static void *accepted_socket_handler(void *arg)
{
    struct socket_handler_data data = *(struct socket_handler_data *)arg;
    free(arg);
    int fd = data.fd;
    log_debug("[%d] thread started: fn=accepted_socket_handler\n", fd);
    char recv_buf[RECV_BUF_CAPACITY];
    int recv_pos = 0;
    int recv_len;

    int line_buf_capacity = INIT_LINE_BUF_CAPACITY;
    char *line_buf = malloc(line_buf_capacity);
    int line_buf_len = 0;

    while (1) {
        recv_len = recv(fd, recv_buf, RECV_BUF_CAPACITY, 0);

        if (recv_len == -1) {
                perror("recv()");
                exit(1);
        }
        
        int line_feed_index = -1;
        for (int i = 0; i < recv_len; ++i) {
            if (recv_buf[i] == '\n') {
                line_feed_index = i;
                break;
            }
        }

        if (line_feed_index == -1) {
            while (line_buf_len + recv_len > line_buf_capacity) {
                line_buf_capacity *= 2;
                line_buf = realloc(line_buf, line_buf_capacity);
            }
            memcpy(line_buf + line_buf_len, recv_buf, RECV_BUF_CAPACITY);
            line_buf_len += recv_len;

            recv_pos = 0;
        } else {
            while (line_buf_len + line_feed_index + 1 > line_buf_capacity) {
                line_buf_capacity *= 2;
                line_buf = realloc(line_buf, line_buf_capacity);
            }
            memcpy(line_buf + line_buf_len, recv_buf, line_feed_index + 1);
            line_buf[line_buf_len + line_feed_index + 1] = '\0';
            line_buf_len += line_feed_index + 1;
            recv_pos = line_feed_index + 1;
            break;
        }

    }
    
    char *request_line = line_buf;
    int request_line_len = line_buf_len;

    // CR position
    request_line[request_line_len - 1] = '\0';

    log_debug("[%d] first line: %s\n", fd, request_line);

    log_debug("[%d] %s\n", fd, request_line);
    char *method;
    char *request_target;
    char *http_version;

    char *first_space_ptr = strchr(request_line, ' ');
    *first_space_ptr = '\0';

    method = strdup(request_line);

    char *second_space_ptr = strchr(first_space_ptr + 1, ' ');
    *second_space_ptr = '\0';

    request_target = strdup(first_space_ptr + 1);

    http_version = strdup(second_space_ptr + 1);

    log_debug("[%d]\n\tMethod: %s\n\tRequest_target: %s\n\tHTTP version: %s\n", fd, method, request_target, http_version);




    line_buf_capacity = INIT_LINE_BUF_CAPACITY;
    line_buf = malloc(line_buf_capacity);
    line_buf_len = 0;

    while (1) {
        if (recv_pos == recv_len) {
            recv_pos = 0;
            int recv_len = recv(fd, recv_buf, RECV_BUF_CAPACITY, 0);

            if (recv_len == -1) {
                    perror("recv()");
                    exit(1);
            }
        }
        
        int line_feed_index = -1;
        for (int i = recv_pos; i < recv_len; ++i) {
            if (recv_buf[i] == '\n') {
                line_feed_index = i;
                break;
            }
        }

        if (line_feed_index == -1) {
            while (line_buf_len + (recv_len - recv_pos) > line_buf_capacity) {
                line_buf_capacity *= 2;
                line_buf = realloc(line_buf, line_buf_capacity);
            }
            memcpy(line_buf + line_buf_len, recv_buf + recv_pos, recv_len - recv_pos);
            line_buf_len += (recv_len - recv_pos);
        } else {
            while (line_buf_len + (line_feed_index + 1 - recv_pos) > line_buf_capacity) {
                line_buf_capacity *= 2;
                line_buf = realloc(line_buf, line_buf_capacity);
            }
            memcpy(line_buf + line_buf_len, recv_buf, (line_feed_index + 1 - recv_pos));
            line_buf[line_buf_len + (line_feed_index + 1 - recv_pos)] = '\0';
            line_buf_len += (line_feed_index + 1 - recv_pos);
            break;
        }

    }
    

    log_debug("[%d] second line: %s\n", fd, line_buf);

    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!";
    send(fd, http_response, strlen(http_response), 0);

    close(fd);

    return NULL;
}

int main(int argc, char **argv)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        perror("socket");
        exit(1);
    }

    int so_reuseaddr_enable = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_enable, sizeof(int)) == -1) {
        perror("setsocketopt()");
    }

    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(8080);
    bind_addr.sin_addr.s_addr = 0;

    int bind_result = bind(socket_fd, (struct sockaddr *)&bind_addr, sizeof bind_addr);
    if (bind_result == -1) {
        perror("bind");
        exit(1);
    }

    int listen_result = listen(socket_fd, BACKLOG);

    if (listen_result == -1) {
        perror("listen()");
        exit(1);
    }

    struct sockaddr_in accepted_socket_addr;
    socklen_t accepted_socket_len = sizeof(accepted_socket_addr);

    while (!server_stopped) {
        int accepted_socket_fd = accept(socket_fd, (struct sockaddr *)&accepted_socket_addr, &accepted_socket_len);

        if (accepted_socket_fd == -1) {
            perror("accept()");
            exit(1);
        }

        log_debug("accepted: fd=%d, address=%s:%d\n", accepted_socket_fd, inet_ntoa(accepted_socket_addr.sin_addr), accepted_socket_addr.sin_port);

        struct socket_handler_data *data = malloc(sizeof(struct socket_handler_data));
        data->fd = accepted_socket_fd;

        pthread_t thread;

        int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, data);

        if (pthread_result != 0) {
            perror("pthread_create()");
            exit(1);
        }
    }

    return 0;
}