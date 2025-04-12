cmake_minimum_required(VERSION 3.10)
project(mempool LANGUAGES C)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 添加 mempool 库
add_library(mempool
    src/mempool.c
)

# 指定头文件目录
target_include_directories(mempool
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 添加测试可执行文件
add_executable(test_mempool
    test/test_mempool.c
)

# 链接测试程序到 mempool 库
target_link_libraries(test_mempool
    mempool
)

# 启用测试
enable_testing()
add_test(NAME mempool_test COMMAND test_mempool)

# 如果启用多线程测试，添加 pthread 支持
option(ENABLE_THREAD_TESTS "Enable thread safety tests" OFF)
if(ENABLE_THREAD_TESTS)
    find_package(Threads REQUIRED)
    target_link_libraries(test_mempool
        ${CMAKE_THREAD_LIBS_INIT}
    )
    target_compile_definitions(test_mempool
        PRIVATE
        MEMPOOL_THREAD_SAFE=1
    )
endif()

# 启用调试信息
target_compile_definitions(mempool
    PUBLIC
    MEMPOOL_DEBUG=1
)