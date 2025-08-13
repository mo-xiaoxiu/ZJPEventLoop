cmake_minimum_required(VERSION 3.15)
project(EventLoop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_library(eventloop
    src/EventLoop.cpp
)

target_include_directories(eventloop
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(eventloop PROPERTIES
    OUTPUT_NAME "eventloop"
)

# examples
add_subdirectory(examples)

target_link_options(eventloop PRIVATE
    $<$<CONFIG:Debug>:-g>
)

