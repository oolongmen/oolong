cmake_minimum_required(VERSION 2.8)

project(oolong-test)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-Wall -g")

include_directories(.)
include_directories(..)

set(rpc_server_src
    ../oolong.h
    ../buffer/buffer.h
    ../buffer/buffer.cpp
    ../json-rpc/rpc_server.h
    ../json-rpc/rpc_server.cpp
    ./rpc-test-server.cpp)

add_executable(rpc-test-server ${rpc_server_src})

target_link_libraries(rpc-test-server -static-libgcc -static-libstdc++ event pthread)

set(rpc_client_src
    ../oolong.h
    ../buffer/buffer.h
    ../buffer/buffer.cpp
    ../json-rpc/rpc_client.h
    ../json-rpc/rpc_client.cpp
    ./rpc-test-client.cpp)

add_executable(rpc-test-client ${rpc_client_src})

target_link_libraries(rpc-test-client -static-libgcc -static-libstdc++ event pthread)
