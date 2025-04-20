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

// 互斥锁测试线程函数
void *mempool_lock_test_thread(void *arg) {
    lock_test_arg_t *params = (lock_test_arg_t *)arg;
    mempool_t *pool = params->pool;
    int thread_id = params->thread_id;
    int iterations = params->iterations;
    int *success_count = params->success_count;
    int *error_count = params->error_count;
    uint8_t pattern = params->pattern;
    
    DEBUG_PRINT("Thread %d starting with %d iterations", thread_id, iterations);
    
    // 等待所有线程就绪
    pthread_barrier_wait(params->barrier);
    
    for (int i = 0; i < iterations; i++) {
        // 随机分配或释放操作
        if (rand() % 2) {
            // 分配操作
            uint8_t *block = mempool_alloc(pool, false);
            if (block) {
                // 初始化头部信息
                init_block_header(block, pool->block_size, pattern);
                
                // 填充数据区（跳过头部）
                uint8_t *data_area = block + HEADER_SIZE;
                size_t data_size = pool->block_size - HEADER_SIZE;
                memset(data_area, pattern, data_size);
                
                // 随机延迟，增加竞争机会
                MEMPOOL_DELAY_MS((rand() % 10)); // 0-10ms
                
                // 验证内存完整性
                if (!verify_block_header(block, pool->block_size, pattern)) {
                    __sync_fetch_and_add(error_count, 1);
                    mempool_free(pool, block);
                    continue;
                }
                
                // 验证数据区
                bool data_ok = true;
                for (size_t j = 0; j < data_size; j++) {
                    if (data_area[j] != pattern) {
                        ERROR_PRINT("Thread %d: Data corruption at offset %zu (expected 0x%02X, got 0x%02X)", 
                                  thread_id, j, pattern, data_area[j]);
                        data_ok = false;
                        break;
                    }
                }
                
                if (!data_ok) {
                    __sync_fetch_and_add(error_count, 1);
                    mempool_free(pool, block);
                    continue;
                }
                
                mempool_free(pool, block);
                __sync_fetch_and_add(success_count, 1);
            }
        } else {
            // 释放操作(需要先分配一个)
            uint8_t *block = mempool_alloc(pool, false);
            if (block) {
                init_block_header(block, pool->block_size, pattern);
                uint8_t *data_area = block + HEADER_SIZE;
                size_t data_size = pool->block_size - HEADER_SIZE;
                memset(data_area, pattern, data_size);
                
                MEMPOOL_DELAY_MS((rand() % 10));
                
                // 验证内存完整性
                if (!verify_block_header(block, pool->block_size, pattern)) {
                    __sync_fetch_and_add(error_count, 1);
                    mempool_free(pool, block);
                    continue;
                }
                
                mempool_free(pool, block);
                __sync_fetch_and_add(success_count, 1);
            }
        }
    }
    
    DEBUG_PRINT("Thread %d completed", thread_id);
    return NULL;
}

