# ESP32 重启原因分析指南

## 重启类型和原因

### 1. 正常重启原因
- **POWERON_RESET**: 上电复位
- **SW_RESET**: 软件复位 (调用 `esp_restart()`)
- **DEEPSLEEP_RESET**: 深度睡眠唤醒

### 2. 异常重启原因
- **PANIC_RESET**: 内核恐慌 (代码错误、内存访问违规等)
- **WDT_RESET**: 看门狗超时重启
- **BROWNOUT_RESET**: 电压不足重启
- **RTCWDT_RESET**: RTC看门狗超时
- **TG0WDT_SYS_RESET**: 任务看门狗超时
- **TG1WDT_SYS_RESET**: 中断看门狗超时

## 如何查看重启原因

### 1. 通过串口监视器查看
```bash
# 启动监视器
source /Users/xuanyuliu/esp/v5.4.2/esp-idf/export.sh
idf.py monitor --port /dev/cu.usbserial-0001
```

### 2. 启动日志中的重启信息
设备重启后，在串口输出的开始部分会显示：
```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
```

### 3. 代码中获取重启原因
```c
#include "esp_system.h"

void check_reset_reason() {
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "Reset reason: Power-on reset");
            break;
        case ESP_RST_EXT:
            ESP_LOGI(TAG, "Reset reason: External pin reset");
            break;
        case ESP_RST_SW:
            ESP_LOGI(TAG, "Reset reason: Software reset");
            break;
        case ESP_RST_PANIC:
            ESP_LOGI(TAG, "Reset reason: Exception/panic");
            break;
        case ESP_RST_INT_WDT:
            ESP_LOGI(TAG, "Reset reason: Interrupt watchdog");
            break;
        case ESP_RST_TASK_WDT:
            ESP_LOGI(TAG, "Reset reason: Task watchdog");
            break;
        case ESP_RST_WDT:
            ESP_LOGI(TAG, "Reset reason: Other watchdog");
            break;
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "Reset reason: Deep sleep reset");
            break;
        case ESP_RST_BROWNOUT:
            ESP_LOGI(TAG, "Reset reason: Brownout reset");
            break;
        case ESP_RST_SDIO:
            ESP_LOGI(TAG, "Reset reason: SDIO reset");
            break;
        default:
            ESP_LOGI(TAG, "Reset reason: Unknown (%d)", reason);
            break;
    }
}
```

## 常见重启问题及解决方案

### 1. 看门狗超时 (WDT_RESET)
**原因**: 主任务被阻塞太久，无法及时喂狗
**解决方案**:
- 检查是否有长时间运行的循环
- 在耗时操作中添加 `vTaskDelay()` 或 `taskYIELD()`
- 增加看门狗超时时间 (在 `sdkconfig` 中配置)

### 2. 内存不足导致的崩溃
**原因**: 堆内存耗尽或栈溢出
**解决方案**:
- 检查内存使用情况
- 减少大数组或缓冲区的使用
- 优化内存分配

### 3. 电源问题 (BROWNOUT_RESET)
**原因**: 供电电压不稳定
**解决方案**:
- 检查电源供应
- 添加电源滤波电容
- 降低系统功耗

### 4. 代码错误导致的 PANIC
**原因**: 空指针访问、数组越界等
**解决方案**:
- 启用核心转储分析
- 检查指针有效性
- 添加边界检查

## 启用详细调试信息

### 1. 在 sdkconfig 中启用选项:
```
CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=n
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y
```

### 2. 启用堆栈跟踪:
```
CONFIG_COMPILER_STACK_CHECK_MODE_STRONG=y
CONFIG_ESP_DEBUG_OCDAWARE=y
```

## 实时监控命令

### 1. 查看内存使用情况
```c
ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free heap: %d bytes", esp_get_minimum_free_heap_size());
```

### 2. 查看任务状态
```c
#include "freertos/task.h"

void print_task_info() {
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    TaskStatus_t *pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, NULL);
        
        for (UBaseType_t x = 0; x < uxArraySize; x++) {
            ESP_LOGI(TAG, "Task: %s, State: %d, Priority: %d, Stack HWM: %d",
                     pxTaskStatusArray[x].pcTaskName,
                     pxTaskStatusArray[x].eCurrentState,
                     pxTaskStatusArray[x].uxCurrentPriority,
                     pxTaskStatusArray[x].usStackHighWaterMark);
        }
        
        vPortFree(pxTaskStatusArray);
    }
}
```

## 分析核心转储

如果启用了核心转储，可以分析崩溃详情：
```bash
# 读取核心转储
idf.py coredump-info

# 分析核心转储
idf.py coredump-debug
```

## 针对当前项目的检查点

基于您的项目，重点检查以下方面：

### 1. LVGL 相关
- 检查 LVGL 任务是否正常运行
- 确认画布内存分配是否充足
- 验证绘制操作是否在正确的线程中执行

### 2. AMOLED 显示相关
- 检查 SPI 通信是否稳定
- 验证显示缓冲区大小是否合适
- 确认像素移动操作是否导致内存访问错误

### 3. IMU 传感器相关
- 检查 I2C 通信是否稳定
- 验证数据处理是否有数组越界
- 确认传感器中断处理是否正确

### 4. 内存使用
- 监控堆内存使用情况
- 检查是否有内存泄漏
- 验证大缓冲区分配是否成功
