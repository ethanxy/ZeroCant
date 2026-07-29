# 垂直滑动手势控制指南

## 手势控制方式

### 📱 显示设置界面
- **在 angle_display (角度显示) 界面**
- **手势**: 从屏幕上方**向下滑动**
- **效果**: 显示设置界面

### 📱 隐藏设置界面  
- **在 settings (设置) 界面**
- **手势**: 从屏幕下方**向上滑动**
- **效果**: 隐藏设置界面，返回角度显示

## 交互逻辑

```
角度显示界面
    ↓ (向下滑动)
设置界面
    ↑ (向上滑动)  
角度显示界面
```

## 检测参数

### 手势有效条件
- **最小滑动距离**: 50 像素
- **方向判断**: 垂直距离 > 水平距离 × 2 (确保是垂直滑动)
- **最大时间**: 1000ms (1秒内完成滑动)

### 日志示例

**angle_display 界面向下滑动**:
```
I (12345) angle_display: Touch start at (120,80)
I (12400) angle_display: Touch end at (120,140), delta=(0,60), duration=55 ms
I (12400) angle_display: Down swipe detected! (distance=60) - Showing settings...
```

**settings 界面向上滑动**:
```
I (15000) settings: Settings touch start at (120,180)
I (15050) settings: Settings touch end at (120,110), delta=(0,-70), duration=50 ms
I (15050) settings: Settings up swipe detected! (distance=70) - Hiding settings...
```

## 技术实现

### 检测机制
使用**三重检测机制**确保可靠性：

1. **自定义手势检测** (主要)
   - 基于 PRESSED → PRESSING → RELEASED 事件序列
   - 计算垂直位移和时间

2. **LVGL 手势事件** (备用)
   - `LV_EVENT_GESTURE` 
   - `LV_DIR_BOTTOM` (向下) / `LV_DIR_TOP` (向上)

3. **滚动事件检测** (第三备选)
   - `LV_EVENT_SCROLL`
   - 分析滚动向量的垂直分量

### 对象配置
- **angle_display 画布**: 启用垂直滚动 (`LV_DIR_VER`)
- **settings 容器**: 启用垂直滚动 (`LV_DIR_VER`)
- **事件注册**: GESTURE, SCROLL, PRESSED, PRESSING, RELEASED

## 已删除功能

- ❌ **颜色水平仪**: 基于 roll 角度的颜色填充显示
- ❌ **单击切换模式**: 点击切换颜色模式的功能
- ❌ **设置界面返回按钮**: "Close" 按钮及其点击事件
- ❌ **左右滑动**: 原来的水平滑动手势

## 当前功能

- ✅ **标准角度指示**: pitch/roll 角度数值和指示线显示
- ✅ **垂直滑动手势**: 上下滑动切换界面
- ✅ **设置界面**: 亮度调节和加速度计校准
- ✅ **三重检测机制**: 确保手势识别可靠性

## 测试方法

### 1. 烧录并监控
```bash
cd /Users/xuanyuliu/164
source ~/esp/v5.4.2/esp-idf/export.sh
idf.py flash monitor
```

### 2. 测试场景

**场景 1: 显示设置界面**
- 在角度显示界面上**向下滑动** (从上到下)
- 应该显示设置界面

**场景 2: 隐藏设置界面**  
- 在设置界面上**向上滑动** (从下到上)
- 应该隐藏设置界面

### 3. 观察日志
查看串口输出中的手势检测日志，确认：
- 触摸开始/拖拽/结束的位置
- 计算的垂直位移 (delta y)
- 手势检测成功的消息

## 参数调整

如需调整手势敏感度，可修改以下参数：

**angle_display.c**:
```c
#define SWIPE_MIN_DISTANCE 50  // 减小 = 更敏感
#define SWIPE_MAX_TIME 1000    // 增加 = 允许更慢的滑动
```

**settings.c**:
```c
#define SETTINGS_SWIPE_MIN_DISTANCE 50
#define SETTINGS_SWIPE_MAX_TIME 1000
```

## 修改总结

### 主要变化
1. **改变滑动方向**: 从水平滑动改为垂直滑动
2. **单向触发**: angle_display 只响应向下滑动，settings 只响应向上滑动
3. **更新对象配置**: 滚动方向从 `LV_DIR_HOR` 改为 `LV_DIR_VER`
4. **修改检测逻辑**: 检测垂直位移而非水平位移

### 代码修改
- **angle_display.c**: 检测 dy > 50 && dy > abs(dx)*2 (向下滑动)
- **settings.c**: 检测 dy < -50 && abs(dy) > abs(dx)*2 (向上滑动)
- **滚动配置**: 两个界面都设置为垂直滚动方向
- **LVGL 手势**: 检测 `LV_DIR_BOTTOM` 和 `LV_DIR_TOP`
