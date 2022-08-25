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

#define LISTEN_BACKLOG 50
#define MAX_EVENTS 10

static bool server_status = false;

struct socket_handler_data {
    int client_fd;
};

// static void *accepted_socket_handler(void *arg)
// {
//     struct socket_handler_data data = *((struct socket_handler_data *)arg);
//     free(arg);
    // size_t buf_capacity = 8;
    // size_t buf_length = 0;
    // int line_feed_index = -1;
    // char *buf = (char *)malloc(buf_capacity);
    // for (;;) {
    //     int received_length = recv(data.client_fd, buf + buf_length, buf_capacity - buf_length, 0);

    //     if (received_length == -1) {
    //         perror("recv");
    //         exit(EXIT_FAILURE);
    //     }

    //     printf("[%d] received_length=%d\n", received_length, received_length);

    //     line_feed_index = -1;
    //     for (int i = buf_length; i < buf_length + received_length; ++i) {
    //         if (buf[i] == '\n') {
    //             line_feed_index = i;
    //             break;
    //         }
    //     }
    //     if (line_feed_index != -1) {
    //         break;
    //     }
        
    //     buf_length += received_length;
    //     if (buf_length == buf_capacity) {
    //         buf_capacity *= 2;
    //         buf = (char *)realloc(buf, buf_capacity);
    //     }
    //     printf("received_length: %d\n", received_length);
    // }

    // buf[line_feed_index - 1] = '\0';
    // printf("[%d] %s\n", data.client_fd, buf);
    // char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!";
    // if (send(data.client_fd, http_response, strlen(http_response), 0) == -1) {
    //     perror("send");
    // }
    
//     close(data.client_fd);

//     return  NULL;
// }

static void setnonblocking(int sock)
{
    int flag = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flag | O_NONBLOCK);
}

static void do_use_fd(int client_fd)
{
    printf("client fd: %d\n", client_fd);
    size_t buf_capacity = 1024;
    size_t buf_length = 0;
    int line_feed_index = -1;
    int recv_len = 0;
    char *buf = (char *)malloc(buf_capacity);
    for (;;) {
        recv_len = recv(client_fd, buf + buf_length, buf_capacity - buf_length, 0);

        if (recv_len == -1) {
            perror("recv");
            exit(EXIT_FAILURE);
        }

        line_feed_index = -1;
        for (int i = buf_length; i < buf_length + recv_len; ++i) {
            if (buf[i] == '\n') {
                line_feed_index = i;
                break;
            }
        }
        if (line_feed_index != -1) {
            break;
        }
        
        buf_length += recv_len;
        if (buf_length == buf_capacity) {
            buf_capacity *= 2;
            buf = (char *)realloc(buf, buf_capacity);
        }
    }

    buf[line_feed_index - 1] = '\0';
    printf("[%d] %s\n", client_fd, buf);
    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, World!!";
    if (send(client_fd, http_response, strlen(http_response), 0) == -1) {
        perror("send");
    }
}

int main(int argc, char *argv[])
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int client_fd;
    int epollfd, nfds, n;
    int port_number = 8080;
    if (server_fd == -1){
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1)
        perror("setsockopt(SO_REUSEADDR) failed");

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(struct sockaddr_in));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, LISTEN_BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in caddr;
    socklen_t caddr_len = sizeof(caddr);
    struct epoll_event ev, events[MAX_EVENTS];
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLET ;
    ev.data.fd = server_fd;

    if (epoll_ctl(epollfd,EPOLL_CTL_ADD, server_fd, &ev)) {
        perror("epoll_ctl: client_fd");
        exit(EXIT_FAILURE);
    }

    while (!server_status) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        printf("nfds: %d\n", nfds);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                client_fd = accept(server_fd, (struct sockaddr *)&caddr, &caddr_len);
                printf("client: %d\n", client_fd);

                if (client_fd == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                setnonblocking(client_fd);
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)  {
                    perror("epoll_ctl: client_fd");
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("else\n");
                do_use_fd(events[n].data.fd);
            }
        }
    }

    // while (!server_status) {
    //     client_fd = accept(server_fd, (struct sockaddr *)&caddr, &caddr_len);

    //     if (client_fd == -1) {
    //         perror("accept");
    //         exit(EXIT_FAILURE);
    //     }
        
    //     struct socket_handler_data *data = malloc(sizeof(struct socket_handler_data));
    //     data->client_fd = client_fd;

    //     pthread_t thread;
    //     int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, data);

    //     if (pthread_result == -1) {
    //         perror("pthread_create");
    //         exit(EXIT_FAILURE);
    //     }
    // }
    close(server_fd);
    // close(epollfd);
   
    return 0;
}