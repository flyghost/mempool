#ifndef MEMPOOL_LOG_H
#define MEMPOOL_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifndef MEMPOOL_LOG_LEVEL
#define MEMPOOL_LOG_LEVEL LOG_LEVEL_ERROR  // 默认日志级别
#endif

// 日志级别定义
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
static inline const char* get_timestamp() {
    static __thread char buffer[32];
    time_t now = time(NULL);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buffer;
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

// 各级别日志宏
#if MEMPOOL_LOG_LEVEL <= LOG_LEVEL_DEBUG
#define DEBUG_PRINT(fmt, ...) mempool_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) ((void)0)
#endif

#if MEMPOOL_LOG_LEVEL <= LOG_LEVEL_INFO
#define INFO_PRINT(fmt, ...)  mempool_log(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define INFO_PRINT(fmt, ...)  ((void)0)
#endif

#if MEMPOOL_LOG_LEVEL <= LOG_LEVEL_WARNING
#define WARNING_PRINT(fmt, ...) mempool_log(LOG_LEVEL_WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define WARNING_PRINT(fmt, ...) ((void)0)
#endif

// ERROR总是打印
#define ERROR_PRINT(fmt, ...) mempool_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // MEMPOOL_LOG_H