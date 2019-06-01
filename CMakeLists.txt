cmake_minimum_required(VERSION 3.13)
project(dropbox_client C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
include_directories(.)

add_executable(dropbox_client main.c options.c list.c request.c client.c handler.c file.c buffer.c session.c connection.c)
