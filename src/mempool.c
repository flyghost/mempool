#include "mempool.h"
#include <string.h>

#ifdef MEMPOOL_LOG_LEVEL
#undef MEMPOOL_LOG_LEVEL
#endif

#define MEMPOOL_LOG_LEVEL LOG_LEVEL_ERROR
#include <mempool_log.h>

// 查找第一个置位的位(编译器优化版本)
static inline int find_first_set_bit(BITMAP_TYPE word)
{
    if (word == 0) return -1;
    
#if defined(__riscv) && defined(__riscv_bitmanip)
    // RISC-V with B扩展支持
    int index;
    asm volatile ("bset %0, %1, zero" : "=r" (index) : "r" (word));
    return index;
#elif defined(__GNUC__) || defined(__clang__)
    // 使用编译器内置函数
    if (sizeof(BITMAP_TYPE) == 4) {
        return __builtin_ffs(word) - 1;
    } else if (sizeof(BITMAP_TYPE) == 8) {
        return __builtin_ffsll(word) - 1;
    }
#else
    // 通用实现
    for (int i = 0; i < sizeof(BITMAP_TYPE)*8; i++) {
        if (word & ((BITMAP_TYPE)1 << i)) return i;
    }
    return -1;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang编译器内置函数
    #define POPCOUNT_LL(x) __builtin_popcountll(x)
#elif defined(_MSC_VER)
    // MSVC编译器
    #include <intrin.h>
    #define POPCOUNT_LL(x) __popcnt64(x)
#else
    // 通用实现（无硬件加速）
    static inline int popcount_ll_generic(uint64_t x) {
        x = (x & 0x5555555555555555) + ((x >> 1)  & 0x5555555555555555);
        x = (x & 0x3333333333333333) + ((x >> 2)  & 0x3333333333333333);
        x = (x & 0x0F0F0F0F0F0F0F0F) + ((x >> 4)  & 0x0F0F0F0F0F0F0F0F);
        x = (x & 0x00FF00FF00FF00FF) + ((x >> 8)  & 0x00FF00FF00FF00FF);
        x = (x & 0x0000FFFF0000FFFF) + ((x >> 16) & 0x0000FFFF0000FFFF);
        return (x & 0x00000000FFFFFFFF) + ((x >> 32) & 0x00000000FFFFFFFF);
    }
    #define POPCOUNT_LL(x) popcount_ll_generic(x)
#endif

// 创建内存池
mempool_t *mempool_create(size_t data_size, size_t num_blocks)
{
    DEBUG_PRINT("Creating mempool: data_size=%zu, num_blocks=%zu", data_size, num_blocks);
    
    // 参数校验
    if (num_blocks == 0 || num_blocks > MEMPOOL_MAX_BLOCKS) {
        return NULL;
    }

    // 计算对齐后的块大小
    size_t aligned_size = (data_size + MEMPOOL_ALIGNMENT - 1) & ~(MEMPOOL_ALIGNMENT - 1);

    DEBUG_PRINT("Aligned block size: %zu", aligned_size);
    
    // 分配控制结构
    mempool_t *pool = MEMPOOL_MALLOC(sizeof(mempool_t));
    if (!pool) {
        ERROR_PRINT("Failed to allocate pool control structure");
        return NULL;
    }
    
    // 分配内存区域(保证对齐)
    pool->memory_area = MEMPOOL_MEMALIGN(MEMPOOL_ALIGNMENT, aligned_size * num_blocks);
    if (!pool->memory_area) {
        ERROR_PRINT("Failed to allocate memory area");
        MEMPOOL_FREE(pool);
        return NULL;
    }
    
    // 初始化参数
    pool->block_size = aligned_size;
    pool->block_count = num_blocks;
    
    // 初始化位图(全1表示空闲)
    for (int i = 0; i < BITMAP_WORDS; i++) {
        pool->free_bitmap[i] = (BITMAP_TYPE)(-1);
        pool->hw_owned_bitmap[i] = 0;

        DEBUG_PRINT("Free bitmap[%d] initialized to 0x%lx", i, pool->free_bitmap[i]);
    }
    
    // 处理非对齐的块数量(屏蔽多余位)
    if (num_blocks % (sizeof(BITMAP_TYPE)*8) != 0) {
        int used_bits = num_blocks % (sizeof(BITMAP_TYPE)*8);
        int last_word = (num_blocks - 1) / (sizeof(BITMAP_TYPE)*8);
        pool->free_bitmap[last_word] &= ((BITMAP_TYPE)1 << used_bits) - 1;
        DEBUG_PRINT("Adjusted bitmap[%d] to 0x%lx (used_bits=%d)", 
                last_word, (unsigned long)pool->free_bitmap[last_word], used_bits);
    }
    
    return pool;
}

// 销毁内存池
void mempool_destroy(mempool_t *pool)
{
    MEMPOOL_ASSERT(pool != NULL);

    DEBUG_PRINT("Destroying mempool at %p", pool);
    
    if (pool->memory_area) {
        MEMPOOL_FREE(pool->memory_area);
    }
    MEMPOOL_FREE(pool);
}

// 分配内存块
uint8_t *mempool_alloc(mempool_t *pool, bool for_hw)
{
    MEMPOOL_ASSERT(pool != NULL);

    BITMAP_TYPE word;
    MEMPOOL_IRQ_LOCK_T lock;

    DEBUG_PRINT("Allocating block (for_hw=%d)", for_hw);

    MEMPOOL_IRQ_LOCK(lock);

    for (int i = 0; i < BITMAP_WORDS; i++)
    {
        word = pool->free_bitmap[i];
        if (word == 0)
            continue; // 关键优化：跳过无空闲块的字

        // 直接使用编译器内置指令查找第一个置位比特
        int bit_pos = find_first_set_bit(word);
        if (bit_pos < 0)
            continue; // 理论上不会触发，因word!=0

        // 计算全局块索引
        int block_idx = i * (sizeof(BITMAP_TYPE) * 8) + bit_pos;
        if (block_idx >= pool->block_count)
        {
            continue; // 超出实际块数（由末尾非对齐块处理）
        }

        // 标记块为已分配
        pool->free_bitmap[i] &= ~((BITMAP_TYPE)1 << bit_pos);
        if (for_hw)
        {
            pool->hw_owned_bitmap[i] |= ((BITMAP_TYPE)1 << bit_pos);
        }

        // 返回内存块地址
        uint8_t *block = pool->memory_area + block_idx * pool->block_size;
        MEMPOOL_IRQ_UNLOCK(lock);

        DEBUG_PRINT("Found free block at index %d (word %d, bit %d)", block_idx, i, bit_pos);

        return block;
    }

    MEMPOOL_IRQ_UNLOCK(lock);

    DEBUG_PRINT("No free blocks available");

    return NULL; // 无可用块
}

// 释放内存块
void mempool_free(mempool_t *pool, uint8_t *ptr)
{
    DEBUG_PRINT("Freeing block at %p", ptr);

    if (!pool || !ptr) return;

    if (ptr < pool->memory_area || ptr >= pool->memory_area + pool->block_size * pool->block_count) {
        ERROR_PRINT("Invalid pointer %p (outside pool range)", ptr);
        return;
    }
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    // 计算块索引
    size_t offset = ptr - pool->memory_area;
    size_t block_idx = offset / pool->block_size;
    
    if (block_idx >= pool->block_count) {
        MEMPOOL_IRQ_UNLOCK(lock);
        return; // 非法指针
    }
    
    // 计算位图位置
    int word_idx = block_idx / (sizeof(BITMAP_TYPE)*8);
    int bit_pos = block_idx % (sizeof(BITMAP_TYPE)*8);
    
    // 验证状态
    if ((pool->free_bitmap[word_idx] & ((BITMAP_TYPE)1 << bit_pos)) != 0) {
        MEMPOOL_IRQ_UNLOCK(lock);
        DEBUG_PRINT("Block already free at %p", ptr);
        return; // 已经是空闲状态
    }
    
    // 清除硬件占用标记(如果存在)
    if (pool->hw_owned_bitmap[word_idx] & ((BITMAP_TYPE)1 << bit_pos)) {
        pool->hw_owned_bitmap[word_idx] &= ~((BITMAP_TYPE)1 << bit_pos);
    }
    
    // 标记为空闲
    pool->free_bitmap[word_idx] |= ((BITMAP_TYPE)1 << bit_pos);
    
    MEMPOOL_IRQ_UNLOCK(lock);
}

// 获取可用块数量
size_t mempool_available(mempool_t *pool)
{
    if (!pool) return 0;
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    size_t count = 0;
    for (int i = 0; i < BITMAP_WORDS; i++) {
        DEBUG_PRINT("Free bitmap is 0x%lx", pool->free_bitmap[i]);
        count += POPCOUNT_LL(pool->free_bitmap[i]);
    }
    
    MEMPOOL_IRQ_UNLOCK(lock);
    return count;
}

// 获取已使用块数量
size_t mempool_used(mempool_t *pool)
{
    if (!pool) return 0;
    return pool->block_count - mempool_available(pool);
}

// 获取块索引
static int get_block_index(mempool_t *pool, uint8_t *buffer)
{
    if (!pool || !buffer) return -1;
    
    size_t offset = buffer - pool->memory_area;
    if (offset >= pool->block_size * pool->block_count) {
        return -1;
    }
    
    return (int)(offset / pool->block_size);
}

// 创建队列
mempool_queue_t *mempool_queue_create(mempool_t *pool, size_t capacity)
{
    DEBUG_PRINT("Creating queue for pool %p with capacity %zu", pool, capacity);

    if (!pool || capacity == 0 || capacity > pool->block_count) {
        return NULL;
    }
    
    mempool_queue_t *queue = MEMPOOL_MALLOC(sizeof(mempool_queue_t));
    if (!queue) {
        ERROR_PRINT("Failed to allocate queue structure");
        return NULL;
    }
    
    queue->block_indices = MEMPOOL_MALLOC(sizeof(uint16_t) * capacity);
    if (!queue->block_indices) {
        ERROR_PRINT("Failed to allocate block indices array");
        MEMPOOL_FREE(queue);
        return NULL;
    }
    
    queue->pool = pool;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->next = NULL;
    
    for (int i = 0; i < BITMAP_WORDS; i++) {
        queue->queue_bitmap[i] = 0;
    }
    
    return queue;
}

// 销毁队列
void mempool_queue_destroy(mempool_queue_t *queue)
{
    if (!queue) return;

    DEBUG_PRINT("Destroying queue at %p", queue);
    
    if (queue->block_indices) {
        MEMPOOL_FREE(queue->block_indices);
    }
    MEMPOOL_FREE(queue);
}

// 入队操作
int mempool_queue_enqueue(mempool_queue_t *queue, uint8_t *buffer)
{
    DEBUG_PRINT("Enqueuing buffer %p to queue %p", buffer, queue);

    if (!queue || !buffer || queue->count >= queue->capacity) {
        DEBUG_PRINT("Queue full (count=%zu, capacity=%zu)", queue->count, queue->capacity);
        return -1;
    }
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    int block_idx = get_block_index(queue->pool, buffer);
    if (block_idx < 0 || block_idx >= queue->pool->block_count) {
        MEMPOOL_IRQ_UNLOCK(lock);
        return -1;
    }
    
    int word_idx = block_idx / (sizeof(BITMAP_TYPE)*8);
    int bit_pos = block_idx % (sizeof(BITMAP_TYPE)*8);
    
    if (queue->queue_bitmap[word_idx] & ((BITMAP_TYPE)1 << bit_pos)) {
        MEMPOOL_IRQ_UNLOCK(lock);
        DEBUG_PRINT("Buffer %p already in queue", buffer);
        return -1;
    }
    
    queue->block_indices[queue->tail] = (uint16_t)block_idx;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    queue->queue_bitmap[word_idx] |= ((BITMAP_TYPE)1 << bit_pos);
    
    MEMPOOL_IRQ_UNLOCK(lock);
    return 0;
}

// 出队操作
uint8_t *mempool_queue_dequeue(mempool_queue_t *queue)
{
    DEBUG_PRINT("Dequeuing from queue %p", queue);

    if (!queue || queue->count == 0) {
        return NULL;
    }
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    uint16_t block_idx = queue->block_indices[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    
    int word_idx = block_idx / (sizeof(BITMAP_TYPE)*8);
    int bit_pos = block_idx % (sizeof(BITMAP_TYPE)*8);
    queue->queue_bitmap[word_idx] &= ~((BITMAP_TYPE)1 << bit_pos);
    
    uint8_t *block = queue->pool->memory_area + block_idx * queue->pool->block_size;
    
    MEMPOOL_IRQ_UNLOCK(lock);
    return block;
}

// 查看队首元素
uint8_t *mempool_queue_peek(mempool_queue_t *queue)
{
    DEBUG_PRINT("Peeking queue %p", queue);

    if (!queue || queue->count == 0) {
        return NULL;
    }
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    uint16_t block_idx = queue->block_indices[queue->head];
    uint8_t *block = queue->pool->memory_area + block_idx * queue->pool->block_size;
    
    MEMPOOL_IRQ_UNLOCK(lock);
    return block;
}

// 批量出队
size_t mempool_queue_dequeue_batch(mempool_queue_t *queue, uint8_t **buffers, size_t max_count)
{
    DEBUG_PRINT("Dequeuing batch of %zu from queue %p", max_count, queue);

    if (!queue || !buffers || max_count == 0) {
        return 0;
    }
    
    MEMPOOL_IRQ_LOCK_T lock;
    MEMPOOL_IRQ_LOCK(lock);
    
    size_t actual_count = MEMPOOL_MIN(queue->count, max_count);
    for (size_t i = 0; i < actual_count; i++) {
        uint16_t block_idx = queue->block_indices[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        
        int word_idx = block_idx / (sizeof(BITMAP_TYPE)*8);
        int bit_pos = block_idx % (sizeof(BITMAP_TYPE)*8);
        queue->queue_bitmap[word_idx] &= ~((BITMAP_TYPE)1 << bit_pos);
        
        buffers[i] = queue->pool->memory_area + block_idx * queue->pool->block_size;
    }
    
    queue->count -= actual_count;
    MEMPOOL_IRQ_UNLOCK(lock);
    return actual_count;
}

// 获取队列元素数量
size_t mempool_queue_count(mempool_queue_t *queue)
{
    DEBUG_PRINT("Getting count for queue %p: %zu", queue, queue->count);
    
    if (!queue) return 0;
    return queue->count;
}

// 检查队列是否为空
bool mempool_queue_is_empty(mempool_queue_t *queue)
{
    if (!queue) return true;
    return queue->count == 0;
}

// 检查队列是否已满
bool mempool_queue_is_full(mempool_queue_t *queue)
{
    if (!queue) return true;
    return queue->count >= queue->capacity;
}
