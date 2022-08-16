cmake_minimum_required(VERSION 3.21)
project(webserver)

set(CMAKE_CXX_STANDARD 14)

add_executable(webserver main.cpp locker.cpp locker.h threadpool.h http_conn.cpp http_conn.h)
