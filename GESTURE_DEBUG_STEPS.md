# 手势调试指南

## 当前问题
- **下滑无反应**: 向下滑动不能呼出settings
- **左滑仍有效**: 左滑可以呼出settings (说明LVGL手势检测工作)

## 调试版本说明

我添加了详细的调试日志来诊断问题：

### 1. 触摸事件日志
```
I (xxx) angle_display: Touch start at (x,y)
I (xxx) angle_display: Touch dragging to (x,y)  [每100ms记录一次]
I (xxx) angle_display: Touch end at (x,y), delta=(dx,dy), duration=xxx ms
```

### 2. 手势检测日志
```
I (xxx) angle_display: Any swipe detected! dx=xx, dy=xx - Showing settings for debug
I (xxx) angle_display: Vertical down swipe detected! (distance=xx) - This should work!
```

### 3. LVGL手势日志
```
I (xxx) angle_display: LVGL Gesture detected! Direction: x (BOTTOM=8, TOP=4, LEFT=1, RIGHT=2)
I (xxx) angle_display: Horizontal swipe detected (LEFT/RIGHT) - This should be removed!
```

## 测试步骤

### 1. 烧录程序
```bash
cd /Users/xuanyuliu/164
source ~/esp/v5.4.2/esp-idf/export.sh
idf.py flash monitor
```

### 2. 测试不同滑动方向

**测试A: 下滑测试**
- 在angle_display界面从上往下滑动
- 观察日志中的:
  - 触摸开始/结束位置
  - delta值 (应该是正的dy值)
  - 是否触发 "Any swipe detected"
  - 是否触发 "Vertical down swipe detected"

**测试B: 左滑测试 (现在能工作)**
- 从右往左滑动
- 观察日志中是否显示:
  - "LVGL Gesture detected! Direction: 1" (LEFT)
  - "Horizontal swipe detected (LEFT)"

**测试C: 右滑测试**
- 从左往右滑动
- 观察日志中是否显示:
  - "LVGL Gesture detected! Direction: 2" (RIGHT)

### 3. 分析结果

根据日志输出，我们可以判断：

**如果没有任何触摸日志**:
- 触摸驱动问题或事件注册问题

**如果有触摸日志但没有手势检测**:
- 检查delta值是否符合条件
- 检查duration是否过长

**如果只有LVGL手势而没有自定义检测**:
- 说明LVGL手势工作，但自定义检测有问题

**如果触发"Any swipe detected"但不触发"Vertical down swipe"**:
- 说明基础检测工作，但垂直检测条件有问题

## 期望的日志输出

**成功的下滑应该显示**:
```
I (xxx) angle_display: Touch start at (120,50)
I (xxx) angle_display: Touch dragging to (120,80)
I (xxx) angle_display: Touch end at (120,120), delta=(0,70), duration=200 ms
I (xxx) angle_display: Any swipe detected! dx=0, dy=70 - Showing settings for debug
I (xxx) angle_display: Vertical down swipe detected! (distance=70) - This should work!
I (xxx) angle_display: Settings hidden, showing...
```

## 可能的问题

### 1. 滚动方向配置问题
angle_display画布设置为`LV_DIR_VER`可能阻止了触摸事件

### 2. 手势阈值问题
- 需要滑动距离 > 50像素
- 垂直距离 > 水平距离×2

### 3. LVGL版本兼容性
- `LV_DIR_BOTTOM`值可能不对应向下滑动

## 下一步调试

根据测试结果：

1. **如果自定义检测完全无效**:
   - 移除垂直滚动配置
   - 简化为只检测任意方向滑动

2. **如果LVGL手势检测正常**:
   - 专注修复LVGL手势的方向判断
   - 添加所有方向的检测来测试

3. **如果基础触摸正常但检测条件有问题**:
   - 降低阈值 (50→30像素)
   - 放宽方向判断条件

请运行测试并提供日志输出，我会根据实际情况进一步修复。
