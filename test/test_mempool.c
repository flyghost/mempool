#include <mempool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MEMPOOL_LOG_LEVEL
#undef MEMPOOL_LOG_LEVEL
#endif

#define MEMPOOL_LOG_LEVEL LOG_LEVEL_DEBUG
#include <mempool_log.h>

// 测试参数配置
#ifndef TEST_BLOCK_SIZE
#define TEST_BLOCK_SIZE 64
#endif

#ifndef TEST_BLOCK_COUNT
#define TEST_BLOCK_COUNT 256
#endif

// 获取调整后的测试块数量
size_t get_test_block_count() {
    size_t max_test_blocks = TEST_BLOCK_COUNT;
    if (max_test_blocks > MEMPOOL_MAX_BLOCKS) {
        WARNING_PRINT("Reducing test blocks from %zu to %d due to MEMPOOL_MAX_BLOCKS",
                    max_test_blocks, MEMPOOL_MAX_BLOCKS);
        return MEMPOOL_MAX_BLOCKS;
    }
    return max_test_blocks;
}

// 测试内存池基本功能
void test_mempool_basic() {
    DEBUG_PRINT("=== Testing mempool basic operations ===");

    size_t test_blocks = get_test_block_count();
    if (test_blocks < 2) {
        WARNING_PRINT("Skipping basic test - insufficient MEMPOOL_MAX_BLOCKS (%d)", MEMPOOL_MAX_BLOCKS);
        return;
    }

    // 创建内存池
    mempool_t *pool = mempool_create(TEST_BLOCK_SIZE, test_blocks);
    MEMPOOL_ASSERT(pool != NULL);

    // 测试分配与释放
    uint8_t *blocks[test_blocks];
    for (size_t i = 0; i < test_blocks; i++) {
        blocks[i] = mempool_alloc(pool, i % 2); // 交替分配普通和HW块
        MEMPOOL_ASSERT(blocks[i] != NULL);
        MEMPOOL_ASSERT(mempool_available(pool) == test_blocks - i - 1);
    }

    // 尝试超额分配
    uint8_t *extra_block = mempool_alloc(pool, false);
    MEMPOOL_ASSERT(extra_block == NULL);

    // 释放所有块
    for (size_t i = 0; i < test_blocks; i++) {
        mempool_free(pool, blocks[i]);
        MEMPOOL_ASSERT(mempool_available(pool) == i + 1);
    }

    // 销毁内存池
    mempool_destroy(pool);
    DEBUG_PRINT("Basic mempool test passed!");
}

// 增强版队列测试
void test_mempool_queue_enhanced() {
    DEBUG_PRINT("=== Enhanced mempool queue testing ===");

    size_t test_blocks = get_test_block_count();
    if (test_blocks < 4) {
        ERROR_PRINT("Skipping enhanced queue test - insufficient MEMPOOL_MAX_BLOCKS (%d)", MEMPOOL_MAX_BLOCKS);
        return;
    }

    // 创建内存池和队列
    mempool_t *pool = mempool_create(TEST_BLOCK_SIZE, test_blocks);
    MEMPOOL_ASSERT(pool != NULL);
    
    const size_t queue_capacity = 3; // 固定测试队列容量
    mempool_queue_t *queue = mempool_queue_create(pool, queue_capacity);
    MEMPOOL_ASSERT(queue != NULL);

    // 测试1: 入队出队顺序一致性
    DEBUG_PRINT("Testing FIFO order consistency...");
    uint8_t *blocks[queue_capacity];
    for (int i = 0; i < queue_capacity; i++) {
        blocks[i] = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(blocks[i] != NULL);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, blocks[i]) == 0);
    }

    for (int i = 0; i < queue_capacity; i++) {
        uint8_t *dequeued = mempool_queue_dequeue(queue);
        MEMPOOL_ASSERT(dequeued == blocks[i]);
        mempool_free(pool, dequeued);
    }

    // 测试2: 队列满时入队应失败
    DEBUG_PRINT("Testing full queue behavior...");
    for (int i = 0; i < queue_capacity; i++) {
        blocks[i] = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(blocks[i] != NULL);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, blocks[i]) == 0);
    }
    
    uint8_t *extra_block = mempool_alloc(pool, false);
    if (extra_block != NULL) {
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, extra_block) == -1);
        mempool_free(pool, extra_block);
    }

    // 测试3: 空队列出队应返回NULL
    DEBUG_PRINT("Testing empty queue behavior...");
    for (int i = 0; i < queue_capacity; i++) {
        uint8_t *dequeued = mempool_queue_dequeue(queue);
        MEMPOOL_ASSERT(dequeued != NULL);
        mempool_free(pool, dequeued);
    }
    
    MEMPOOL_ASSERT(mempool_queue_dequeue(queue) == NULL);

    // 测试4: 混合操作
    DEBUG_PRINT("Testing mixed operations...");
    for (int cycle = 0; cycle < 2; cycle++) {
        // 入队两个
        uint8_t *b1 = mempool_alloc(pool, false);
        uint8_t *b2 = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, b1) == 0);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, b2) == 0);
        
        // 出队一个
        uint8_t *d1 = mempool_queue_dequeue(queue);
        MEMPOOL_ASSERT(d1 == b1);
        mempool_free(pool, d1);
        
        // 再入队一个
        uint8_t *b3 = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, b3) == 0);
        
        // 出队剩余两个
        uint8_t *d2 = mempool_queue_dequeue(queue);
        uint8_t *d3 = mempool_queue_dequeue(queue);
        MEMPOOL_ASSERT(d2 == b2);
        MEMPOOL_ASSERT(d3 == b3);
        mempool_free(pool, d2);
        mempool_free(pool, d3);
    }

    // 清理
    mempool_queue_destroy(queue);
    mempool_destroy(pool);
    DEBUG_PRINT("Enhanced queue test passed!");
}

