add_executable(example examples.cpp)

target_include_directories(example PUBLIC ../include)

target_link_libraries(example eventloop)

target_compile_options(example PRIVATE
    -O0            # 关闭优化，便于单步
    -g3 -ggdb      # 最大级别调试信息
    -fno-omit-frame-pointer
)
target_link_options(example PRIVATE
    -g
)
