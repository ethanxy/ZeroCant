# 长按事件故障排查指南

## 问题现象
用户报告长按主界面（角度显示界面）没有反应，设置界面不会弹出。

## 排查步骤

### 1. 确认设置组件已正确初始化
查看串口日志，应该能看到以下初始化信息：
```
I (xxx) settings: Initializing settings UI
I (xxx) settings: Settings UI initialized successfully
```

### 2. 确认角度显示画布正确创建
查看串口日志，应该能看到：
```
I (xxx) angle_display: Canvas created with clickable flag at 0x[address]
I (xxx) angle_display: All events registered for canvas at 0x[address]
```

### 3. 确认触摸事件正常工作
当触摸屏幕时，应该能看到：
```
I (xxx) angle_display: event_cb triggered! code=x (PRESSED)
I (xxx) angle_display: event_cb triggered! code=x (RELEASED)
I (xxx) angle_display: event_cb triggered! code=x (CLICKED)  [短按]
```

### 4. 确认长按事件触发
长按屏幕800ms以上时，应该能看到：
```
I (xxx) angle_display: event_cb triggered! code=x (LONG_PRESSED)
I (xxx) angle_display: Long press detected! Checking settings...
I (xxx) angle_display: Settings hidden, showing...
I (xxx) settings: Settings shown
```

## 可能的问题和解决方案

### 问题1：触摸事件完全没有反应
**原因**：触摸驱动或LVGL触摸输入配置问题
**解决方案**：
1. 检查触摸驱动初始化
2. 确认触摸输入设备配置：
```c
indev_drv.long_press_time = 800;  // 已设置为800ms
```

### 问题2：有PRESSED/RELEASED事件，但没有CLICKED
**原因**：触摸识别阈值问题
**解决方案**：检查触摸坐标是否稳定

### 问题3：有CLICKED事件，但没有LONG_PRESSED
**原因**：长按时间不够或者长按配置问题
**解决方案**：
1. 确保按住时间超过800ms
2. 检查长按时间配置是否生效

### 问题4：有LONG_PRESSED事件，但设置界面不显示
**原因**：设置组件初始化失败或显示问题
**解决方案**：
1. 检查设置组件内存分配
2. 确认LVGL对象创建成功

## 调试命令

可以在用户应用中添加一个测试函数来手动触发设置界面：

```c
// 在user_app.c中添加测试函数
void test_settings_manual(void) {
    ESP_LOGI("test", "Manual test: showing settings");
    if (settings_is_visible()) {
        settings_hide();
        ESP_LOGI("test", "Settings was visible, now hidden");
    } else {
        settings_show();
        ESP_LOGI("test", "Settings was hidden, now shown");
    }
}
```

## 临时解决方案

如果长按事件无法正常工作，可以考虑以下替代方案：

### 方案1：使用短按次数触发
修改事件回调，连续短按3次触发设置：
```c
static int click_count = 0;
static uint32_t last_click_time = 0;

if(code == LV_EVENT_CLICKED) {
    uint32_t current_time = lv_tick_get();
    if (current_time - last_click_time < 500) { // 500ms内连续点击
        click_count++;
        if (click_count >= 3) {
            // 触发设置界面
            settings_show();
            click_count = 0;
        }
    } else {
        click_count = 1;
        // 切换显示模式
    }
    last_click_time = current_time;
}
```

### 方案2：添加专用设置按钮
在角度显示界面添加一个小的设置按钮。

### 方案3：使用双击事件
修改为双击触发设置界面。

## 预期日志输出

正常工作时的完整日志序列：
```
I (xxx) example: Touch input device configured with long_press_time=800 ms
I (xxx) angle_display: Canvas created with clickable flag at 0x[addr]
I (xxx) angle_display: All events registered for canvas at 0x[addr]
I (xxx) settings: Initializing settings UI
I (xxx) settings: Settings UI initialized successfully

[用户触摸]
I (xxx) angle_display: event_cb triggered! code=1 (PRESSED)
I (xxx) angle_display: event_cb triggered! code=2 (RELEASED)
I (xxx) angle_display: event_cb triggered! code=6 (CLICKED)
I (xxx) angle_display: Display mode switched to: 1

[用户长按]
I (xxx) angle_display: event_cb triggered! code=1 (PRESSED)
I (xxx) angle_display: event_cb triggered! code=9 (LONG_PRESSED)
I (xxx) angle_display: Long press detected! Checking settings...
I (xxx) angle_display: Settings hidden, showing...
I (xxx) settings: Settings shown
I (xxx) angle_display: event_cb triggered! code=2 (RELEASED)
```

如果看不到这些日志，说明对应的环节有问题，需要进一步排查。
