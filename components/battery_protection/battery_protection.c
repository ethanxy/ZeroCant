#include "battery_protection.h"
#include "adc_bsp.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "driver/gpio.h"  // GPIO控制
#include <string.h>  // memset

static const char *TAG = "battery_protection";

// 全局状态管理
typedef struct {
    bool initialized;
    bool monitoring_active;
    battery_protection_state_t current_state;
    esp_timer_handle_t check_timer;
    esp_timer_handle_t countdown_timer;
    battery_shutdown_callback_t shutdown_callback;
    
    // 关机倒计时相关
    lv_obj_t *warning_screen;
    lv_obj_t *warning_label;
    lv_obj_t *countdown_label;
    int countdown_seconds;
    float last_voltage;
} battery_protection_context_t;

static battery_protection_context_t g_context = {0};

// 外部函数声明
extern void setBrightnes(uint8_t brig);
extern void setBrightnessTemporary(uint8_t brig);  // 临时设置亮度，不保存到NVS
extern void shutdownDisplay(void);
extern uint8_t getBrightness(void);

// 前置声明
static void battery_check_timer_callback(void *arg);
static void battery_countdown_timer_callback(void *arg);
static void battery_show_warning_screen(float voltage);
static void battery_hide_warning_screen(void);
static void battery_shutdown_peripherals(void);
static void battery_emergency_shutdown(void);

/**
 * @brief 关闭所有外设电源
 */
static void battery_shutdown_peripherals(void) {
    ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Shutting down peripherals...");
    
    // 关闭激光器电源 (GPIO45)
    gpio_set_level(45, 0);
    ESP_LOGI(TAG, "Laser power disabled");
    
    // 关闭其他可能的高功耗外设
    // 注意：这里可以根据实际硬件添加其他外设的关闭代码
    
    ESP_LOGI(TAG, "All peripherals powered down");
}

/**
 * @brief 显示低电量警告屏幕
 */
