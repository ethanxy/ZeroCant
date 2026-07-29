#include "level_display.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

static lv_obj_t *level_canvas = NULL;
static int disp_width = 240;
static int disp_height = 240;
static lv_color_t *cbuf = NULL;

void level_display_init(lv_obj_t *parent, int screen_width, int screen_height) {
    disp_width = screen_width;
    disp_height = screen_height;
    
    printf("level_display_init: initializing %dx%d display\n", disp_width, disp_height);
    
    // 简化的资源检查
    if (level_canvas || cbuf) {
        printf("level_display_init: Warning - some resources not cleaned properly\n");
    }
    
    // 分配画布缓冲区
    size_t buf_size = sizeof(lv_color_t) * disp_width * disp_height;
    
    // 使用heap_caps_malloc，优先PSRAM
    cbuf = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cbuf) {
        // 回退到内部RAM
        cbuf = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    if (!cbuf) {
        printf("level_display: canvas buffer alloc failed!\n");
        return;
    }
    memset(cbuf, 0, buf_size);
    
    // 创建画布
    level_canvas = lv_canvas_create(parent);
    lv_obj_set_size(level_canvas, disp_width, disp_height);
    lv_obj_align(level_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_set_buffer(level_canvas, cbuf, disp_width, disp_height, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(level_canvas, lv_color_black(), LV_OPA_COVER);
    
    printf("level_display_init: completed successfully\n");
}

void level_display_update(float roll) {
    // 多重检查，防止并发访问已删除的对象
    if (!level_canvas || !cbuf) return;
    
    // 再次检查对象状态，防止并发访问
    if (!lv_obj_is_valid(level_canvas)) {
        level_canvas = NULL; // 清理无效引用
        return;
    }
    
    // 第三次检查，确保在验证过程中对象没有被删除
    if (!level_canvas || !cbuf) return;
    
    lv_color_t color;
    float abs_roll = fabsf(roll);
    
    // 根据roll角度设置颜色
    if (abs_roll <= 0.4f) {
        color = lv_color_hex(0x00FF00); // 绿色：≤±0.4°
    } else if (abs_roll > 0.4f && abs_roll < 1.0f) {
        color = lv_color_hex(0xFFFF00); // 黄色：±0.4°-±1°
    } else {
        color = lv_color_hex(0xFF0000); // 红色：>±1°
    }
    
    lv_canvas_fill_bg(level_canvas, color, LV_OPA_COVER);
}

// 清理函数
void level_display_cleanup(void) {
    printf("level_display_cleanup: cleaning up\n");
    
    // 先将画布设为黑色，减少视觉跳跃
    if (level_canvas && lv_obj_is_valid(level_canvas)) {
        lv_canvas_fill_bg(level_canvas, lv_color_black(), LV_OPA_COVER);
        vTaskDelay(pdMS_TO_TICKS(10)); // 短暂延迟让黑色显示
    }
    
    // 删除LVGL对象
    if (level_canvas) {
        lv_obj_del(level_canvas);
        level_canvas = NULL;
    }
    
    // 释放内存缓冲区
    if (cbuf) {
        heap_caps_free(cbuf);
        cbuf = NULL;
    }
    
    printf("level_display_cleanup: completed\n");
}