// 边界条件测试
void test_mempool_edge_cases() {
    DEBUG_PRINT("=== Testing mempool edge cases ===");

    // 测试1: 创建0块内存池
    mempool_t *pool = mempool_create(TEST_BLOCK_SIZE, 0);
    MEMPOOL_ASSERT(pool == NULL);

    // 测试2: 最小内存池(1块)
    if (MEMPOOL_MAX_BLOCKS >= 1) {
        pool = mempool_create(TEST_BLOCK_SIZE, 1);
        MEMPOOL_ASSERT(pool != NULL);
        
        uint8_t *block = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(block != NULL);
        MEMPOOL_ASSERT(mempool_alloc(pool, false) == NULL);
        
        // 测试释放后重用
        mempool_free(pool, block);
        uint8_t *new_block = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(new_block != NULL);
        mempool_free(pool, new_block);
        
        mempool_destroy(pool);
    }

    // 测试3: 队列边界
    if (MEMPOOL_MAX_BLOCKS >= 2) {
        pool = mempool_create(TEST_BLOCK_SIZE, 2);
        MEMPOOL_ASSERT(pool != NULL);
        
        // 创建容量为1的队列
        mempool_queue_t *queue = mempool_queue_create(pool, 1);
        MEMPOOL_ASSERT(queue != NULL);
        
        // 入队一个
        uint8_t *b1 = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, b1) == 0);
        MEMPOOL_ASSERT(mempool_queue_enqueue(queue, b1) == -1); // 重复入队
        
        // 出队
        uint8_t *d1 = mempool_queue_dequeue(queue);
        MEMPOOL_ASSERT(d1 == b1);
        
        // 空队列出队
        MEMPOOL_ASSERT(mempool_queue_dequeue(queue) == NULL);
        
        mempool_queue_destroy(queue);
        mempool_destroy(pool);
    }

    DEBUG_PRINT("Edge case test passed!");
}

