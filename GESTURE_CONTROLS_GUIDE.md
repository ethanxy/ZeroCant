# 手势控制使用指南

## 更新说明

项目已更新为基于滑动手势的界面切换方式，移除了之前的双击/长按切换和关闭按钮。

## 当前手势控制

### Angle Display 界面
- **左右滑动**: 打开/关闭设置界面
- **单击**: 无功能（仅记录日志）

### Settings 设置界面
- **左右滑动**: 关闭设置界面，返回角度显示
- **亮度滑块**: 调节屏幕亮度 (10-255)
- **校准按钮**: 执行加速度计校准

## 已删除的功能

### 从 Angle Display 删除
- ✅ 单击切换显示模式（颜色水平仪）
- ✅ 双击显示设置界面
- ✅ 长按显示设置界面
- ✅ 颜色模式水平仪（绿色/红色背景）

### 从 Settings 删除
- ✅ "Close" 关闭按钮
- ✅ 关闭按钮事件处理函数
- ✅ 相关的结构体字段和变量

## 手势实现细节

### 手势检测
- 使用 LVGL 的 `LV_EVENT_GESTURE` 事件
- 通过 `lv_indev_get_gesture_dir()` 获取滑动方向
- 支持 `LV_DIR_LEFT` 和 `LV_DIR_RIGHT`

### 代码位置
- **Angle Display**: `/components/angle_display/angle_display.c`
  - 事件回调: `angle_disp_canvas_event_cb()`
  - 事件注册: `LV_EVENT_GESTURE`

- **Settings**: `/components/settings/settings.c`
  - 事件回调: `settings_gesture_event_cb()`
  - 事件注册: `LV_EVENT_GESTURE`

## 用户体验

### 优点
- 更直观的滑动手势操作
- 减少意外触发
- 简化界面（无关闭按钮）
- 统一的交互方式

### 注意事项
- 滑动手势需要一定的滑动距离才能触发
- 左右滑动都可以切换界面
- 设置界面只能通过滑动关闭

## 调试信息

手势事件会输出详细的日志信息：
```
angle_display: Left swipe detected! Toggling settings...
settings: Right swipe detected! Hiding settings...
```

这些日志可以帮助调试手势识别和界面切换功能。
