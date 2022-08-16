//
// Created by 黄志鸿 on 2022/8/3.
//

#ifndef WEBSERVER_THREADPOOL_H
#define WEBSERVER_THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <cstdio>

// thread pool class.
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);

private:
    int m_thread_number;
    pthread_t *m_threads; //threads
    int m_max_requests;
    std::list<T*> m_workqueue;//work queue
    locker m_queuelocker; //mutex
    sem m_queuestat; //semaphore
    bool m_stop; // stop the pool

    static void* worker(void *arg);
    void run();

};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
        m_thread_number(thread_number), m_max_requests(max_requests),
        m_stop(false), m_threads(nullptr) {
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads){
        throw std::exception();
    }

    // detach, destroyed by itself.
    for(int i = 0; i < m_thread_number; i++){
        printf("Creating the %d th thread...\n", i);
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) { // # of workers exceed the limit, return false.
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void *arg){
    auto *pool = (threadpool*) arg;
    pool->run();
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait(); // if no value in sem, block in here.
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) continue;

        request->process();

    }
}

#endif //WEBSERVER_THREADPOOL_H
