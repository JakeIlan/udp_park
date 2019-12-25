cmake_minimum_required(VERSION 3.15)
project(lexa_udp C)

set(CMAKE_C_STANDARD 99)

add_executable(lexa_udp client.c declaration.h server.c dexchange.h)