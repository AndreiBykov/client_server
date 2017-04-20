cmake_minimum_required(VERSION 3.6)
project(client_server)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

set(SOURCE_FILES main.cpp)
add_executable(client_server ${SOURCE_FILES})