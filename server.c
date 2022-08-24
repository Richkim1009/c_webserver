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

#define LISTEN_BACKLOG 50
#define BUF_LEN 1024
#define MAX_EVENTS 10

static bool server_status = false;
static char buf[BUF_LEN];

static void setnonblocking(int sock)
{
    int flag = fcntl(sock,  F_GETFL, 0);
    fcntl(sock, F_SETFL, flag | O_NONBLOCK);
}

static int do_use_fd(int server_fd)
{
    int recv_len = read(server_fd, buf, BUF_LEN);
    buf[recv_len] = '\0';
    printf("%s", buf);
    return recv_len;
}

int main(int argc, char *argv[])
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int client_fd, epollfd, nfds, n;
    int port_number = 8080;
    if (server_fd == -1){
        perror("open");
        exit(EXIT_FAILURE);
    }
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

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epollfd,EPOLL_CTL_ADD, server_fd, &ev)) {
        perror("epoll_ctl: client_fd");
        exit(EXIT_FAILURE);
    }

    while (!server_status) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_pwait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                client_fd = accept(server_fd, (struct sockaddr *)&caddr, &caddr_len);

                if (client_fd == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                setnonblocking(client_fd);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)  {
                    perror("epoll_ctl: client_fd");
                    exit(EXIT_FAILURE);
                }
                printf("connect client: %d\n", client_fd);
            } else {
                int close_epoll = do_use_fd(events[n].data.fd);
                if (!close_epoll) {
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[n].data.fd, NULL);
                    close(events[n].data.fd);
                    printf("closed client: %d\n", events[n].data.fd);
                }
            }
        }
    }
    close(server_fd);
    close(epollfd);
   
    return 0;
}