// 线程安全测试函数
void test_mempool_thread_safety() {
    DEBUG_PRINT("=== Testing mempool thread safety ===");
    
    const int NUM_THREADS = 4;
    const int ITERATIONS = 1000;
    pthread_t threads[NUM_THREADS];
    lock_test_arg_t args[NUM_THREADS];
    int success_count = 0;
    int error_count = 0;
    pthread_barrier_t barrier;
    
    // 创建内存池（块大小需要足够容纳头部信息）
    // const size_t TEST_BLOCK_SIZE = 256; // 示例大小
    // const size_t TEST_BLOCK_COUNT = 100;
    mempool_t *pool = mempool_create(256, 100);
    MEMPOOL_ASSERT(pool != NULL);
    
    // 初始化屏障
    pthread_barrier_init(&barrier, NULL, NUM_THREADS + 1);
    
    // 创建测试线程
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i] = (lock_test_arg_t){
            .pool = pool,
            .thread_id = i,
            .iterations = ITERATIONS,
            .success_count = &success_count,
            .error_count = &error_count,
            .barrier = &barrier,
            .pattern = 0x55 + i // 每个线程使用不同的填充模式
        };
        pthread_create(&threads[i], NULL, mempool_lock_test_thread, &args[i]);
    }
    
    // 等待所有线程就绪
    pthread_barrier_wait(&barrier);
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // 检查结果
    size_t expected_operations = NUM_THREADS * ITERATIONS;
    float success_rate = (float)success_count / expected_operations * 100;
    float error_rate = (float)error_count / expected_operations * 100;
    
    DEBUG_PRINT("Thread safety test results:");
    DEBUG_PRINT("  Success: %d (%.1f%%)", success_count, success_rate);
    DEBUG_PRINT("  Errors:  %d (%.1f%%)", error_count, error_rate);
    DEBUG_PRINT("  Total:   %d operations", expected_operations);
    
    // 清理
    pthread_barrier_destroy(&barrier);
    mempool_destroy(pool);
    
    MEMPOOL_ASSERT(error_count == 0);
    DEBUG_PRINT("Thread safety test %s!", error_count == 0 ? "passed" : "failed");
}

// 模拟中断处理函数
void *simulate_irq_handler(void *arg) {
    mempool_t *pool = (mempool_t*)arg;
    while (!stop_test) {
        // 随机触发"中断"
        MEMPOOL_DELAY_US(rand() % 100); // 50-150us间隔
        
        void *block = mempool_alloc(pool, true); // 强制使用中断锁
        if (block) {
            // 模拟中断处理中的DMA操作
            simulate_dma_transfer(block);
            mempool_free(pool, block);
        }
    }
    return NULL;
}

void test_interrupt_context_operations() {
    DEBUG_PRINT("=== Interrupt Context Simulation Test ===");
    
    mempool_t *pool = mempool_create(512, 64); // 适合DMA的小池
    pthread_t irq_thread, normal_thread;
    
    // 启动中断模拟线程
    pthread_create(&irq_thread, NULL, simulate_irq_handler, pool);
    
    // 正常线程操作
    pthread_create(&normal_thread, NULL, [](void *arg) -> void* {
        mempool_t *pool = (mempool_t*)arg;
        for (int i = 0; i < 100000; i++) {
            void *block = mempool_alloc(pool, false);
            if (block) {
                process_data(block); // 正常处理
                mempool_free(pool, block);
            }
            MEMPOOL_DELAY_US(10);
        }
        stop_test = true;
        return NULL;
    }, pool);
    
    pthread_join(irq_thread, NULL);
    pthread_join(normal_thread, NULL);
    
    mempool_destroy(pool);
}

// 时间关键型操作测试
void test_time_critical_operations() {
    DEBUG_PRINT("=== Time-Critical Operation Test ===");
    
    const int TEST_DURATION_MS = 5000; // 5秒测试
    const size_t BLOCK_SIZE = 256;
    
    mempool_t *pool = mempool_create(BLOCK_SIZE, 32);
    uint64_t total_alloc_time = 0, total_free_time = 0;
    int operations = 0;
    
    auto start = high_resolution_clock::now();
    while (duration_cast<milliseconds>(high_resolution_clock::now() - start).count() < TEST_DURATION_MS) {
        auto t1 = high_resolution_clock::now();
        void *block = mempool_alloc(pool, true);
        auto t2 = high_resolution_clock::now();
        
        if (block) {
            simulate_dma_operation(block);
            
            auto t3 = high_resolution_clock::now();
            mempool_free(pool, block);
            auto t4 = high_resolution_clock::now();
            
            total_alloc_time += duration_cast<nanoseconds>(t2 - t1).count();
            total_free_time += duration_cast<nanoseconds>(t4 - t3).count();
            operations++;
        }
    }
    
    DEBUG_PRINT("Performance results after %d operations:", operations);
    DEBUG_PRINT("  Average allocation time: %.2f ns", (double)total_alloc_time/operations);
    DEBUG_PRINT("  Average free time: %.2f ns", (double)total_free_time/operations);
    DEBUG_PRINT("  Total throughput: %.2f ops/ms", operations/(double)TEST_DURATION_MS);
    
    mempool_destroy(pool);
}

