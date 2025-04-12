#ifndef CIRCULAR_QUEUE_H
#define CIRCULAR_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

// 配置开关（通过编译命令定义）
#ifndef CQ_DEFAULT_SIZE
#define CQ_DEFAULT_SIZE 8 // 必须是2的幂次
#endif

#ifndef CQ_BACKPRESSURE_EN
#define CQ_BACKPRESSURE_EN 1 // 默认启用背压
#endif

// 队列句柄（不透明指针）
typedef struct cqueue_s cqueue_t;

// 创建静态内存队列
cqueue_t* cq_create_static(void *buf_mem, size_t *len_mem, uint8_t size);

// 基础操作
int cq_enqueue(cqueue_t *handle, void *data, size_t len);
int cq_dequeue(cqueue_t *handle, void ​**data, size_t *len);
bool cq_is_empty(cqueue_t *handle);

// 背压状态接口
#if CQ_BACKPRESSURE_EN
bool cq_get_backpressure(cqueue_t *handle);
#endif

// 内存计算宏
#define CQ_NEEDED_MEM_SIZE(queue_size) \
    (sizeof(struct cqueue_s) + \
     queue_size * (sizeof(void*) + sizeof(size_t)))

#endif // CIRCULAR_QUEUE_H