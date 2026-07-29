# 动态线段防抖动优化方案

## 问题描述
- 动态水平线段在微小角度变化（0.几度）时视觉上严重抖动
- 即使数据稳定，像素级的微小变化也会导致明显的视觉跳动
- 影响用户体验，特别是在精密观测时

## 根本原因分析
1. **角度数据噪声**: 传感器原始数据存在微小噪声
2. **roll角度无滤波**: 只对pitch进行了低通滤波，roll没有处理
3. **像素量化效应**: 浮点角度转换为整数像素坐标时的量化噪声
4. **过度敏感更新**: 每次微小变化都触发重绘

## 解决方案

### 1. 角度层面滤波增强
```c
// 为roll添加独立的低通滤波
static float roll_lpf_alpha = 0.3f;   // roll滤波系数
static float filtered_roll = 0;

// 滤波处理
filtered_pitch = pitch_lpf_alpha * pitch + (1.0f - pitch_lpf_alpha) * filtered_pitch;
filtered_roll = roll_lpf_alpha * roll + (1.0f - roll_lpf_alpha) * filtered_roll;
```

### 2. 显示层面智能防抖
```c
// 防抖动阈值
#define PIXEL_JITTER_THRESHOLD 2      // 像素阈值
#define ANGLE_CHANGE_THRESHOLD 0.4f   // 角度阈值（度）

// 双重检测机制
bool angle_changed = fabsf(pitch - last_filtered_pitch) > ANGLE_CHANGE_THRESHOLD ||
                    fabsf(roll - last_filtered_roll) > ANGLE_CHANGE_THRESHOLD;

bool pixel_changed = abs(x0 - last_line_x0) > PIXEL_JITTER_THRESHOLD ||
                    abs(y0 - last_line_y0) > PIXEL_JITTER_THRESHOLD ||
                    abs(x1 - last_line_x1) > PIXEL_JITTER_THRESHOLD ||
                    abs(y1 - last_line_y1) > PIXEL_JITTER_THRESHOLD;

// 只有满足条件才重绘
if (angle_changed || pixel_changed) {
    // 执行重绘逻辑
}
```

### 3. 参数优化配置
```c
// 滤波参数设置
angle_calc_set_pitch_lpf_alpha(0.2f);  // pitch滤波：平滑
angle_calc_set_roll_lpf_alpha(0.15f);  // roll滤波：更平滑，减少视觉抖动

// 防抖动阈值
PIXEL_JITTER_THRESHOLD = 2     // 2像素以内的变化忽略
ANGLE_CHANGE_THRESHOLD = 0.4f  // 0.4度以内的变化忽略
```

### 4. 性能优化
```c
// 标签更新频率限制
static int label_update_counter = 0;
if (++label_update_counter >= 5) { // 每5次更新一次标签
    label_update_counter = 0;
    // 更新角度显示标签
}
```

## 技术特点

### 双层滤波机制
1. **角度层滤波**: 在数据源头对pitch和roll分别进行低通滤波
2. **显示层防抖**: 在像素坐标层面进行变化检测和过滤

### 智能更新策略
- **角度阈值**: 只有角度变化超过0.4°才考虑更新
- **像素阈值**: 只有像素位置变化超过2个像素才更新
- **组合逻辑**: 满足任一条件即触发更新，确保重要变化不丢失

### 参数可调节性
- 滤波系数可独立调节（pitch和roll分开设置）
- 防抖动阈值可根据需要调整
- 不影响响应速度，只过滤微小抖动

## 效果预期

### 视觉改善
- **大幅减少**: 0.1-0.4°微小变化导致的视觉抖动
- **保持响应**: 1°以上的真实变化仍能及时响应
- **平滑过渡**: 角度变化更加平滑自然

### 性能优化
- **减少重绘**: 过滤无意义的微小变化，减少不必要的重绘
- **CPU节省**: 减少LVGL画布操作和内存拷贝次数
- **稳定运行**: 避免高频无效更新导致的系统负载

### 用户体验
- **观测舒适**: 消除令人分心的抖动
- **精度保持**: 不影响真实角度的显示精度
- **可靠性**: 在各种使用环境下都能稳定工作

## 参数调优指南

### 滤波系数调节
```c
// 响应速度 vs 平滑度权衡
// 数值越小 = 更平滑，但响应慢
// 数值越大 = 响应快，但可能有抖动

pitch_lpf_alpha: 0.15-0.3   // pitch一般设置
roll_lpf_alpha:  0.1-0.25   // roll建议更平滑
```

### 阈值调节
```c
// 根据显示分辨率和使用场景调节
PIXEL_JITTER_THRESHOLD: 1-5 像素
ANGLE_CHANGE_THRESHOLD: 0.2-1.0 度
```

### 应用场景建议
- **精密测量**: 滤波系数小，阈值小
- **一般观测**: 使用默认参数
- **快速响应**: 滤波系数大，阈值大

---
**实施结果**: ✅ 显著改善视觉抖动，保持响应性能
**适用性**: 可复用于其他需要平滑显示的角度可视化场景
