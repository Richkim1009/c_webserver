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

#define BACKLOG 32
#define INIT_BUF_CAPACITY 2048
static bool server_stopped = false;

struct socket_handler_data {
    int accepted_socket_fd;
};

static void *accepted_socket_handler(void *arg)
{
    struct socket_handler_data data = *(struct socket_handler_data *)arg;
    free(arg);

    int buf_capacity = INIT_BUF_CAPACITY;
    char *buf = malloc(buf_capacity);
    int line_feed_index = -1;
    int buf_length = 0;

    while (1) {
        int received_length = recv(data.accepted_socket_fd, buf + buf_length, buf_capacity - buf_length, 0);
        if (received_length == -1) {
                perror("recv()");
                exit(1);
        }
        
        line_feed_index = -1;
        for (int i = buf_length; i < buf_length + received_length; ++i) {
            if (buf[i] == '\n') {
                line_feed_index = i;
                break;
            }
        }

        if (line_feed_index != -1) {
            break;
        }

        buf_length += received_length;
        if (buf_length >= buf_capacity) {
            buf_capacity *= 2;
            buf = realloc(buf, buf_capacity);
        }
    }

    // CR position
    buf[line_feed_index - 1] = '\0';

    char *method;
    char *request_target;
    char *http_version;

    char *first_space_ptr = strchr(buf, ' ');
    *first_space_ptr = '\0';

    method = strdup(buf);

    char *second_space_ptr = strchr(first_space_ptr + 1, ' ');
    *second_space_ptr = '\0';

    request_target = strdup(first_space_ptr + 1);

    http_version = strdup(second_space_ptr + 1);

    printf("[%d] %s\n", data.accepted_socket_fd, buf);

    printf("[%d]\n\tMethod: %s\n\tRequest_target: %s\n\tHTTP version: %s\n", data.accepted_socket_fd, method, request_target, http_version);
    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!";
    send(data.accepted_socket_fd, http_response, strlen(http_response), 0);

    close(data.accepted_socket_fd);

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

        printf("accepted: return=%d\n", accepted_socket_fd);

        struct socket_handler_data *data = malloc(sizeof(struct socket_handler_data));
        data->accepted_socket_fd = accepted_socket_fd;

        pthread_t thread;

        int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, data);

        if (pthread_result != 0) {
            perror("pthread_create()");
            exit(1);
        }

    }

    return 0;
}