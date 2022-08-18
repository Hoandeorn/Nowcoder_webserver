//
// Created by 黄志鸿 on 2022/8/17.
//

#ifndef WEBSERVER_LST_TIMER_H
#define WEBSERVER_LST_TIMER_H

#include <ctime>
#include <cstdio>
#include <arpa/inet.h>

#define BUFFER_SIZE 64
class util_timer; // forward declaration

struct client_data{ // client data
    sockaddr_in address;
    int sock_fd;
    char buf[BUFFER_SIZE];
    util_timer *timer;
};

/*
 * class util_timer
 * A bidirectional list node.
 */
class util_timer {
public:
    util_timer(): prev(nullptr), next(nullptr){};

public:
    time_t expire; // Absolute time of expiration.
    void (*cb_func) (client_data* ); // function pointer.
    client_data* user_data;
    util_timer* prev;
    util_timer* next;
};

/*
 * class sort_timer_lst
 * Bidirectional list
 */
class sort_timer_lst{
public:
    sort_timer_lst(): head(nullptr), tail(nullptr) {};
    ~sort_timer_lst() {
        util_timer* tmp = head;
        while(tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(util_timer* timer) {
        if (!timer) return;
        if (!head) {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire) { // add at head
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    /*
     * If a timer changes, the list is adjusted.
     */
    void adjust_timer(util_timer* timer) {
        if (!timer) return;
        util_timer *tmp = timer->next;
        if (!tmp || timer->expire < tmp->expire) return;
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer, head);
        } else {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void del_timer(util_timer *timer) {
        if (!timer) return;
        if ((timer == head) && (timer == tail)) {
            delete timer;
            head = nullptr;
            tail = nullptr;
            return;
        }
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        if (timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    /*
     * Function tick() is executed when SIGALARM is triggered to process tasks due on the linked list.
     * */
    void tick(){
        if (!head) return;
        printf("timer tick.\n");
        time_t cur = time(nullptr);
        util_timer *tmp = head;
        while(tmp) {
            if (cur < tmp->expire) break;
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head) head->prev = nullptr;
            delete tmp;
            tmp = head;
        }
    }

private:

    void add_timer(util_timer *timer, util_timer *lst_head) {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while(tmp) { // add before tmp
            if (timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp) { // add at tail
            prev->next = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};


#endif //WEBSERVER_LST_TIMER_H
