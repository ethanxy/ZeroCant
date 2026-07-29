#include "amoled_burn_protection.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "amoled_burn_protection";

typedef struct {
    amoled_burn_protection_config_t config;
    esp_timer_handle_t timer_handle;
    lv_disp_t* display;
    lv_disp_drv_t* original_drv;
    int8_t current_offset_x;
    int8_t current_offset_y;
    bool is_running;
    bool is_initialized;
} amoled_burn_protection_context_t;

static amoled_burn_protection_context_t g_context = {0};

// 默认配置
static const amoled_burn_protection_config_t default_config = {
    .offset_interval_ms = 30000,        // 30秒间隔
    .max_offset_pixels = 2,             // 最大2像素偏移
    .enable_random_offset = true,       // 随机偏移模式
    .target_display = NULL              // 默认显示器
};

/**
 * @brief 应用显示偏移
 */
static void apply_display_offset(int8_t offset_x, int8_t offset_y) {
    if (!g_context.display || !g_context.original_drv) {
        ESP_LOGE(TAG, "Display or driver not initialized");
        return;
    }

    lv_disp_drv_t* drv = g_context.display->driver;
    
    // 保存原始分辨率（只在第一次保存）
    static bool original_saved = false;
    static lv_coord_t original_hor_res, original_ver_res;
    
    if (!original_saved) {
        original_hor_res = drv->hor_res;
        original_ver_res = drv->ver_res;
        original_saved = true;
    }
    
    // 应用偏移：通过调整有效分辨率实现视觉偏移
    drv->hor_res = original_hor_res - abs(offset_x);
    drv->ver_res = original_ver_res - abs(offset_y);
    
    // 更新当前偏移
    g_context.current_offset_x = offset_x;
    g_context.current_offset_y = offset_y;
    
    // 刷新显示
    lv_obj_invalidate(lv_scr_act());
    
    ESP_LOGI(TAG, "Applied offset: x=%d, y=%d (res: %dx%d)", 
             offset_x, offset_y, drv->hor_res, drv->ver_res);
}

/**
 * @brief 生成下一个偏移值
 */
static void generate_next_offset(int8_t* offset_x, int8_t* offset_y) {
    if (g_context.config.enable_random_offset) {
        // 随机偏移模式
        *offset_x = (rand() % (g_context.config.max_offset_pixels * 2 + 1)) - g_context.config.max_offset_pixels;
        *offset_y = (rand() % (g_context.config.max_offset_pixels * 2 + 1)) - g_context.config.max_offset_pixels;
    } else {
        // 固定模式：循环偏移
        static uint8_t step = 0;
        const int8_t patterns[][2] = {
            {0, 0}, {1, 0}, {1, 1}, {0, 1}, 
            {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        };
        const uint8_t pattern_count = sizeof(patterns) / sizeof(patterns[0]);
        
        *offset_x = patterns[step][0] * g_context.config.max_offset_pixels;
        *offset_y = patterns[step][1] * g_context.config.max_offset_pixels;
        
        step = (step + 1) % pattern_count;
    }
}

/**
 * @brief 定时器回调函数
 */
static void burn_protection_timer_callback(void* arg) {
    if (!g_context.is_running) {
        return;
    }
    
    int8_t new_offset_x, new_offset_y;
    generate_next_offset(&new_offset_x, &new_offset_y);
    
    // 在LVGL任务中执行偏移操作
    if (lv_disp_get_default()) {
        apply_display_offset(new_offset_x, new_offset_y);
        ESP_LOGD(TAG, "Burn protection offset applied: (%d, %d)", new_offset_x, new_offset_y);
    }
}

esp_err_t amoled_burn_protection_init(const amoled_burn_protection_config_t* config) {
    if (g_context.is_initialized) {
        ESP_LOGW(TAG, "AMOLED burn protection already initialized");
        return ESP_OK;
    }
    
    // 使用提供的配置或默认配置
    if (config) {
        memcpy(&g_context.config, config, sizeof(amoled_burn_protection_config_t));
    } else {
        memcpy(&g_context.config, &default_config, sizeof(amoled_burn_protection_config_t));
    }
    
    // 获取目标显示器
    g_context.display = g_context.config.target_display ? 
                       g_context.config.target_display : lv_disp_get_default();
    
    if (!g_context.display) {
        ESP_LOGE(TAG, "No display available");
        return ESP_ERR_NOT_FOUND;
    }
    
    g_context.original_drv = g_context.display->driver;
    if (!g_context.original_drv) {
        ESP_LOGE(TAG, "Display driver not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = burn_protection_timer_callback,
        .arg = NULL,
        .name = "amoled_burn_protection_timer"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &g_context.timer_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 初始化偏移
    g_context.current_offset_x = 0;
    g_context.current_offset_y = 0;
    g_context.is_running = false;
    g_context.is_initialized = true;
    
    ESP_LOGI(TAG, "AMOLED burn protection initialized (interval: %dms, max_offset: %dpx, random: %s)",
             g_context.config.offset_interval_ms, 
             g_context.config.max_offset_pixels,
             g_context.config.enable_random_offset ? "yes" : "no");
    
    return ESP_OK;
}

esp_err_t amoled_burn_protection_start(void) {
    if (!g_context.is_initialized) {
        ESP_LOGE(TAG, "AMOLED burn protection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_context.is_running) {
        ESP_LOGW(TAG, "AMOLED burn protection already running");
        return ESP_OK;
    }
    
    // 启动定时器
    esp_err_t ret = esp_timer_start_periodic(g_context.timer_handle, 
                                           g_context.config.offset_interval_ms * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_context.is_running = true;
    ESP_LOGI(TAG, "AMOLED burn protection started");
    
    return ESP_OK;
}

esp_err_t amoled_burn_protection_stop(void) {
    if (!g_context.is_initialized) {
        ESP_LOGE(TAG, "AMOLED burn protection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_context.is_running) {
        ESP_LOGW(TAG, "AMOLED burn protection not running");
        return ESP_OK;
    }
    
    // 停止定时器
    esp_err_t ret = esp_timer_stop(g_context.timer_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_context.is_running = false;
    
    // 重置偏移
    amoled_burn_protection_reset_offset();
    
    ESP_LOGI(TAG, "AMOLED burn protection stopped");
    
    return ESP_OK;
}

esp_err_t amoled_burn_protection_reset_offset(void) {
    if (!g_context.is_initialized) {
        ESP_LOGE(TAG, "AMOLED burn protection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    apply_display_offset(0, 0);
    ESP_LOGI(TAG, "Display offset reset to origin");
    
    return ESP_OK;
}

esp_err_t amoled_burn_protection_get_offset(int8_t* offset_x, int8_t* offset_y) {
    if (!g_context.is_initialized) {
        ESP_LOGE(TAG, "AMOLED burn protection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (offset_x) *offset_x = g_context.current_offset_x;
    if (offset_y) *offset_y = g_context.current_offset_y;
    
    return ESP_OK;
}
