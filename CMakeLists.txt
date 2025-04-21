cmake_minimum_required(VERSION 3.10)
project(mempool LANGUAGES C)

# 添加控制是否编译测试的选项
option(MEMPOOL_BUILD_TESTS "Build mempool test cases" OFF)

# 创建mempool库
add_library(mempool
    src/mempool.c
)

# 设置头文件目录
target_include_directories(mempool
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 启用调试信息
target_compile_definitions(mempool
    PUBLIC
    MEMPOOL_DEBUG=1
)

# 条件编译测试代码
if(MEMPOOL_BUILD_TESTS)
    message(STATUS "Building mempool tests")
    add_executable(mempool_test
        test/basic_test.c
        test/test_mempool.c
    )
    target_link_libraries(mempool_test mempool)
    
    # 如果需要，可以在这里添加测试
    # enable_testing()
    # add_test(NAME mempool_test COMMAND mempool_test)
else()
    message(STATUS "Skipping mempool tests")
endif()