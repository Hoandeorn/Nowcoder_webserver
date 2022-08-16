//
// Created by 黄志鸿 on 2022/8/3.
//

#ifndef WEBSERVER_LOCKER_H
#define WEBSERVER_LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

//
class locker {
public:
    locker();
    ~locker();
    inline bool lock(){
        return pthread_mutex_lock(&m_mutex);
    };
    inline bool unlock(){
        return pthread_mutex_unlock(&m_mutex);
    }
    inline pthread_mutex_t *get(){
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class cond{
public:
    cond();
    ~cond();

    inline bool wait(pthread_mutex_t * mutex){
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    inline bool timedwait(pthread_mutex_t * mutex, struct timespec t){
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }

    inline bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }

    inline bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

class sem{
public:
    sem();
    sem(int num);
    ~sem();
    inline bool wait(){
        return sem_wait(&m_sem) == 0;
    }
    inline bool post(){
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem{};
};

#endif //WEBSERVER_LOCKER_H
