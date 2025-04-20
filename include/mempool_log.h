#ifndef MEMPOOL_LOG_H
#define MEMPOOL_LOG_H

#include "mempool_port.h"

#ifndef MEMPOOL_LOG_LEVEL
#define MEMPOOL_LOG_LEVEL LOG_LEVEL_DEBUG  // 默认日志级别
#endif

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

#define MEMPOOL_HEX_DUMP(data, length)  hex_dump((const unsigned char *)data, (size_t)length)

// ERROR总是打印
#define ERROR_PRINT(fmt, ...) mempool_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif // MEMPOOL_LOG_H