// 内存内容测试
void test_mempool_memory_content() {
    DEBUG_PRINT("=== Testing memory content integrity ===");

    size_t test_blocks = get_test_block_count();
    if (test_blocks < 2) {
        WARNING_PRINT("Skipping memory content test - insufficient blocks");
        return;
    }

    // 创建内存池
    mempool_t *pool = mempool_create(TEST_BLOCK_SIZE, test_blocks);
    MEMPOOL_ASSERT(pool != NULL && "mempool_create failed");

    // 分配两个内存块
    uint8_t *block1 = mempool_alloc(pool, false);
    uint8_t *block2 = mempool_alloc(pool, false);
    MEMPOOL_ASSERT(block1 != NULL && block2 != NULL && "mempool_alloc failed");

    // 测试1: 验证内存块可写性
    const char *test_str = "Memory pool test string";
    size_t str_len = strlen(test_str) + 1;
    
    // 写入不同模式的数据到两个块
    memcpy(block1, test_str, str_len);          // block1写入字符串
    memset(block2, 0xAA, TEST_BLOCK_SIZE);      // block2填充0xAA

    // 验证写入的数据
    MEMPOOL_ASSERT(memcmp(block1, test_str, str_len) == 0 && "Data verification failed for block1");
    
    for (size_t i = 0; i < TEST_BLOCK_SIZE; i++) {
        MEMPOOL_ASSERT(block2[i] == 0xAA && "Data verification failed for block2");
    }

    // 测试2: 验证内存块独立性
    // 修改block1不应该影响block2
    memset(block1, 0x55, TEST_BLOCK_SIZE);
    for (size_t i = 0; i < TEST_BLOCK_SIZE; i++) {
        MEMPOOL_ASSERT(block2[i] == 0xAA && "block2 corrupted when modifying block1");
    }

    // 测试3: 验证释放后重新分配的内存可用性（不假设内容状态）
    mempool_free(pool, block1);
    uint8_t *new_block = mempool_alloc(pool, false);
    MEMPOOL_ASSERT(new_block != NULL && "re-allocation failed");
    
    // 只验证新块是否可写，不假设其初始内容
    memset(new_block, 0x77, TEST_BLOCK_SIZE);
    for (size_t i = 0; i < TEST_BLOCK_SIZE; i++) {
        MEMPOOL_ASSERT(new_block[i] == 0x77 && "re-allocated block write failed");
    }

    // 清理
    mempool_free(pool, block2);
    mempool_free(pool, new_block);
    mempool_destroy(pool);
    DEBUG_PRINT("Memory content test passed!");
}

void test_mempool_all_blocks_isolation() {
    DEBUG_PRINT("=== Testing isolation across ALL memory blocks (%d blocks) ===", 
               MEMPOOL_MAX_BLOCKS);

    // 创建内存池
    mempool_t *pool = mempool_create(TEST_BLOCK_SIZE, MEMPOOL_MAX_BLOCKS);
    MEMPOOL_ASSERT(pool != NULL);

    // 分配所有内存块
    uint8_t *blocks[MEMPOOL_MAX_BLOCKS];
    for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
        blocks[i] = mempool_alloc(pool, false);
        MEMPOOL_ASSERT(blocks[i] != NULL);
    }

    // 测试1: 初始化所有块为唯一模式
    DEBUG_PRINT("Initializing all blocks with unique patterns...");
    for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
        memset(blocks[i], 0xA0 + (i % 0x5F), TEST_BLOCK_SIZE);
    }

    // 保存初始状态
    uint8_t *initial_blocks[MEMPOOL_MAX_BLOCKS];
    for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
        initial_blocks[i] = malloc(TEST_BLOCK_SIZE);
        memcpy(initial_blocks[i], blocks[i], TEST_BLOCK_SIZE);
    }

    // 测试2: 修改每个块并验证其他块
    DEBUG_PRINT("Modifying and verifying each block in isolation...");
    for (int target_block = 0; target_block < MEMPOOL_MAX_BLOCKS; target_block++) {
        // 恢复所有块到初始状态
        for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
            memcpy(blocks[i], initial_blocks[i], TEST_BLOCK_SIZE);
        }
        
        // 修改目标块
        memset(blocks[target_block], 0xF0 + (target_block % 0x0F), TEST_BLOCK_SIZE);

        // 验证其他所有块
        for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
            if (i == target_block) continue;

            for (size_t j = 0; j < TEST_BLOCK_SIZE; j++) {
                if (blocks[i][j] != initial_blocks[i][j]) {
                    ERROR_PRINT("Block %d corrupted when modifying block %d at offset %zu: "
                              "expected 0x%02X, got 0x%02X",
                              i, target_block, j, initial_blocks[i][j], blocks[i][j]);
                    // 打印更多调试信息
                    ERROR_PRINT("Target block content at offset %zu: 0x%02X", 
                              j, blocks[target_block][j]);
                    MEMPOOL_ASSERT(0 && "Cross-block interference detected");
                }
            }
        }
    }

    // 释放资源
    for (int i = 0; i < MEMPOOL_MAX_BLOCKS; i++) {
        free(initial_blocks[i]);
        mempool_free(pool, blocks[i]);
    }
    mempool_destroy(pool);
    DEBUG_PRINT("All-block isolation test passed successfully!");
}

int main() {
    DEBUG_PRINT("Starting comprehensive memory pool tests");
    
    test_mempool_basic();
    test_mempool_queue_enhanced();
    test_mempool_edge_cases();
    test_mempool_memory_content();
    test_mempool_all_blocks_isolation();

    DEBUG_PRINT("All memory pool tests passed successfully!");
    return 0;
}
