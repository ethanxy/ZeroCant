# 重启历史记录控制台命令

## 通过串口查看重启历史

您可以通过以下方式查看重启历史：

### 1. 启动时自动显示
每次设备启动时，会自动显示最近的重启历史记录。

### 2. 手动查看 (通过代码)
在任何地方调用以下函数：
```c
#include "restart_history.h"

// 显示所有重启历史
restart_history_print_all();

// 获取重启次数
uint32_t count = restart_history_get_count();
ESP_LOGI(TAG, "Total restarts: %d", count);
```

### 3. 添加控制台命令 (可选)
如果需要交互式查看，可以添加控制台命令支持。

## 重启历史数据存储

### 存储位置
- **NVS分区**: 数据保存在非易失性存储中
- **持久化**: 断电后数据不会丢失
- **循环缓冲**: 最多保存10条记录，超出后覆盖最旧的记录

### 存储内容
每条记录包含：
- 重启原因
- 时间戳 (相对启动时间)
- 可用堆内存大小
- 历史最小堆内存大小
- 重启原因描述

### 数据示例
```
=================== RESTART HISTORY ===================
Total restarts: 5
-------------------------------------------------------
#5   | USB reset            | Free:   7890 KB | Min:   7654 KB | Time: +234 ms
#4   | Power-on reset       | Free:   7891 KB | Min:   7655 KB | Time: +189 ms
#3   | Task watchdog        | Free:   6234 KB | Min:   5432 KB | Time: +45678 ms
#2   | Software reset       | Free:   7800 KB | Min:   7600 KB | Time: +156 ms
#1   | Power-on reset       | Free:   7892 KB | Min:   7656 KB | Time: +178 ms
=======================================================
```

## 管理重启历史

### 清除历史记录
```c
#include "restart_history.h"

// 清除所有重启历史记录
esp_err_t err = restart_history_clear();
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Restart history cleared successfully");
}
```

### 查看统计信息
```c
uint32_t total_restarts = restart_history_get_count();
ESP_LOGI(TAG, "Device has restarted %d times", total_restarts);
```
