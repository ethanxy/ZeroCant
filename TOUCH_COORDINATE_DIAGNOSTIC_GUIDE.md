# 触摸坐标诊断测试指南

## 🔍 增强的坐标系诊断已部署

现在代码包含详细的触摸坐标和手势分析日志。

## 📋 测试步骤

### 1. 烧录固件
```bash
idf.py flash
```

### 2. 连接串口监视器
```bash
idf.py monitor
```

### 3. 执行测试手势

#### 测试A: 在角度显示界面执行下滑
1. 确保在角度显示界面（主界面）
2. 从屏幕上方向下滑动
3. 观察日志输出

**期望日志格式：**
```
angle_display: 🔍 COORDINATE ANALYSIS:
angle_display:   Start pos: (140, 100)
angle_display:   End pos:   (140, 200)  
angle_display:   Delta:     dx=0, dy=100
angle_display:   Abs values: |dx|=0, |dy|=100
angle_display:   Duration:   500 ms
angle_display:   Gesture type: DOWN_SWIPE
angle_display: ✅ DOWN SWIPE CONFIRMED! dy=100 > 30 && dy=100 > abs(dx)=0 - Showing settings...
```

#### 测试B: 在设置界面执行上滑
1. 确保设置界面已显示
2. 从屏幕下方向上滑动
3. 观察日志输出

**期望日志格式：**
```
settings: 🔍 SETTINGS COORDINATE ANALYSIS:
settings:   Start pos: (140, 300)
settings:   End pos:   (140, 200)
settings:   Delta:     dx=0, dy=-100
settings:   Abs values: |dx|=0, |dy|=100  
settings:   Duration:   500 ms
settings:   Gesture type: UP_SWIPE
settings: ✅ UP SWIPE CONFIRMED! dy=-100 < -30 && abs(dy)=100 > abs(dx)*2=0 - Hiding settings...
```

#### 测试C: 执行水平滑动（应被阻止）
1. 从屏幕左侧向右滑动
2. 从屏幕右侧向左滑动
3. 观察日志输出

**期望日志格式：**
```
angle_display: 🔍 COORDINATE ANALYSIS:
angle_display:   Start pos: (50, 200)
angle_display:   End pos:   (200, 200)
angle_display:   Delta:     dx=150, dy=0
angle_display:   Abs values: |dx|=150, |dy|=0
angle_display:   Duration:   600 ms
angle_display:   Gesture type: HORIZONTAL_SWIPE
angle_display: ❌ HORIZONTAL SWIPE BLOCKED! abs(dx)=150 > 20 && abs(dx)=150 > abs(dy)=0
```

## 🎯 关键诊断点

### 1. 坐标系验证
- **Start pos** 和 **End pos** 应该符合屏幕坐标范围 (0-280, 0-456)
- **Delta** 计算应该正确：`dx = end_x - start_x`, `dy = end_y - start_y`

### 2. 手势方向判断
- **向下滑动**: `dy > 0` (Y坐标增加)
- **向上滑动**: `dy < 0` (Y坐标减少)
- **向右滑动**: `dx > 0` (X坐标增加)
- **向左滑动**: `dx < 0` (X坐标减少)

### 3. 可能的问题识别

#### 如果下滑无法显示设置：
- 检查 `dy` 值是否 > 30
- 检查 `dy` 是否 > `abs(dx)` (垂直分量占主导)
- 检查 duration 是否 < 1500ms

#### 如果水平滑动意外触发设置：
- 检查 `abs(dx)` 和 `abs(dy)` 的比较结果
- 可能存在事件传播问题或多重检测

#### 如果上滑无法关闭设置：
- 检查 `dy` 值是否 < -30
- 检查 `abs(dy)` 是否 > `abs(dx) * 2` (更严格的垂直主导检查)

## 📱 报告格式

请将测试结果按以下格式报告：

```
**测试结果报告**

1. 下滑测试 (角度界面 -> 设置界面):
   - 手势: [描述您的滑动动作]
   - 日志: [粘贴完整的 COORDINATE ANALYSIS 日志]
   - 结果: [成功显示设置/失败/其他]

2. 上滑测试 (设置界面 -> 角度界面):
   - 手势: [描述您的滑动动作]  
   - 日志: [粘贴完整的 COORDINATE ANALYSIS 日志]
   - 结果: [成功隐藏设置/失败/其他]

3. 水平滑动测试:
   - 手势: [描述您的滑动动作]
   - 日志: [粘贴完整的 COORDINATE ANALYSIS 日志]  
   - 结果: [正确阻止/意外触发/其他]
```

## 🚀 下一步诊断

根据测试结果，我们可以：
1. 确认坐标系计算是否正确
2. 识别事件传播或检测优先级问题
3. 调整检测阈值或算法
4. 检查LVGL配置参数

---
*诊断工具版本: v2.0*
*部署时间: 2025年7月11日*
