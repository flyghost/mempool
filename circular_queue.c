#include "circular_queue.h"
#include <stdbool.h>
#include <string.h>

// 静态断言确保队列大小为2的幂次
#define CQ_STATIC_ASSERT(pow2_size) \
    typedef char static_assert_[(pow2_size & (pow2_size - 1)) == 0 ? 1 : -1]

// 内部队列结构体
struct cqueue_s {
    void ​**buffers;
    size_t *lengths;
    volatile uint8_t head;
    volatile uint8_t tail;
    uint8_t size_mask;  // 替代size，直接存储掩码
#if CQ_BACKPRESSURE_EN
    volatile bool backpressure;
    uint8_t watermark_high;
    uint8_t watermark_low;
#endif
};

// 初始化时静态断言检查
CQ_STATIC_ASSERT(CQ_DEFAULT_SIZE);

// 创建队列（静态内存版本）
cqueue_t* cq_create_static(void *buf_mem, size_t *len_mem, uint8_t size) {
    struct cqueue_s *q = (struct cqueue_s*)buf_mem;
    q->buffers = (void**)((uint8_t*)buf_mem + sizeof(struct cqueue_s));
    q->lengths = (size_t*)((uint8_t*)q->buffers + size * sizeof(void*));
    q->head = q->tail = 0;
    q->size_mask = size - 1;
#if CQ_BACKPRESSURE_EN
    q->backpressure = false;
    q->watermark_high = size * 3 / 4;
    q->watermark_low = size / 4;
#endif
    return (cqueue_t*)q;
}

// 入队操作（带背压检测）
int cq_enqueue(cqueue_t *handle, void *data, size_t len) {
    struct cqueue_s *q = (struct cqueue_s*)handle;
    uint8_t next_tail = (q->tail + 1) & q->size_mask;

    if (next_tail == q->head) {
#if CQ_BACKPRESSURE_EN
        q->backpressure = true;
#endif
        return -1; // 队列满
    }

    q->buffers[q->tail] = data;
    q->lengths[q->tail] = len;
    q->tail = next_tail;

#if CQ_BACKPRESSURE_EN
    // 更新背压状态
    uint8_t used = (q->tail - q->head) & q->size_mask;
    if (used >= q->watermark_high) {
        q->backpressure = true;
    }
#endif
    return 0;
}

// 出队操作（非阻塞）
int cq_dequeue(cqueue_t *handle, void ​**data, size_t *len) {
    struct cqueue_s *q = (struct cqueue_s*)handle;
    
    if (q->head == q->tail) {
        return -1; // 队列空
    }

    *data = q->buffers[q->head];
    *len = q->lengths[q->head];
    q->head = (q->head + 1) & q->size_mask;

#if CQ_BACKPRESSURE_EN
    // 检查背压解除
    uint8_t used = (q->tail - q->head) & q->size_mask;
    if (q->backpressure && used <= q->watermark_low) {
        q->backpressure = false;
    }
#endif
    return 0;
}

#if CQ_BACKPRESSURE_EN
bool cq_get_backpressure(cqueue_t *handle) {
    return ((struct cqueue_s*)handle)->backpressure;
}
#endif