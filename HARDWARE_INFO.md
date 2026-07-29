# ESP32-S3 硬件信息记录

## 项目概述
- **项目名称**: ESP-IDF + LVGL 飞行姿态显示器
- **主控芯片**: ESP32-S3
- **显示屏**: SH8601 280x456 AMOLED
- **传感器**: QMI8658C 6轴 IMU
- **开发框架**: ESP-IDF v5.4.2 + LVGL
- **记录日期**: 2025-07-11

## 内存配置详情

### 内部 RAM 分布
```
内存段                    大小        用途
3FCA00E8 ~ 3FCA4A10      293 KiB     主 RAM
3FCE9710 ~ 3FCEE034      21 KiB      辅助 RAM  
3FCF0000 ~ 3FCF8000      32 KiB      DRAM
600FE01C ~ 600FFFE8      7 KiB       RTC RAM
总计                     353 KiB     内部 RAM
```

### 外部 PSRAM 配置
```
PSRAM 芯片规格:
- 厂商: AP (vendor id: 0x0d)
- 型号: generation 3 (dev id: 0x02)  
- 容量: 64 Mbit = 8 MB (density: 0x03)
- 接口: Octal SPI (8线)
- 速度: 80MHz
- 延迟: 10 cycles@Fixed

可用 PSRAM:
- 主池: 7744 KB
- 对齐间隙: 45 KB  
- 总计: 7789 KB ≈ 7.6 MB
```

### ESP-IDF PSRAM 配置
```kconfig
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y                    # 8线模式
CONFIG_SPIRAM_SPEED_80M=y                   # 80MHz时钟
CONFIG_SPIRAM_USE_MALLOC=y                  # 支持malloc分配
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=10000   # <10KB优先内部内存
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768 # 保留32KB内部内存
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y          # 指令可在PSRAM执行
CONFIG_SPIRAM_RODATA=y                      # 只读数据可放PSRAM
```

## 显示系统配置

### LCD 规格
- **分辨率**: 280 x 456 像素
- **色深**: 16位 (CONFIG_LV_COLOR_DEPTH=16)
- **接口**: SPI QSPI (4线)
- **驱动**: SH8601
- **显示模式**: RGB565

### LVGL 内存分配策略
```c
// Draw Buffer 配置 (必须使用DMA内存)
EXAMPLE_LVGL_BUF_HEIGHT = 456/4 = 114 像素
buf_size = 280 × 114 × 2 = 63,840 字节
总draw buffer = 2 × 63,840 = 127,680 字节 (124.7 KB)

// Canvas Buffer 配置 (优先PSRAM，降级DMA)
canvas_size = 280 × 456 × 2 = 255,360 字节 (249.4 KB)
ruler_cache = 280 × 456 × 2 = 255,360 字节 (249.4 KB)
总canvas内存 = 510,720 字节 (498.8 KB)
```

## 内存分配最佳实践

### 1. 内存类型选择
```c
// DMA内存 (必需，但容量有限 ~353KB)
lv_color_t *lvgl_buf = heap_caps_malloc(size, MALLOC_CAP_DMA);

// PSRAM内存 (大容量 ~7.6MB，优先用于大缓存)  
lv_color_t *canvas_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// 降级策略
if (!psram_ptr) {
    psram_ptr = heap_caps_malloc(size, MALLOC_CAP_DMA);
    if (!psram_ptr) {
        psram_ptr = heap_caps_malloc(size/2, MALLOC_CAP_DMA); // 减半重试
    }
}
```

### 2. 关键分配规则
- **LVGL draw buffer**: 必须DMA内存，用于LCD驱动
- **大画布缓存**: 优先PSRAM，降级DMA，最后减半
- **小对象(<10KB)**: 自动使用内部内存
- **标尺缓存**: 可选，PSRAM优先，失败时每帧重绘

### 3. 内存不足处理
```c
// 成功分配: 性能最佳，有缓存
if (ruler_cached && ruler_bg_buf) {
    memcpy(cbuf, ruler_bg_buf, buf_size); // 快速恢复
}
// 缓存失败: 功能正常，性能稍差  
else {
    draw_pitch_ruler_on_canvas(); // 每帧重绘
}
```

## 性能基准

### 刷新频率
- **目标**: 20Hz (50ms间隔)
- **任务优先级**: 3 (中等)
- **看门狗**: 启用，防止阻塞

### 内存使用基准
```
配置                     内存使用        性能
LVGL draw buffer        127.7 KB        必需 (DMA)
Canvas + Cache          498.8 KB        最佳 (PSRAM)  
Canvas only             249.4 KB        良好 (PSRAM/DMA)
Canvas half-size        124.7 KB        可接受 (DMA)
```

## 开发注意事项

### 1. 内存分配顺序
1. 先分配必需的DMA内存 (LVGL buffers)
2. 再分配可选的PSRAM内存 (大缓存)
3. 最后分配小对象 (自动选择)

### 2. 错误处理
- 所有大内存分配都要检查返回值
- 提供降级方案，避免系统崩溃
- 记录内存分配状态，便于调试

### 3. 调试工具
```c
// 内存使用监控
size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);  
size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
ESP_LOGI(TAG, "Memory: Heap=%u PSRAM=%u DMA=%u", free_heap, free_psram, free_dma);
```

### 4. 关键教训
- **PSRAM容量充足**: 8MB完全够用，关键是正确使用
- **内部RAM宝贵**: 仅353KB，需谨慎分配给DMA用途
- **降级机制重要**: 确保在各种内存条件下都能工作
- **性能vs内存权衡**: 缓存提升性能，但要处理分配失败

## 硬件引脚配置

### LCD SPI引脚
```c
#define EXAMPLE_PIN_NUM_LCD_CS     GPIO_NUM_9
#define EXAMPLE_PIN_NUM_LCD_PCLK   GPIO_NUM_10
#define EXAMPLE_PIN_NUM_LCD_DATA0  GPIO_NUM_11  
#define EXAMPLE_PIN_NUM_LCD_DATA1  GPIO_NUM_12
#define EXAMPLE_PIN_NUM_LCD_DATA2  GPIO_NUM_13
#define EXAMPLE_PIN_NUM_LCD_DATA3  GPIO_NUM_14
#define EXAMPLE_PIN_NUM_LCD_RST    GPIO_NUM_21
```

### PSRAM引脚
```c
CONFIG_SPIRAM_CLK_IO=30  # PSRAM时钟
CONFIG_SPIRAM_CS_IO=26   # PSRAM片选
```

### I2C引脚 (QMI8658C)
- 配置在 i2c_bsp 组件中
- 触摸屏也使用I2C接口

---
**更新记录**:
- 2025-07-11: 初始创建，记录内存优化后的配置
- 解决了ruler_bg_buf分配失败问题
- 确认PSRAM分配策略有效
