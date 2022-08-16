#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <libgen.h>
#include <csignal>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000

// signal capture
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

// add fd to epoll
extern void addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]) {

    if (argc <= 1) {
        printf("Run as: %s port number\n", basename(argv[0]));
        exit(-1);
    }

    int port = atoi(argv[1]);

    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn> * pool = nullptr;
    try{
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    http_conn *users = new http_conn[MAX_FD];

    // monitor socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // Port multiplexing
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    // Bind
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // any address
    address.sin_port = htons(port); // host to network sequence
    bind(listenfd, (struct sockaddr*)&address, sizeof address);

    // Monitor
    listen(listenfd, 5);

    // Create epoll objects and event array
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && (errno != EINTR)) {
            printf("EPOLL failure.\n");
            break;
        }

        // traverse all the events.
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*) &client_address, &client_addrlen);

                if (http_conn::m_user_count >= MAX_FD) {
                    // full
                    // write the client a message that the server is busy.
                    close(connfd);
                    continue;
                }

                // initialize the new client and put into the array.
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){ // disconnection from exception or error
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN) {
                // read all data at one time
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else{
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT){
                // write all data at one time
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }

            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}
