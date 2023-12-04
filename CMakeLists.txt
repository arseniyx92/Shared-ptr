cmake_minimum_required(VERSION 3.25)
project(SharedPtr)

set(CMAKE_CXX_STANDARD 23)

include_directories(.)

add_executable(SharedPtr
        smart_pointers.h
        smartpointers_test.cpp)
