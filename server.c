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

#define ARRAY_SIZE(arr) sizeof(arr) / sizeof(arr[0])

#define BACKLOG 32
#define INIT_STR_BUF_CAPACITY 64
#define RECV_BUF_CAPACITY 2048
static bool server_stopped = false;

struct HandleClientArgrs {
    int sock;
    struct sockaddr_in client_addr;
};

struct RecvBuffer {
    int fd;
    char *buf;
    int pos;
    int len;
    int cap;
};

enum HttpMethod {
    HTTP_METHOD_GET = 1,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_CONNECT,
    HTTP_METHOD_PATCH,
};

enum HttpVersion {
    HTTP_VERSION_0_9 = 1,
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
};

char *recv_str_until(struct RecvBuffer *recv_buffer, char c)
{
    int str_buf_capacity = INIT_STR_BUF_CAPACITY;
    char *str_buf = malloc(str_buf_capacity);
    int str_buf_len = 0;

    while (1) {
        if (recv_buffer->pos == recv_buffer->len) {
            recv_buffer->pos = 0;
            recv_buffer->len = recv(recv_buffer->fd, recv_buffer->buf, RECV_BUF_CAPACITY, 0);

            if (recv_buffer->len == -1) {
                    perror("recv()");
                    return NULL;
            }
        }
        
        int index = -1;
        for (int i = recv_buffer->pos; i < recv_buffer->len; ++i) {
            if (recv_buffer->buf[i] == c) {
                index = i;
                break;
            }
        }

        int n = (index == -1 ? recv_buffer->len : index + 1) - recv_buffer->pos;
 
        while (str_buf_len + n > str_buf_capacity) {
            str_buf_capacity *= 2;
            str_buf = realloc(str_buf, str_buf_capacity);
        }
        memcpy(str_buf + str_buf_len, recv_buffer->buf + recv_buffer->pos, n);
        str_buf_len += (index + 1 - recv_buffer->pos);

        recv_buffer->pos += n;   

        if (index != -1) {
            break;
        }

    }

    if (str_buf_len + 1 > str_buf_capacity) {
            ++str_buf_capacity;
            str_buf = realloc(str_buf, str_buf_capacity);
        }
    str_buf[str_buf_len] = '\0';

    return str_buf;
}

char *recv_line(struct RecvBuffer *recv_buffer)
{
    return recv_str_until(recv_buffer ,'\n');
}

