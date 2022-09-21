#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>

#include "string.h"
#include "socket.h"
#include "http.h"

#define ARRAY_SIZE(arr) sizeof(arr) / sizeof(arr[0])
#define BACKLOG 32

static const int recv_buf_capacity = 2048;

static bool server_stopped = false;

struct HandleClientArgrs {
    int sock;
    struct sockaddr_in client_addr;
};

void log_debug(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
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
        .buf = malloc(recv_buf_capacity),
        .pos = 0,
        .len = 0,
        .cap = recv_buf_capacity,
    };
    
    char *request_line = recv_line(&recv_buffer);
    remove_crlf(request_line);

    log_debug_handle_client_header(args);
    log_debug("first line: %s\n", request_line);

    struct HttpRequest request;

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

    request.method = method;

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

    request.version = version;

    log_debug_handle_client_header(args);
    log_debug("Given HTTP method: %d\n", method);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP version: %d\n", version);

    char *first_space_ptr = strchr(request_line, ' ');
    char *last_space_ptr = strrchr(request_line, ' ');

    request_target = strndup(first_space_ptr + 1, last_space_ptr - first_space_ptr - 1);
    request.target = request_target;

    log_debug_handle_client_header(args);
    log_debug("Given HTTP request target: %s\n", request_target);
    
    struct HttpRequestHeaderFields fields;
    fields.len = 0;
    fields.cap = initial_header_fields_capacity;
    fields.elements = malloc(fields.cap * sizeof(struct HttpRequestHeaderField));

    int pos = 0;
    while (1) {
        char *header_line = recv_line(&recv_buffer);
        remove_crlf(header_line);
        if (!strlen(header_line)) {
            break;
        }

        char *header_field_name;
        char *header_field_value;

        char *colon_ptr = strchr(header_line, ':');
        header_field_name = strndup(header_line, colon_ptr - header_line);

        char *ptr = colon_ptr + 1;
        for (ptr = colon_ptr + 1; *ptr == ' ' || *ptr == '\t'; ++ptr) {}

        header_field_value = strdup(ptr);

        if (pos == fields.cap) {
            fields.cap *= 2;
            fields.elements = realloc(fields.elements, fields.cap * sizeof(struct HttpRequestHeaderField));
        }

        fields.elements[pos].name = header_field_name;
        fields.elements[pos].value = header_field_value;

        log_debug_handle_client_header(args);
        log_debug("%d Header field name: %s\n", pos, header_field_name);
        log_debug("%d Header field value: %s\n", pos, header_field_value);
        
        ++pos;
    }

    request.fields = fields;

    if (strcmp(request.target, "/") == 0) {
        FILE *fp = fopen("/home/polaris/project/c_webserver/contents/index.html", "r");

        if (fp == NULL) {
            perror("fopen()");
            goto finally;
        }

        if(fseek(fp, 0, SEEK_END) == -1) {
            perror("fseek()");
            fclose(fp);
            goto finally;
        }

        long file_size = ftell(fp);

        if (file_size == -1) {
            perror("ftell()");
            fclose(fp);
            goto finally; 
        }

        if (fseek(fp, 0, SEEK_SET)) {
            perror("fseek()");
            fclose(fp);
            goto finally;
        }

        char *file_content = malloc(file_size);
        size_t n = fread(file_content, 1, file_size, fp);

        if (n != file_size) {
            log_debug_handle_client_header(args);
            log_debug("Error while reading the file: n != file_size\n");
            fclose(fp);
            goto finally;
        }
    
        fclose(fp);
        
        char *http_response_first = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";

        struct SendAllResult send_all_result;
        send_all_result = send_all(sock, http_response_first, strlen(http_response_first), 0);

        if (!send_all_result.success) {
            perror("send_all()");
            goto finally;
        }

        send_all_result = send_all(sock, file_content, file_size, 0);

        if (!send_all_result.success) {
            perror("send_all()");
            goto finally;
        }

    } else if (strcmp(request.target, "/taeyeon.jpg") == 0) {
        FILE *fp = fopen("/home/polaris/project/c_webserver/contents/taeyeon.jpg", "r");
        if (fp == NULL) {
            perror("fopen()2");
            goto finally;
        }

        if(fseek(fp, 0, SEEK_END) == -1) {
            perror("fseek()");
            fclose(fp);
            goto finally;
        }

        long file_size = ftell(fp);

        if (file_size == -1) {
            perror("ftell()");
            fclose(fp);
            goto finally; 
        }

        if (fseek(fp, 0, SEEK_SET)) {
            perror("fseek()");
            fclose(fp);
            goto finally;
        }

        char *file_content = malloc(file_size);
        size_t n = fread(file_content, 1, file_size, fp);

        if (n != file_size) {
            log_debug_handle_client_header(args);
            log_debug("Error while reading the file: n != file_size\n");
            fclose(fp);
            goto finally;
        }
    
        fclose(fp);
        
        char *http_response_first = "HTTP/1.1 200 OK\r\nContent-type: image/jpeg\r\n\r\n";

        struct SendAllResult send_all_result;
        send_all_result = send_all(sock, http_response_first, strlen(http_response_first), 0);

        if (!send_all_result.success) {
            perror("send_all()");
            goto finally;
        }

        send_all_result = send_all(sock, file_content, file_size, 0);

        if (!send_all_result.success) {
            perror("send_all()");
            goto finally;
        }
    }

    
    
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