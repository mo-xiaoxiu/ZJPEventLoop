cmake_minimum_required(VERSION 3.10)
project(EventLoopExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Debug configuration for GDB
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

# Include directories
include_directories(../include)

# Add the EventLoop library
add_library(EventLoop ../src/EventLoop.cpp)

# Link libraries
target_link_libraries(EventLoop pthread)

# Original examples executable
add_executable(examples examples.cpp)
target_link_libraries(examples EventLoop)

# New timer example executable
add_executable(timer_example timer_example.cpp)
target_link_libraries(timer_example EventLoop)
