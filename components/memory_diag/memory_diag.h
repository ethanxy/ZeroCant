#ifndef MEMORY_DIAG_H
#define MEMORY_DIAG_H

#include "esp_heap_caps.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打印详细的内存使用情况
 * @param tag 日志标签
 */
void memory_diag_print_status(const char* tag);

/**
 * @brief 检查是否有足够内存分配指定大小的缓存
 * @param size 需要分配的字节数
 * @param prefer_psram 是否优先使用PSRAM
 * @return true 如果有足够内存，false 否则
 */
bool memory_diag_check_available(size_t size, bool prefer_psram);

/**
 * @brief 智能分配内存，自动选择最佳内存类型
 * @param size 需要分配的字节数
 * @param prefer_psram 是否优先使用PSRAM
 * @param allow_fallback 是否允许降级到其他内存类型
 * @return 分配的内存指针，失败返回NULL
 */
void* memory_diag_smart_malloc(size_t size, bool prefer_psram, bool allow_fallback);

/**
 * @brief 获取内存类型字符串描述
 * @param ptr 内存指针
 * @return 内存类型描述字符串
 */
const char* memory_diag_get_type_str(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_DIAG_H