// 内存碎片化抗性测试
void test_fragmentation_resistance() {
    DEBUG_PRINT("=== Fragmentation Resistance Test ===");
    
    const size_t BLOCK_SIZE = 128;
    const int BLOCK_COUNT = 1000;
    const int ITERATIONS = 10000;
    
    mempool_t *pool = mempool_create(BLOCK_SIZE, BLOCK_COUNT);
    vector<void*> allocated_blocks;
    allocated_blocks.reserve(BLOCK_COUNT);
    
    for (int i = 0; i < ITERATIONS; i++) {
        // 随机分配或释放
        if (rand() % 2 || allocated_blocks.empty()) {
            if (allocated_blocks.size() < BLOCK_COUNT) {
                void *block = mempool_alloc(pool, false);
                if (block) {
                    allocated_blocks.push_back(block);
                }
            }
        } else {
            size_t index = rand() % allocated_blocks.size();
            mempool_free(pool, allocated_blocks[index]);
            allocated_blocks.erase(allocated_blocks.begin() + index);
        }
        
        // 每100次检查一次连续性
        if (i % 100 == 0) {
            size_t contiguous = check_contiguous_blocks(pool);
            DEBUG_PRINT("Iteration %d: %zu contiguous blocks available", i, contiguous);
        }
    }
    
    mempool_destroy(pool);
}

// 多优先级任务竞争测试
void test_priority_inversion() {
    DEBUG_PRINT("=== Priority Inversion Test ===");
    
    mempool_t *pool = mempool_create(256, 10); // 小池增加竞争
    
    // 高优先级任务(模拟中断)
    pthread_attr_t high_attr;
    pthread_attr_init(&high_attr);
    pthread_attr_setschedpolicy(&high_attr, SCHED_FIFO);
    sched_param high_param = { .sched_priority = 99 };
    pthread_attr_setschedparam(&high_attr, &high_param);
    
    // 低优先级任务
    pthread_attr_t low_attr;
    pthread_attr_init(&low_attr);
    pthread_attr_setschedpolicy(&low_attr, SCHED_OTHER);
    
    pthread_t high_thread, low_thread;
    
    // 低优先级先获取锁
    pthread_create(&low_thread, &low_attr, [](void* arg) -> void* {
        mempool_t *pool = (mempool_t*)arg;
        void *block = mempool_alloc(pool, true);
        if (block) {
            DEBUG_PRINT("Low-priority thread got block, holding for 100ms");
            MEMPOOL_DELAY_MS(100); // 长时间持有
            mempool_free(pool, block);
        }
        return NULL;
    }, pool);
    
    MEMPOOL_DELAY_MS(10); // 确保低优先级先运行
    
    // 高优先级尝试获取
    pthread_create(&high_thread, &high_attr, [](void* arg) -> void* {
        mempool_t *pool = (mempool_t*)arg;
        auto start = high_resolution_clock::now();
        void *block = mempool_alloc(pool, true);
        auto end = high_resolution_clock::now();
        
        if (block) {
            DEBUG_PRINT("High-priority thread got block after %lld ms", 
                      duration_cast<milliseconds>(end-start).count());
            mempool_free(pool, block);
        }
        return NULL;
    }, pool);
    
    pthread_join(high_thread, NULL);
    pthread_join(low_thread, NULL);
    
    mempool_destroy(pool);
}

int main() {
    DEBUG_PRINT("Starting comprehensive memory pool tests");
    
    test_mempool_thread_safety();

    test_interrupt_context_operations();

    test_interrupt_context_operations();
    test_time_critical_operations();
    test_fragmentation_resistance();
    test_priority_inversion();

    DEBUG_PRINT("All memory pool tests passed successfully!");
    return 0;
}
