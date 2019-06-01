cmake_minimum_required(VERSION 3.13)
project(dropbox_client C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
include_directories(.)

add_executable(dropbox_client main.c list.c requests.c client.c handlers.c file.c buffer.c)