void remove_crlf(char *str)
{
    int len = strlen(str);
    if (str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
    if (str[len - 2] == '\r') {
        str[len - 2] = '\0';
    }
}

void log_debug(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

bool str_starts_with(char *s, char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_with(char *s, char *suffix)
{
    int s_len = strlen(s);
    int suffix_len = strlen(suffix);

    if (s_len < suffix_len) {
        return false;
    }

    return strncmp(s + s_len - suffix_len, suffix + 1, suffix_len - 1) == 0;
}

void log_debug_handle_client_header(struct HandleClientArgrs *args)
{
    log_debug("[%d] [%s:%d] ", args->sock, 
        inet_ntoa(args->client_addr.sin_addr), ntohs(args->client_addr.sin_port));
}

static void *handle_client(void *void_arg)
{
    struct HandleClientArgrs *args = (struct HandleClientArgrs *)void_arg;
    int sock = args->sock;
    log_debug_handle_client_header(args);
    log_debug("[%d] thread started: fn=handle_client\n", sock);
    
    struct RecvBuffer recv_buffer = {
        .fd = sock,
        .buf = malloc(RECV_BUF_CAPACITY),
        .pos = 0,
        .len = 0,
        .cap = RECV_BUF_CAPACITY,
    };
    
    char *request_line = recv_line(&recv_buffer);
    int request_line_len = strlen(request_line);

    // CR position
    request_line[request_line_len - 1] = '\0';
    log_debug_handle_client_header(args);
    log_debug("[%d] first line: %s\n", sock, request_line);

    log_debug("[%d] %s\n", sock, request_line);
    enum HttpMethod method;
    char *request_target;
    enum HttpVersion version;

    struct MethodTableEntry {
        char *request_line_prefix;
        enum HttpMethod method;
    };

    struct MethodTableEntry method_table[] = {
        { "GET", HTTP_METHOD_GET },
        { "HEAD", HTTP_METHOD_HEAD },
        { "POST", HTTP_METHOD_POST },
        { "PUT", HTTP_METHOD_PUT },
        { "DELETE", HTTP_METHOD_DELETE },
        { "TRACE", HTTP_METHOD_TRACE },
        { "OPTIONS", HTTP_METHOD_OPTIONS },
        { "CONNECT", HTTP_METHOD_CONNECT },
        { "PATCH", HTTP_METHOD_PATCH }
    };

    bool found = false;

    for (int i = 0; i < ARRAY_SIZE(method_table); ++i) {
        if (str_starts_with(request_line, method_table[i].request_line_prefix)) {
            method = method_table[i].method;
            found = true;
            break;
        }
    }

    if (!found) {
        log_debug_handle_client_header(args);
        log_debug("Uknown HTTP method\n");
        goto finally;
    }

    printf("test %s\n", request_line);

    if (str_ends_with(request_line, " HTTP/0.9")) {
        version = HTTP_VERSION_0_9;
    } else if (str_ends_with(request_line, " HTTP/1.0")) {
        version = HTTP_VERSION_1_0;
    } else if (str_ends_with(request_line, " HTTP/1.1")) {
        version = HTTP_VERSION_1_1;
    } else {
        log_debug_handle_client_header(args);
        log_debug("Uknown HTTP version\n");
        goto finally;
    }

    log_debug_handle_client_header(args);
    log_debug("Given HTTP method: %d\n", method);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP version: %d\n", version);

    char *first_space_ptr = strchr(request_line, ' ');
    char *last_space_ptr = strrchr(request_line, ' ');

    request_target = strndup(first_space_ptr + 1, last_space_ptr - first_space_ptr - 1);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP request target: %s\n", request_target);
    /*
    http_version = strdup(second_space_ptr + 1);

    log_debug_handle_client_header(args);
    log_debug("[%d]\n\tMethod: %d\n\tRequest_target: %s\n\tHTTP version: %s\n", sock, method, request_target, http_version);
    */
    while (1) {
        char *header_line = recv_line(&recv_buffer);
        remove_crlf(header_line);
        if (!strlen(header_line)) {
            break;
        }
        log_debug_handle_client_header(args);
        log_debug("[%d] first header: %s\n", sock, header_line);
    }

    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!";
    send(sock, http_response, strlen(http_response), 0);

    
finally:
    close(sock);
    pthread_exit((void *)0);
    free(void_arg);

    return NULL;
}

int main(int argc, char **argv)
{
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock == -1) {
        perror("socket");
        exit(1);
    }

    int so_reuseaddr_enable = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_enable, sizeof(int)) == -1) {
        perror("setsocketopt()");
        exit(1);
    }

    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(8080);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sock, (struct sockaddr *)&bind_addr, sizeof bind_addr) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(server_sock, BACKLOG) == -1) {
        perror("listen()");
        exit(1);
    }

    
    while (!server_stopped) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);

        if (sock == -1) {
            perror("accept()");
            exit(1);
        }

        log_debug("accepted: fd=%d, address=%s:%d\n", sock, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        struct HandleClientArgrs *args = malloc(sizeof(struct HandleClientArgrs));
        args->sock = sock;
        args->client_addr = client_addr;

        pthread_t client_thread;

        int pthread_result = pthread_create(&client_thread, NULL, handle_client, args);

        if (pthread_result != 0) {
            perror("pthread_create()");
            exit(1);
        }
    }

    return 0;
}