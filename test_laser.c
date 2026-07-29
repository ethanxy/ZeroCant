#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "laser.h"
#include "lvgl.h"

// 激光测试任务
static void laser_test_task(void *arg) {
    float test_distance = 1.0f;
    int direction = 1;  // 1 为增加，-1 为减少
    
    while (1) {
        // 模拟距离变化（1.0m 到 5.0m 之间）
        test_distance += direction * 0.1f;
        
        if (test_distance >= 5.0f) {
            direction = -1;
            laser_ui_set_status_text("Measuring... (decreasing)");
        } else if (test_distance <= 1.0f) {
            direction = 1;
            laser_ui_set_status_text("Measuring... (increasing)");
        }
        
        // 更新显示
        laser_ui_update_distance(test_distance);
        
        // 每500ms更新一次
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void test_laser_standalone(void) {
    printf("Starting laser standalone test...\n");
    
    // 初始化激光UI（假设已有LVGL环境）
    laser_ui_init(lv_scr_act(), 280, 456);
    
    // 设置初始状态
    laser_ui_set_status_text("Laser Test Mode");
    laser_ui_update_distance(1.0f);
    
    // 创建测试任务
    xTaskCreate(laser_test_task, "laser_test", 4096, NULL, 5, NULL);
    
    printf("Laser test started. Distance will cycle between 1.0m and 5.0m\n");
}
