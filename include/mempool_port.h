#ifndef MEMPOOL_PORT_H
#define MEMPOOL_PORT_H


#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// 平台无关的宏定义
#include <sched.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>

//===================================================================
// 树莓派实现
//===================================================================
// core需要的适配
typedef pthread_mutex_t                     MEMPOOL_LOCK_TYPE;
#define MEMPOOL_LOCK_INIT(lock)             pthread_mutex_init((lock), NULL)
#define MEMPOOL_LOCK(lock)                  pthread_mutex_lock((lock))
#define MEMPOOL_UNLOCK(lock)                pthread_mutex_unlock((lock))
#define MEMPOOL_MALLOC(size)                malloc(size)
#define MEMPOOL_FREE(ptr)                   free(ptr)
#define MEMPOOL_MEMALIGN(alignment, size)   memalign(alignment, size)
// test需要的适配
#define MEMPOOL_DELAY_MS(ms)                     \
    do                                           \
    {                                            \
        struct timespec ts = {                   \
            .tv_sec = (ms) / 1000,               \
            .tv_nsec = ((ms) % 1000) * 1000000}; \
        nanosleep(&ts, NULL);                    \
    } while (0)

#define MEMPOOL_CURRENT_TIME_MS()                    \
    ({                                               \
        struct timespec ts;                          \
        clock_gettime(CLOCK_MONOTONIC, &ts);         \
        (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000); \
    })

#define MEMPOOL_ASSERT(expr)                                                     \
    do                                                                           \
    {                                                                            \
        if (!(expr))                                                             \
        {                                                                        \
            fprintf(stderr, "\033[1;31mAssertion failed\033[0m: %s (%s:%d)\r\n", \
                    #expr, __FILE__, __LINE__);                                  \
            abort();                                                             \
        }                                                                        \
    } while (0)

#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_ERROR     3

// ANSI颜色定义（整行着色）
#define COLOR_DEBUG   "\033[0;36m"  // 青色
#define COLOR_INFO    "\033[0;32m"  // 绿色
#define COLOR_WARNING "\033[0;33m"  // 黄色
#define COLOR_ERROR   "\033[1;31m"  // 红色（加粗）
#define COLOR_RESET   "\033[0m"     // 重置

// 获取当前时间字符串（线程安全版本）
static inline const char* get_timestamp2() {
    static __thread char buffer[32];
    time_t now = time(NULL);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buffer;
}


#include <ctype.h>
static void hex_dump(const unsigned char *data, size_t length) {
    const int BYTES_PER_LINE = 16;
    
    for (size_t offset = 0; offset < length; offset += BYTES_PER_LINE) {
        // 打印偏移量
        printf("%08zx  ", offset);
        
        // 打印十六进制数据
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            if (offset + i < length) {
                printf("%02x ", data[offset + i]);
            } else {
                printf("   "); // 对齐填充
            }
            
            // 在8字节后添加额外空格
            if (i == 7) {
                printf(" ");
            }
        }
        
        printf(" ");
        
        // 打印ASCII字符
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            if (offset + i < length) {
                unsigned char c = data[offset + i];
                printf("%c", isprint(c) ? c : '.');
            } else {
                printf(" "); // 对齐填充
            }
        }
        
        printf("\n");
    }
}

// 基础日志函数（整行着色）
static inline void mempool_log(uint8_t level, const char* file, int line, const char* fmt, ...) {
    const char* color = COLOR_RESET;
    const char* prefix = "";
    
    switch(level) {
        case LOG_LEVEL_DEBUG:   color = COLOR_DEBUG;   prefix = "[DEBUG]  "; break;
        case LOG_LEVEL_INFO:    color = COLOR_INFO;    prefix = "[INFO]   "; break;
        case LOG_LEVEL_WARNING: color = COLOR_WARNING; prefix = "[WARNING]"; break;
        case LOG_LEVEL_ERROR:   color = COLOR_ERROR;   prefix = "[ERROR]  "; break;
    }
    
    // 打印带颜色的日志头
    // fprintf(stderr, "%s%s %s:%d ", color, prefix, file, line);
    fprintf(stderr, "%s%s  ", color, prefix);
    
    // 打印带颜色的日志内容
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    // 重置颜色并换行
    fprintf(stderr, "%s\n", COLOR_RESET);
}




#endif