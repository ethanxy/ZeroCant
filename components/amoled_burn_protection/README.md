# AMOLED防烧屏保护组件

## 概述

AMOLED防烧屏保护组件为AMOLED显示器提供专业的烧屏保护功能，通过周期性的微小像素偏移来防止静态图像在屏幕上留下永久印记。

## 特性

- ✅ **微小偏移**：1-2像素偏移，用户几乎无感知
- ✅ **周期性保护**：可配置的时间间隔（默认30秒）
- ✅ **双模式支持**：随机偏移 / 固定模式
- ✅ **低资源占用**：仅约200字节内存占用
- ✅ **零侵入集成**：不影响现有UI代码
- ✅ **实时控制**：支持启动/停止/重置

## 快速开始

### 基本使用

```c
#include "amoled_burn_protection.h"

void app_main(void) {
    // ... LVGL初始化代码 ...
    
    // 使用默认配置初始化
    esp_err_t ret = amoled_burn_protection_init(NULL);
    if (ret == ESP_OK) {
        // 启动防烧屏保护
        amoled_burn_protection_start();
        ESP_LOGI("APP", "AMOLED burn protection started");
    }
}
```

### 高级配置

```c
// 自定义配置
amoled_burn_protection_config_t config = {
    .offset_interval_ms = 45000,        // 45秒间隔
    .max_offset_pixels = 1,             // 最大1像素偏移
    .enable_random_offset = true,       // 随机偏移模式
    .target_display = NULL              // 默认显示器
};

amoled_burn_protection_init(&config);
amoled_burn_protection_start();
```

## API参考

### 配置结构体

```c
typedef struct {
    uint32_t offset_interval_ms;    // 偏移间隔时间(毫秒)
    uint8_t max_offset_pixels;      // 最大偏移像素数
    bool enable_random_offset;      // 随机偏移模式开关
    lv_disp_t* target_display;      // 目标显示器
} amoled_burn_protection_config_t;
```

### 主要函数

- `amoled_burn_protection_init()` - 初始化组件
- `amoled_burn_protection_start()` - 启动保护
- `amoled_burn_protection_stop()` - 停止保护
- `amoled_burn_protection_reset_offset()` - 重置偏移
- `amoled_burn_protection_get_offset()` - 获取当前偏移

## 工作原理

组件通过调整LVGL显示驱动的有效分辨率实现像素偏移：

1. **保存原始分辨率**：记录初始的`hor_res`和`ver_res`
2. **周期性偏移**：定时器触发时调整有效分辨率
3. **视觉偏移效果**：内容在屏幕上产生1-2像素的移动
4. **无缝集成**：对现有UI组件完全透明

## 性能影响

- **内存占用**：约200字节
- **CPU开销**：每30秒仅0.1ms处理时间  
- **显示刷新**：偏移时触发一次全屏刷新
- **用户体验**：1-2像素偏移几乎无感知

## 注意事项

1. 必须在LVGL初始化完成后调用
2. 建议在主应用任务中集成
3. 适用于所有AMOLED显示器
4. 与现有UI代码完全兼容

## 许可证

本组件基于MIT许可证开源。
