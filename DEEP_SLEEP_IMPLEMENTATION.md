# 深度休眠功能实现

## 🌙 功能描述
- **触发区域**: 屏幕右上角 140×140 像素区域
- **触发方式**: 长按3秒进入深度休眠
- **唤醒方式**: 设备重启（后续可配置触摸唤醒或按钮唤醒）
- **状态保存**: 进入休眠前保存当前状态

## 📍 技术实现

### 区域定义
```c
// 右上角深度休眠区域定义 (140×140px)
#define SLEEP_AREA_SIZE 140
#define SLEEP_AREA_X1 (EXAMPLE_LCD_H_RES - SLEEP_AREA_SIZE)  // 280 - 140 = 140
#define SLEEP_AREA_Y1 0
#define SLEEP_AREA_X2 (EXAMPLE_LCD_H_RES - 1)  // 279
#define SLEEP_AREA_Y2 (SLEEP_AREA_SIZE - 1)    // 139
```

### 长按检测参数
```c
#define LONG_PRESS_DURATION_MS 3000  // 3秒长按
#define TOUCH_POLL_INTERVAL_MS 100   // 100ms轮询间隔
```

## 🔧 核心功能

### 1. 区域检测
- `is_touch_in_sleep_area()`: 检测触摸是否在右上角休眠区域
- 坐标范围: X(140-279), Y(0-139)

### 2. 长按状态管理
- `long_press_active`: 长按状态标志
- `long_press_start_time`: 长按开始时间
- `long_press_start_x/y`: 长按开始坐标
- 允许20像素范围内的移动，避免手指微动取消长按

### 3. 深度休眠处理
```c
static void deep_sleep_handler(void) {
    // 1. 保存当前状态
    // 2. 关闭屏幕背光
    // 3. 配置唤醒源
    // 4. 进入深度休眠
    esp_deep_sleep_start();
}
```

## 🎯 工作流程

### 触摸检测流程
1. **触摸检测**: 每100ms检测一次触摸状态
2. **区域判断**: 判断触摸位置是否在右上角区域
3. **长按计时**: 如果在区域内，开始计时
4. **移动容错**: 允许20像素范围内的移动
5. **时间达成**: 3秒后触发深度休眠
6. **状态重置**: 离开区域或松手时重置状态

### 日志输出
- 🌙 LONG PRESS: Started - 开始长按检测
- 🌙 LONG PRESS: Progress - 显示进度（每500ms）
- 🌙 LONG PRESS: Cancelled - 取消长按（移动或松手）
- 🌙 DEEP SLEEP: 进入休眠的各个步骤

## ⚙️ 配置选项

### 可调参数
- `LONG_PRESS_DURATION_MS`: 长按持续时间（默认3000ms）
- `SLEEP_AREA_SIZE`: 触摸区域大小（默认140px）
- 移动容错距离: 20像素
- 轮询间隔: 100ms

### 唤醒配置（可扩展）
```c
// 可以添加的唤醒源配置
esp_sleep_enable_ext1_wakeup(BUTTON_PIN_SEL, ESP_EXT1_WAKEUP_ANY_HIGH);
esp_sleep_enable_timer_wakeup(WAKEUP_TIME_SEC * 1000000);
```

## 🚀 测试方法

### 基本测试
1. 触摸右上角区域并保持3秒
2. 观察串口日志输出
3. 确认设备进入深度休眠
4. 重启设备确认唤醒

### 边界测试
1. 测试移动超出20像素范围
2. 测试提前松手
3. 测试在不同区域的触摸
4. 测试与左下角切换功能的协同

## 📊 功能状态

- ✅ 右上角区域检测
- ✅ 长按时间检测（3秒）
- ✅ 移动容错处理
- ✅ 深度休眠功能
- ✅ 详细日志输出
- ⏳ 唤醒源配置（可扩展）
- ⏳ 状态恢复（可扩展）

## 🔄 与现有功能的协同

- **左下角触摸切换**: 继续正常工作，不受影响
- **四模式循环**: 正常运行，休眠不会影响模式状态
- **防抖机制**: 独立的长按检测，不影响快速触摸的防抖
- **坐标转换**: 使用相同的坐标转换逻辑

---

**实现状态**: ✅ 完成  
**测试状态**: ⏳ 待测试  
**集成状态**: ✅ 已集成到现有系统
