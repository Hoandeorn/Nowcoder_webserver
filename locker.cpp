//
// Created by 黄志鸿 on 2022/8/3.
//

#include "locker.h"

locker::locker(){
    if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
        throw std::exception();
    }
}

locker::~locker(){
    pthread_mutex_destroy(&m_mutex);
}

cond::cond(){
    if (pthread_cond_init(&m_cond, nullptr) != 0) {
        throw std::exception();
    }
}

cond::~cond(){
    pthread_cond_destroy(&m_cond);
}

sem::sem(){
    if (sem_init(&m_sem, 0, 0) != 0){
        throw std::exception();
    }
}

sem::sem(int num){
    if (sem_init(&m_sem, 0, num) != 0){
        throw std::exception();
    }
}

sem::~sem(){
    sem_destroy(&m_sem);
}