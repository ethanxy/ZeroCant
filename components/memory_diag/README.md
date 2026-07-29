# 内存监控使用指南

## 快速集成

### 1. 在组件中添加依赖
```cmake
# CMakeLists.txt
idf_component_register(
    SRCS "your_component.c"
    INCLUDE_DIRS "."
    REQUIRES memory_diag  # 添加这一行
)
```

### 2. 在代码中使用
```c
#include "memory_diag.h"

// 显示详细内存状态
memory_diag_print_status("your_tag");

// 智能分配内存
void* buffer = memory_diag_smart_malloc(size, true, true); // 优先PSRAM，允许降级
if (buffer) {
    ESP_LOGI("TAG", "Allocated: addr=%p type=%s", buffer, memory_diag_get_type_str(buffer));
}

// 检查是否有足够内存
if (memory_diag_check_available(large_size, true)) {
    // 可以安全分配
}
```

## API 说明

### memory_diag_print_status(tag)
显示完整的内存使用情况，包括：
- 总堆内存使用率
- PSRAM 使用率  
- DMA 内存使用率
- 内部内存使用率

### memory_diag_smart_malloc(size, prefer_psram, allow_fallback)
智能内存分配：
- `prefer_psram`: true优先PSRAM，false优先DMA
- `allow_fallback`: true允许降级到其他内存类型
- 返回分配的指针或NULL

### memory_diag_check_available(size, prefer_psram)
检查是否有足够内存（预留1KB安全边界）

### memory_diag_get_type_str(ptr)
返回内存类型字符串："PSRAM"/"DMA"/"Internal"/"Unknown"

## 最佳实践

### 1. 启动时监控
```c
void app_main(void) {
    memory_diag_print_status("startup");
    // ... 初始化代码
    memory_diag_print_status("after_init");
}
```

### 2. 大内存分配
```c
// 分配大缓存时
void* large_buffer = memory_diag_smart_malloc(large_size, true, true);
if (!large_buffer) {
    // 降级方案
    ESP_LOGW(TAG, "Large buffer failed, using alternative approach");
}
```

### 3. 定期监控
```c
// 在主循环或定时器中
static int monitor_count = 0;
if (++monitor_count % 100 == 0) { // 每100次循环监控一次
    memory_diag_print_status("runtime");
}
```

## 输出示例
```
I (1234) your_tag: === Memory Status ===
I (1234) your_tag: Heap     :   6234 /   7789 KB (80.0% free)
I (1234) your_tag: PSRAM    :   7234 /   7744 KB (93.4% free)  
I (1234) your_tag: DMA      :    123 /    353 KB (34.8% free)
I (1234) your_tag: Internal :    234 /    353 KB (66.3% free)
I (1234) your_tag: ==================
```
