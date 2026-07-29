# 手势检测问题修复说明

## 问题描述
在实现 ESP-IDF + LVGL 项目中 angle_display 与 settings 界面之间的左右滑动手势切换功能时，遇到了滑动没有反应的问题。

## 问题根本原因
1. **LVGL 版本差异**: 项目使用的 LVGL 8.3.11 版本中，手势配置API与新版本不同
2. **错误的API调用**: 使用了不存在的 `lv_indev_set_gesture_min_velocity()` 和 `lv_indev_set_gesture_limit()` 函数
3. **对象配置不完整**: 画布对象未正确配置来接收手势/滚动事件

## 解决方案

### 1. 修正输入设备驱动配置 (main.c)
**修改前**:
```c
lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
// 错误的API调用
lv_indev_set_gesture_min_velocity(indev, 20);
lv_indev_set_gesture_limit(indev, 10);
```

**修改后**:
```c
// 在注册驱动前直接设置结构体字段
indev_drv.gesture_min_velocity = 20;       // 手势最小速度
indev_drv.gesture_limit = 10;              // 手势最小距离
indev_drv.scroll_limit = 5;                // 滚动最小距离

lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
```

### 2. 增强 angle_display 对象配置
**修改内容**:
```c
// 确保画布可以接收手势和滚动事件
lv_obj_add_flag(angle_disp_canvas, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_flag(angle_disp_canvas, LV_OBJ_FLAG_SCROLLABLE);  // 启用滚动来检测手势
lv_obj_clear_flag(angle_disp_canvas, LV_OBJ_FLAG_SCROLL_CHAIN);

// 设置滚动方向为水平，用于检测左右滑动
lv_obj_set_scroll_dir(angle_disp_canvas, LV_DIR_HOR);
lv_obj_set_scroll_snap_x(angle_disp_canvas, LV_SCROLL_SNAP_NONE);

// 启用手势检测
lv_obj_add_flag(angle_disp_canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
lv_obj_set_ext_click_area(angle_disp_canvas, 10);
```

### 3. 双重事件处理策略
由于 LVGL 8.3 中手势事件可能不稳定，我们实现了双重检测机制：

**手势事件处理**:
```c
if(code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        // 切换设置界面
    }
}
```

**滚动事件处理** (作为备用方案):
```c
else if(code == LV_EVENT_SCROLL) {
    lv_indev_t *indev = lv_indev_get_act();
    if(indev != NULL) {
        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);
        
        // 检测水平滑动
        if(abs(vect.x) > abs(vect.y) && abs(vect.x) > 20) {
            // 切换设置界面
        }
    }
}
```

### 4. settings 界面相同修改
对 settings 组件应用了相同的配置和事件处理逻辑：
- 启用 `LV_OBJ_FLAG_SCROLLABLE` 和水平滚动方向
- 注册 `LV_EVENT_GESTURE` 和 `LV_EVENT_SCROLL` 事件
- 实现双重检测机制

## API 修正说明

### lv_indev_get_vect() 函数
**错误调用**:
```c
lv_point_t vect = lv_indev_get_vect(indev);  // 错误！缺少参数
```

**正确调用**:
```c
lv_point_t vect;
lv_indev_get_vect(indev, &vect);  // 正确！通过指针返回结果
```

## 调试和验证

### 1. 事件日志
在事件回调中添加了详细的日志输出：
```c
ESP_LOGI("angle_display", "event_cb triggered! code=%d (%s)", code, 
         code == LV_EVENT_GESTURE ? "GESTURE" : 
         code == LV_EVENT_SCROLL ? "SCROLL" : "OTHER");
```

### 2. 手势参数日志
```c
ESP_LOGI(TAG, "Touch input device configured with gesture_min_velocity=%d, gesture_limit=%d", 
         indev_drv.gesture_min_velocity, indev_drv.gesture_limit);
```

### 3. 滚动向量日志
```c
ESP_LOGI("angle_display", "Scroll event: vect=(%d,%d)", vect.x, vect.y);
```

## 预期效果
通过这些修改，现在应该能够：
1. 在 angle_display 界面上左右滑动来显示/隐藏 settings 界面
2. 在 settings 界面上左右滑动来关闭设置界面
3. 两种事件机制提供冗余保障，提高手势识别的可靠性
4. 通过日志监控手势检测的工作状态

## 测试建议
1. 编译并烧录程序到设备
2. 观察串口日志输出，确认：
   - 输入设备配置成功
   - 事件注册成功
   - 触摸操作触发相应事件
3. 测试左右滑动功能是否正常工作
4. 如果仍有问题，通过日志分析哪种事件类型正常工作
