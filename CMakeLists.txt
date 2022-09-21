cmake_minimum_required(VERSION 3.24.2)
project(webserver C)

set(CMAKE_C_STANDARD 11)

add_executable(webserver
        main.c
        string.c
        string.h
        socket.c
        socket.h
        http.h)

target_link_libraries(webserver pthread)