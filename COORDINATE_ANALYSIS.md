# 触摸坐标系和手势检测分析

## 坐标系理论

```
屏幕坐标系 (LVGL标准)
┌─────────────────────┐ (280, 0)
│ (0,0)               │
│   ↓ Y轴向下         │
│   X轴向右 →         │
│                     │
│                     │
└─────────────────────┘ (280, 456)
```

## 手势方向计算

```c
// 当前代码的计算方式
int dx = current_pos.x - start_pos.x;  // 水平偏移
int dy = current_pos.y - start_pos.y;  // 垂直偏移

// 手势方向判断：
// 向右滑动: dx > 0 (end_x > start_x)
// 向左滑动: dx < 0 (end_x < start_x)  
// 向下滑动: dy > 0 (end_y > start_y) ✅ 用于显示设置
// 向上滑动: dy < 0 (end_y < start_y) ✅ 用于隐藏设置
```

## 当前代码检查

### angle_display.c (角度显示 - 检测下滑)
```c
// 第89行：向下滑动检测
if(dy > SWIPE_MIN_DISTANCE && dy > abs(dx) && duration < SWIPE_MAX_TIME) {
    // dy > 30 && dy > |dx| -> 向下滑动且垂直分量占主导
    settings_show(); // ✅ 逻辑正确
}
```

### settings.c (设置界面 - 检测上滑)
```c  
// 第183行：向上滑动检测
if(dy < -SETTINGS_SWIPE_MIN_DISTANCE && abs(dy) > abs(dx) * 2 && duration < SETTINGS_SWIPE_MAX_TIME) {
    // dy < -30 && |dy| > |dx|*2 -> 向上滑动且垂直分量显著占主导
    settings_hide(); // ✅ 逻辑正确
}
```

## 可能的问题点

1. **事件优先级冲突**
   - 多个组件同时注册了相同的手势事件
   - LVGL内置手势检测可能与自定义检测冲突

2. **触摸区域重叠**
   - angle_display 和 settings 组件的触摸区域可能重叠
   - 导致意外的事件传播

3. **LVGL手势检测阈值**
   - main.c中设置的gesture_min_velocity=15, gesture_limit=8可能过于敏感
   - 可能导致水平滑动被误识别为垂直滑动

4. **事件传播机制**
   - 某个组件可能截获了应该传递给其他组件的事件

## 诊断建议

### 1. 添加详细的触摸坐标日志
```c
ESP_LOGI(TAG, "TOUCH: start=(%d,%d) end=(%d,%d) delta=(%d,%d) abs_dx=%d abs_dy=%d", 
         start_pos.x, start_pos.y, current_pos.x, current_pos.y, 
         dx, dy, abs(dx), abs(dy));
```

### 2. 检查组件层级和事件传播
- 确认哪个组件首先接收到触摸事件
- 验证事件是否被正确传播或阻断

### 3. 暂时禁用LVGL内置手势检测
- 只使用自定义的PRESSED/RELEASED检测
- 避免多重检测机制冲突

### 4. 调整检测阈值
- 降低最小滑动距离要求
- 增加垂直方向的权重比较

---
*分析时间: 2025年7月11日*
