#include "mempool.h"

//===================================================================
// 协议栈相关接口 - 具体实现依赖于协议栈
// 这些接口是协议栈的抽象，具体实现依赖于协议栈
//===================================================================
static void net_input(uint8_t *buf, size_t len)
{
    // 假设这个接口是协议栈的抽象，处理接收到的网络数据包
    // 具体实现依赖于协议栈
}

static void net_output(uint8_t *buf, size_t len)
{
    // 假设这个接口是协议栈的抽象，处理发送的数据包
    // 具体实现依赖于协议栈
    eth_send_frame(buf, len); // 调用发送接口
}

static void net_tx_done(uint8_t *buf)
{
    // 假设这个接口是协议栈的抽象，释放网络数据包
    // 具体实现依赖于协议栈
}

static void net_rx_done(uint8_t *buf)
{
    // 假设这个接口是协议栈的抽象，释放网络数据包
    // 具体实现依赖于协议栈
}

//===================================================================
//硬件相关接口 - 具体实现依赖于硬件平台和驱动程序
//===================================================================
static int eth_hw_rx(uint8_t *buf)
{
    // 假设这个接口是硬件相关的，添加缓冲区到DMA链表
    // 具体实现依赖于硬件平台和驱动程序
}

// 硬件发送数据接口 - 具体实现依赖于硬件平台和驱动程序
static int eth_hw_tx(uint8_t *buf, size_t len)
{
    // 假设这个接口是硬件相关的，启动发送数据
    // 具体实现依赖于硬件平台和驱动程序
    return 0; // 示例返回0，表示成功
}

//===================================================================
//                  通用实现部分（提供给硬件控制器调用）
// TX如果是零拷贝，需要在发送完成后调用协议栈接口来释放内存
// RX如果是零拷贝，需要提供一个释放接口，供协议栈调用
//===================================================================
#define MEMPOOL_TX_ZEROCOPY_EN      1       // 启用零拷贝模式(0/1)

#define MEMPOOL_RX_BLOCK_SIZE 1536 // RX内存池块大小
#define MEMPOOL_TX_BLOCK_SIZE 1536 // TX内存池块大小
#define MEMPOOL_RX_BLOCK_COUNT 32   // RX内存池块数量
#define MEMPOOL_TX_BLOCK_COUNT 32   // TX内存池块数量

// 全局内存池实例
static mempool_t *eth_rx_pool = NULL;
static mempool_t *eth_tx_pool = NULL;

#define ETH_HW_RX_NUMBER 12 // RX DMA链表数量

// 初始化控制器和DMA链表
int eth_hw_init(void)
{
    // 创建接收内存池(1536字节/块，32块)
    eth_rx_pool = mempool_create(MEMPOOL_RX_BLOCK_SIZE, MEMPOOL_RX_BLOCK_COUNT);
    if (eth_rx_pool == NULL) {
        return -1;
    }

    // 创建发送内存池(1536字节/块，16块)
#if MEMPOOL_TX_ZEROCOPY_EN
    eth_tx_pool = NULL; // 零拷贝模式不需要内存池
#else
    eth_tx_pool = mempool_create(MEMPOOL_TX_BLOCK_SIZE, MEMPOOL_TX_BLOCK_COUNT); // 非零拷贝模式使用32块
    if (eth_tx_pool == NULL) {
        mempool_destroy(eth_rx_pool);
        return -1;
    }
#endif

    // 初始化硬件和DMA链表
    // 这里假设硬件初始化成功，实际实现依赖于硬件平台和驱动程序

    // 将RX的buffer配置给控制器的DMA链表
    for(int i = 0; i < ETH_HW_RX_NUMBER; i++) {
        uint8_t *buf = mempool_alloc(eth_rx_pool, true); // true表示硬件持有
        if (!buf) {
            eth_rx_pool->block_count = i; // 更新实际块数
            break;
        }
        net_input(buf);
    }
    
    return 0;
}

// 接收完成中断处理
void eth_hw_rx_isr(uint8_t *buffer)
{
    if(buffer == NULL) {
        return;
    }
#if MEMPOOL_TX_ZEROCOPY_EN
    // 零拷贝模式 - 直接将内存池分配的内存传给上层，上层使用完后需要释放
    net_input(buffer, MEMPOOL_RX_BLOCK_SIZE); // 处理接收到的数据包
#else
    // 非零拷贝模式 - 从内存池分配
    uint8_t *buf = mempool_alloc(eth_rx_pool, true); // true表示硬件持有
    if (buf) {
        net_input(buf);
    }
#endif
}

// 发送完成中断处理
void eth_hw_tx_isr(uint8_t *data)
{
#if MEMPOOL_TX_ZEROCOPY_EN
    net_tx_done(NULL); // 零拷贝模式 - 不需要处理
#else
    // 非零拷贝模式需要释放缓冲区
    mempool_free(eth_tx_pool, buf);
#endif
}

// 发送数据
int eth_send_frame(uint8_t *data, size_t len)
{
    MEMPOOL_ASSERT(data != NULL);
    MEMPOOL_ASSERT(len > 0 && len <= MEMPOOL_TX_BLOCK_SIZE); // 数据长度检查

    uint8_t *buf = NULL;

#if MEMPOOL_TX_ZEROCOPY_EN
    // 零拷贝直接发送
    buf = data; // 直接使用传入的缓冲区
#else
    // 从内存池分配并拷贝
    uint8_t *buf = mempool_alloc(eth_tx_pool, false);
    if (!buf) return -1;
    
    memcpy(buf, data, len);
#endif

    if (eth_hw_tx(buf, len) != 0) {
    #if MEMPOOL_TX_ZEROCOPY_EN == 0
        mempool_free(eth_tx_pool, buf);
    #endif
        return -1;
    }

    return 0;
}

// 上层使用完后，释放缓冲区
void eth_rx_done(uint8_t *buf)
{
#if MEMPOOL_TX_ZEROCOPY_EN
    // 零拷贝模式 - 使用的是内存池的内存，需要释放
    mempool_free(eth_rx_pool, buf);
#else
    // 非零拷贝模式 - 不需要处理
#endif
}

// 发送完成回调
static void eth_tx_done(uint8_t *buf)
{
#if !MEMPOOL_TX_ZEROCOPY_EN
    // 非零拷贝模式需要释放缓冲区
    mempool_free(eth_tx_pool, buf);
#endif
}