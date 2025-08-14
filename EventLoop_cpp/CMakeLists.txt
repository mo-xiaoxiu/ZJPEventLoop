cmake_minimum_required(VERSION 3.15)
project(EventLoop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(USE_ZJP_THREADPOOL "Enable ZJP thread pool integration" OFF)

add_library(eventloop
    src/EventLoop.cpp
)

target_include_directories(eventloop
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (USE_ZJP_THREADPOOL)
    message(STATUS "Building with ZJP thread pool support")
    # Headers
    target_include_directories(eventloop PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/ZJPTools/install/include
    )
    # Libraries
    # Prefer static if available, else link shared by name
    set(ZJP_THREADLOOP_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/ZJPTools/install/lib)
    if (EXISTS ${ZJP_THREADLOOP_LIB_DIR}/libzjpThreadloop.a)
        target_link_libraries(eventloop PUBLIC ${ZJP_THREADLOOP_LIB_DIR}/libzjpThreadloop.a)
    else()
        link_directories(${ZJP_THREADLOOP_LIB_DIR})
        target_link_libraries(eventloop PUBLIC zjpThreadloop)
        # Help runtime find .so in build tree
        set_target_properties(eventloop PROPERTIES BUILD_RPATH ${ZJP_THREADLOOP_LIB_DIR})
    endif()
    target_compile_definitions(eventloop PUBLIC USE_ZJP_THREADPOOL)
endif()

set_target_properties(eventloop PROPERTIES
    OUTPUT_NAME "eventloop"
)

# examples
add_subdirectory(examples)

target_link_options(eventloop PRIVATE
    $<$<CONFIG:Debug>:-g>
)
