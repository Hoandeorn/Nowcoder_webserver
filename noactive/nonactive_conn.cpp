//
// Created by 黄志鸿 on 2022/8/17.
//
#include <arpa/inet.h>
#include <libgen.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <cerrno>
#include "lst_timer.h"

#define MAX_EVENT_NUMBER 1024
#define FD_LIMIT 65535
#define TIMESLOT 5

static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

void setnonblocking(int fd);

void addfd(int epollfd, int fd);

void addsig(int sig);

void cb_func(client_data *user_data);

void timer_handler();

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("Usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    int port = atoi(argv[1]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*) &address, sizeof address);
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd);

    // establish pipe
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]); // write
    addfd(epollfd, pipefd[0]); // read

    // signal process
    addsig(SIGALRM); // timer expires.
    addsig(SIGTERM); // terminate.
    bool stop_server = false;

    auto* users = new client_data[FD_LIMIT];
    bool timeout = false;
    alarm(TIMESLOT); // Generate SIGALRM after 5 secs.

    while(!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ( (number < 0) && (errno != EINTR) ) {
            printf("EPOLL failure\n");
            break;
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*) &client_address, &client_addrlength);
                addfd(epollfd, connfd);
                users[connfd].address = client_address;
                users[connfd].sock_fd = connfd;

                // set a timer.
                auto *timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(nullptr);
                timer->expire = cur + 3 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer);
            } else if ( (sockfd == pipefd[0]) && (events[i].events & EPOLLIN) ) {
                // process signals.
                int sig;
                char signals[1024];
                ret = recv( pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1 || ret == 0) continue;
                else {
                    for(int j = 0; j < ret; j++) {
                        switch(signals[j]) {
                            case SIGALRM:{
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务. 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务.
                                timeout = true;
                                break;
                            }
                            case SIGTERM:{
                                // Termination.
                                stop_server = true;
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                memset( users[sockfd].buf, '\0', BUFFER_SIZE);
                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE-1, 0);
                printf("get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd);
                util_timer* timer = users[sockfd].timer;
                if (ret < 0) {
                    // Read error. Close connection and remove the timer.
                    if (errno != EAGAIN) {
                        cb_func( &users[sockfd] );
                        if (timer) timer_lst.del_timer(timer);
                    }
                } else if (ret == 0) {
                    // If the connection is shut down by the client. Close connection and remove the timer.
                    cb_func( &users[sockfd] );
                    if (timer) timer_lst.del_timer(timer);
                } else {
                    // Data to be read from the client. Adjust the timer to delay.
                    if (timer) {
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_lst.adjust_timer(timer);
                    }
                }
            }
        }
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete [] users;
    return 0;
}

// Scheduled processing tasks.
void timer_handler() {
    timer_lst.tick();
    alarm(TIMESLOT);
}

void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*) &msg, 1, 0); // write
    errno = save_errno;
}

void addsig(int sig) {
    struct sigaction sa; // examine and change a signal action
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART; // 由此信号中断的系统调用会自动重启
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    old_option |= O_NONBLOCK;
    fcntl(fd, F_SETFL, old_option);
}

void cb_func(client_data *user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sock_fd, 0);
    assert(user_data);
    close(user_data->sock_fd);
    printf("close fd %d\n", user_data->sock_fd);
}