static void battery_show_warning_screen(float voltage) {
    ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Showing warning screen (%.2fV)", voltage);
    
    // 临时设置低亮度 (10%) - 不保存到NVS，避免影响用户设置
    uint8_t low_brightness = (uint8_t)(255 * BATTERY_LOW_BRIGHTNESS_PERCENT / 100);
    setBrightnessTemporary(low_brightness);
    ESP_LOGI(TAG, "Display brightness temporarily set to %d%% (%d/255) for battery protection", BATTERY_LOW_BRIGHTNESS_PERCENT, low_brightness);
    
    // 创建全屏警告界面
    g_context.warning_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_context.warning_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_context.warning_screen, lv_color_hex(0x800000), 0); // 深红色背景
    lv_obj_set_style_bg_opa(g_context.warning_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_context.warning_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // 主警告文本
    g_context.warning_label = lv_label_create(g_context.warning_screen);
    lv_label_set_text(g_context.warning_label, "LOW BATTERY\\nPOWER DOWN SOON");
    lv_obj_set_style_text_color(g_context.warning_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(g_context.warning_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_context.warning_label, &lv_font_montserrat_24, 0);
    lv_obj_align(g_context.warning_label, LV_ALIGN_CENTER, 0, -40);
    
    // 倒计时文本
    g_context.countdown_label = lv_label_create(g_context.warning_screen);
    lv_label_set_text_fmt(g_context.countdown_label, "%d", g_context.countdown_seconds);
    lv_obj_set_style_text_color(g_context.countdown_label, lv_color_hex(0xFFFF00), 0); // 黄色
    lv_obj_set_style_text_align(g_context.countdown_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_context.countdown_label, &lv_font_montserrat_48, 0);
    lv_obj_align(g_context.countdown_label, LV_ALIGN_CENTER, 0, 40);
    
    // 电压信息
    lv_obj_t *voltage_label = lv_label_create(g_context.warning_screen);
    lv_label_set_text_fmt(voltage_label, "Battery: %.2fV", voltage);
    lv_obj_set_style_text_color(voltage_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(voltage_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_14, 0);
    lv_obj_align(voltage_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    ESP_LOGI(TAG, "Warning screen displayed");
}

/**
 * @brief 隐藏警告屏幕
 */
static void battery_hide_warning_screen(void) {
    if (g_context.warning_screen) {
        lv_obj_del(g_context.warning_screen);
        g_context.warning_screen = NULL;
        g_context.warning_label = NULL;
        g_context.countdown_label = NULL;
        ESP_LOGI(TAG, "Warning screen hidden");
    }
}

/**
 * @brief 紧急关机处理
 */
static void battery_emergency_shutdown(void) {
    ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Emergency shutdown initiated!");
    
    // 1. 调用用户回调
    if (g_context.shutdown_callback) {
        g_context.shutdown_callback();
    }
    
    // 2. 关闭显示屏
    shutdownDisplay();
    
    // 4. 确保数据写入
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 5. 进入深度睡眠
    ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Entering deep sleep...");
    fflush(stdout);
    esp_deep_sleep_start();
}

/**
 * @brief 倒计时定时器回调
 */
static void battery_countdown_timer_callback(void *arg) {
    g_context.countdown_seconds--;
    
    if (g_context.countdown_label) {
        lv_label_set_text_fmt(g_context.countdown_label, "%d", g_context.countdown_seconds);
    }
    
    ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Shutdown in %d seconds...", g_context.countdown_seconds);
    
    if (g_context.countdown_seconds <= 0) {
        // 停止倒计时定时器
        esp_timer_stop(g_context.countdown_timer);
        
        // 执行紧急关机
        battery_emergency_shutdown();
    }
}

/**
 * @brief 电池检查定时器回调
 */
static void battery_check_timer_callback(void *arg) {
    if (!g_context.monitoring_active) {
        return;
    }
    
    float battery_voltage;
    adc_get_value(&battery_voltage);
    g_context.last_voltage = battery_voltage;
    
    ESP_LOGI(TAG, "🔋 Battery check: %.2fV", battery_voltage);
    
    if (battery_voltage <= BATTERY_CRITICAL_VOLTAGE) {
        ESP_LOGW(TAG, "🔋 CRITICAL: Battery voltage %.2fV <= %.2fV threshold!", 
                 battery_voltage, BATTERY_CRITICAL_VOLTAGE);
        
        // 更新状态
        g_context.current_state = BATTERY_PROTECTION_SHUTDOWN;
        
        // 关闭外设
        battery_shutdown_peripherals();
        
        // 显示警告屏幕
        battery_show_warning_screen(battery_voltage);
        
        // 初始化倒计时
        g_context.countdown_seconds = BATTERY_SHUTDOWN_DELAY_SEC;
        
        // 停止定期检查定时器
        esp_timer_stop(g_context.check_timer);
        
        // 启动倒计时定时器 (每秒触发)
        esp_timer_start_periodic(g_context.countdown_timer, 1000000); // 1秒 = 1,000,000微秒
        
        ESP_LOGW(TAG, "🔋 BATTERY PROTECTION: Shutdown sequence initiated - %d seconds countdown", 
                 BATTERY_SHUTDOWN_DELAY_SEC);
    }
}

/**
 * @brief 初始化电池保护功能
 */
esp_err_t battery_protection_init(battery_shutdown_callback_t shutdown_callback) {
    if (g_context.initialized) {
        ESP_LOGW(TAG, "Battery protection already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing battery protection...");
    
    // 初始化上下文
    memset(&g_context, 0, sizeof(g_context));
    g_context.shutdown_callback = shutdown_callback;
    g_context.current_state = BATTERY_PROTECTION_IDLE;
    g_context.last_voltage = 0.0f;  // 初始化电压值
    
    // 创建定期检查定时器
    esp_timer_create_args_t check_timer_args = {
        .callback = battery_check_timer_callback,
        .arg = NULL,
        .name = "battery_check"
    };
    
    esp_err_t ret = esp_timer_create(&check_timer_args, &g_context.check_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create battery check timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建倒计时定时器
    esp_timer_create_args_t countdown_timer_args = {
        .callback = battery_countdown_timer_callback,
        .arg = NULL,
        .name = "battery_countdown"
    };
    
    ret = esp_timer_create(&countdown_timer_args, &g_context.countdown_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create countdown timer: %s", esp_err_to_name(ret));
        esp_timer_delete(g_context.check_timer);
        return ret;
    }
    
    g_context.initialized = true;
    
    ESP_LOGI(TAG, "Battery protection initialized successfully");
    ESP_LOGI(TAG, "- Critical voltage: %.2fV", BATTERY_CRITICAL_VOLTAGE);
    ESP_LOGI(TAG, "- Check interval: %d minutes", BATTERY_CHECK_INTERVAL_MS / 60000);
    ESP_LOGI(TAG, "- Shutdown delay: %d seconds", BATTERY_SHUTDOWN_DELAY_SEC);
    
    return ESP_OK;
}

/**
 * @brief 启动电池保护监控
 */
esp_err_t battery_protection_start(void) {
    if (!g_context.initialized) {
        ESP_LOGE(TAG, "Battery protection not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_context.monitoring_active) {
        ESP_LOGW(TAG, "Battery protection monitoring already active");
        return ESP_OK;
    }
    
    // 立即进行一次检查
    battery_check_timer_callback(NULL);
    
    // 启动定期检查
    esp_err_t ret = esp_timer_start_periodic(g_context.check_timer, BATTERY_CHECK_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start battery check timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_context.monitoring_active = true;
    
    ESP_LOGI(TAG, "🔋 Battery protection monitoring started (check every %d minutes)", 
             BATTERY_CHECK_INTERVAL_MS / 60000);
    
    return ESP_OK;
}

/**
 * @brief 停止电池保护监控
 */
esp_err_t battery_protection_stop(void) {
    if (!g_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_context.monitoring_active) {
        return ESP_OK;
    }
    
    esp_timer_stop(g_context.check_timer);
    esp_timer_stop(g_context.countdown_timer);
    
    battery_hide_warning_screen();
    
    g_context.monitoring_active = false;
    g_context.current_state = BATTERY_PROTECTION_IDLE;
    
    ESP_LOGI(TAG, "Battery protection monitoring stopped");
    
    return ESP_OK;
}

/**
 * @brief 手动触发电池检查
 */
esp_err_t battery_protection_check_now(void) {
    if (!g_context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Manual battery protection check triggered");
    battery_check_timer_callback(NULL);
    
    return ESP_OK;
}

/**
 * @brief 获取当前电池保护状态
 */
battery_protection_state_t battery_protection_get_state(void) {
    return g_context.current_state;
}

/**
 * @brief 获取最后记录的电池电压
 */
float battery_protection_get_last_voltage(void) {
    return g_context.last_voltage;
}

/**
 * @brief 反初始化电池保护功能
 */
esp_err_t battery_protection_deinit(void) {
    if (!g_context.initialized) {
        return ESP_OK;
    }
    
    battery_protection_stop();
    
    if (g_context.check_timer) {
        esp_timer_delete(g_context.check_timer);
    }
    
    if (g_context.countdown_timer) {
        esp_timer_delete(g_context.countdown_timer);
    }
    
    battery_hide_warning_screen();
    
    memset(&g_context, 0, sizeof(g_context));
    
    ESP_LOGI(TAG, "Battery protection deinitialized");
    
    return ESP_OK;
}
