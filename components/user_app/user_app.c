#include <stdio.h>
#include "user_app.h"
#include "sd_card_bsp.h"
#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"
#include "esp_timer.h"
#include "esp_sleep.h"  // 添加深度休眠支持
#include "esp_log.h"    // 添加日志支持
#include "adc_bsp.h"
#include "esp_wifi_bsp.h"
#include "ble_scan_bsp.h"
#include "freertos/event_groups.h"
#include "qmi8658c.h"
#include "angle_display.h"
#include "angle_calc.h"
#include "laser.h"  // 添加激光组件头文件
#include "settings.h"  // 添加设置组件头文件
#include "battery_protection.h"  // 添加电池保护组件

// 外部函数声明
extern void setBrightnes(uint8_t brig);  // LCD亮度控制函数
extern uint8_t loadSavedBrightness(void);  // NVS亮度加载函数

// 编译时选择启动模式
// #define LASER_TEST_MODE  // 取消注释以启用激光测试模式
#include "level_display.h"
#include "main.h"  // 包含LVGL锁函数和shutdownDisplay函数声明
#include "esp_task_wdt.h"
#include "touch_bsp.h"
#include "ui_state_manager.h" // 添加UI状态管理器

#define EXAMPLE_LCD_H_RES 280
#define EXAMPLE_LCD_V_RES 456

// 显示模式管理
typedef enum {
    DISPLAY_MODE_ANGLE = 0,
    DISPLAY_MODE_LEVEL = 1,
    DISPLAY_MODE_LASER = 2,
    DISPLAY_MODE_SETTINGS = 3
} display_mode_t;

static display_mode_t current_display_mode = DISPLAY_MODE_ANGLE;
static display_mode_t previous_main_mode = DISPLAY_MODE_ANGLE; // 记录进入settings前的模组
static bool mode_switching = false;
// 已移除：未使用的触摸防抖变量
// static uint32_t last_touch_time = 0;
// static const uint32_t TOUCH_DEBOUNCE_MS = 500;

// 长按深度休眠状态管理
static bool long_press_active = false;
static uint32_t long_press_start_time = 0;
static uint16_t long_press_start_x = 0;
static uint16_t long_press_start_y = 0;

// 滑动检测状态管理
static bool swipe_active = false;
static uint16_t swipe_start_x = 0;
static uint16_t swipe_start_y = 0;
static uint16_t swipe_last_x = 0;  // 滑动过程中的最后位置
static uint16_t swipe_last_y = 0;  // 滑动过程中的最后位置
static uint32_t swipe_start_time = 0;

// 设置界面底部提示矩形状态管理
static bool bottom_touch_hint_active = false;  // 跟踪是否已在底部区域触摸过
#define SWIPE_MIN_DISTANCE 60    // 最小滑动距离（像素）从80减少到60
#define SWIPE_MAX_TIME_MS 800    // 最大滑动时间（毫秒）
#define SWIPE_MIN_TIME_MS 100    // 最小滑动时间（毫秒）避免误触
#define SWIPE_MAX_Y_DEVIATION 160 // X轴最大偏移（像素）允许更倾斜的滑动，从80px增加到160px

// Settings模式的电压读取控制
static uint32_t last_voltage_update = 0;
static const uint32_t VOLTAGE_UPDATE_INTERVAL_MS = 1000; // 1秒更新一次电压

// 左下角触摸区域定义 (140×140px)
// 坐标转换已验证正常，现在设置为真正的左下角
#define TOUCH_AREA_SIZE 140
#define TOUCH_AREA_X1 0
#define TOUCH_AREA_Y1 (EXAMPLE_LCD_V_RES - TOUCH_AREA_SIZE)  // 456 - 140 = 316
#define TOUCH_AREA_X2 (TOUCH_AREA_X1 + TOUCH_AREA_SIZE - 1)  // 139
#define TOUCH_AREA_Y2 (EXAMPLE_LCD_V_RES - 1)  // 455

// 右上角深度休眠区域定义 (140×140px)
#define SLEEP_AREA_SIZE 140
#define SLEEP_AREA_X1 (EXAMPLE_LCD_H_RES - SLEEP_AREA_SIZE)  // 280 - 140 = 140
#define SLEEP_AREA_Y1 0
#define SLEEP_AREA_X2 (EXAMPLE_LCD_H_RES - 1)  // 279
#define SLEEP_AREA_Y2 (SLEEP_AREA_SIZE - 1)    // 139

// 长按检测参数
#define LONG_PRESS_DURATION_MS 1000  // 1秒长按
#define TOUCH_POLL_INTERVAL_MS 20    // 20ms轮询间隔 (从100ms优化为20ms，提高滑动检测精度)

// LVGL初始化就绪标志 - 防止冷启动时的竞态条件
static bool lvgl_ready = false;

lv_ui guider_ui;
TaskHandle_t pxBleTask;
TaskHandle_t pxWifiTask;
EventGroupHandle_t TaskEven;
void lv_stop_roll(lv_obj_t *obj,uint8_t value,uint8_t mode);
void user_gui_screen(lv_ui *ui);
void user_app_init(lv_ui *ui);
void example_app_task(void *pro);
void esp_wifi_scan_w(void *arg);
void esp_ble_scan_w(void *arg);
void esp_wifi_ble_setscan(uint8_t mode);
void color_user(void *arg);
//void example_ble_scan_w(void *arg);
void lv_clear_list(lv_obj_t *obj,uint8_t value);
ClockModule clock_iniput;
void SetTheClock_start(lv_ui *ui);
void out_time(ClockModule * clock);
extern void setBrightnes(uint8_t brig);
extern uint8_t getBrightness(void);

// 触摸切换相关函数声明
static void transform_touch_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y);
static bool is_touch_in_sleep_area(uint16_t x, uint16_t y);
static void deep_sleep_handler(void);
static void touch_monitor_task(void *arg);

// 倒计时功能相关声明
static void countdown_screen_show(void);
static void countdown_screen_hide(void);
static void countdown_update_display(int seconds);
static lv_obj_t *countdown_screen = NULL;
static lv_obj_t *countdown_label = NULL;
static lv_obj_t *countdown_hint_label = NULL;

/*事件*/
static void screen_btn_event_handler (lv_event_t *e);

// 触摸防抖检查 (已弃用 - 滑动切换不需要防抖)
/*
static bool is_touch_valid(void) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_touch_time < TOUCH_DEBOUNCE_MS) {
        return false;
    }
    last_touch_time = now;
    return true;
}
*/

// 触摸坐标转换函数 - 根据实际硬件测试调整映射
static void transform_touch_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y) {
    // 根据测试数据分析：
    // 触摸左下角时得到 raw_x=449-455, raw_y=1-21
    // 期望的屏幕左下角坐标应该是 screen_x=0-139, screen_y=316-455
    // 
    // 重新分析坐标系变换：
    // 看起来触摸坐标系是旋转了90度，并且原点可能在不同位置
    // 尝试新的映射：raw_y -> screen_x, (456-raw_x) -> screen_y
    
    *screen_x = raw_y;  // Y轴成为X轴 (1-21 -> 1-21，这个映射看起来正确)
    *screen_y = raw_x;  // X轴成为Y轴，但不翻转 (449-455 -> 449-455，这应该映射到底部)
    
    // 边界检查
    if (*screen_x >= EXAMPLE_LCD_H_RES) *screen_x = EXAMPLE_LCD_H_RES - 1;
    if (*screen_y >= EXAMPLE_LCD_V_RES) *screen_y = EXAMPLE_LCD_V_RES - 1;
}

// 检查触摸是否在左下角切换区域内（已弃用 - 改为滑动切换）
/*
static bool is_touch_in_switch_area(uint16_t x, uint16_t y) {
    printf("🔍 COORDINATE DEBUG: screen(%d,%d) vs switch_area(x=%d-%d, y=%d-%d)\n", 
           x, y, TOUCH_AREA_X1, TOUCH_AREA_X2, TOUCH_AREA_Y1, TOUCH_AREA_Y2);
    
    // 额外的区域分析 - 帮助理解坐标系
    const char* region = "unknown";
    if (x < EXAMPLE_LCD_H_RES/2 && y < EXAMPLE_LCD_V_RES/2) {
        region = "左上角";
    } else if (x >= EXAMPLE_LCD_H_RES/2 && y < EXAMPLE_LCD_V_RES/2) {
        region = "右上角";  
    } else if (x < EXAMPLE_LCD_H_RES/2 && y >= EXAMPLE_LCD_V_RES/2) {
        region = "左下角";
    } else {
        region = "右下角";
    }
    printf("🎯 触摸区域分析: %s (屏幕四等分)\n", region);
    
    return (x <= TOUCH_AREA_X2 && 
            y >= TOUCH_AREA_Y1 && y <= TOUCH_AREA_Y2);
}
*/

// 滑动方向枚举
typedef enum {
    SWIPE_NONE,
    SWIPE_LEFT,   // 向左滑 - 切换到下一个主模组
    SWIPE_RIGHT,  // 向右滑 - 切换到上一个主模组
    SWIPE_DOWN,   // 向下滑 - 进入settings模组
    SWIPE_UP      // 向上滑 - 退出settings模组
} swipe_direction_t;

// 检测滑动方向
static swipe_direction_t detect_swipe(uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y, uint32_t duration_ms, bool allow_up_swipe) {
    int16_t dx = end_x - start_x;  // X轴位移
    int16_t dy = end_y - start_y;  // Y轴位移
    uint16_t abs_dx = (dx >= 0) ? dx : -dx;  // X轴绝对位移
    uint16_t abs_dy = (dy >= 0) ? dy : -dy;  // Y轴绝对位移
    
    // 检查时间范围 - 但对从底部开始的上滑（退出settings）不做时间限制
    bool is_settings_exit_swipe = allow_up_swipe && (dy < 0);  // 从底部开始的上滑
    
    if (!is_settings_exit_swipe) {
        // 对非settings退出的滑动保持原有时间限制
        if (duration_ms < SWIPE_MIN_TIME_MS || duration_ms > SWIPE_MAX_TIME_MS) {
            printf("⏱️ SWIPE: Invalid duration (%dms), range: %d-%dms\n", 
                   (int)duration_ms, SWIPE_MIN_TIME_MS, SWIPE_MAX_TIME_MS);
            return SWIPE_NONE;
        }
    } else {
        printf("⏱️ SWIPE: Settings exit swipe - time limit bypassed (duration: %dms)\n", (int)duration_ms);
    }
    
    // 判断是水平滑动还是垂直滑动
    if (abs_dx > abs_dy) {
        // 水平滑动 - 检查X轴位移是否足够
        if (abs_dx < SWIPE_MIN_DISTANCE) {
            return SWIPE_NONE;
        }
        
        // 检查Y轴偏移是否在允许范围内
        if (abs_dy > SWIPE_MAX_Y_DEVIATION) {
            printf("📐 SWIPE: Excessive Y deviation (%d > %d) for horizontal swipe\n", abs_dy, SWIPE_MAX_Y_DEVIATION);
            return SWIPE_NONE;
        }
        
        // 确定水平滑动方向
        if (dx > 0) {
            printf("➡️ SWIPE: Detected RIGHT swipe (previous main module)\n");
            return SWIPE_RIGHT;
        } else {
            printf("⬅️ SWIPE: Detected LEFT swipe (next main module)\n");
            return SWIPE_LEFT;
        }
    } else {
        // 垂直滑动 - 根据方向使用不同的最小距离要求
        bool is_up_swipe = (dy < 0);
        uint16_t min_distance_required = is_up_swipe ? 40 : SWIPE_MIN_DISTANCE;  // 上滑只需40px，下滑仍需60px
        
        if (abs_dy < min_distance_required) {
            return SWIPE_NONE;
        }
        
        // 检查X轴偏移是否在允许范围内（复用SWIPE_MAX_Y_DEVIATION作为X轴偏移限制）
        if (abs_dx > SWIPE_MAX_Y_DEVIATION) {
            printf("📐 SWIPE: Excessive X deviation (%d > %d) for vertical swipe\n", abs_dx, SWIPE_MAX_Y_DEVIATION);
            return SWIPE_NONE;
        }
        
        // 确定垂直滑动方向
        if (dy > 0) {
            printf("⬇️ SWIPE: Detected DOWN swipe (enter settings)\n");
            return SWIPE_DOWN;
        } else {
            // 上滑检测 - 需要检查是否允许
            if (!allow_up_swipe) {
                printf("🚫 SWIPE: UP swipe not allowed (not started from bottom area)\n");
                return SWIPE_NONE;
            }
            printf("⬆️ SWIPE: Detected UP swipe (exit settings) - started from bottom\n");
            return SWIPE_UP;
        }
    }
}

// 根据滑动方向切换组件
static void handle_swipe_switch(swipe_direction_t direction) {
    if (direction == SWIPE_NONE) return;
    
    display_mode_t new_mode = current_display_mode;
    ui_state_t target_state;
    
    switch (direction) {
        case SWIPE_LEFT:
            // 向左滑 - 下一个主模组 (仅限 Angle, Level, Laser)
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("🚫 SWIPE LEFT: Settings mode doesn't support horizontal switching\n");
                return;
            }
            
            switch (current_display_mode) {
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_LEVEL; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_ANGLE; break;
                default: return;
            }
            printf("🔄 SWIPE LEFT: %d -> %d (next main module)\n", current_display_mode, new_mode);
            break;
            
        case SWIPE_RIGHT:
            // 向右滑 - 上一个主模组 (仅限 Angle, Level, Laser)
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("🚫 SWIPE RIGHT: Settings mode doesn't support horizontal switching\n");
                return;
            }
            
            switch (current_display_mode) {
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_ANGLE; break;
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_LEVEL; break;
                default: return;
            }
            printf("🔄 SWIPE RIGHT: %d -> %d (previous main module)\n", current_display_mode, new_mode);
            break;
            
        case SWIPE_DOWN:
            // 向下滑 - 进入Settings模组
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("🚫 SWIPE DOWN: Already in Settings mode\n");
                return;
            }
            
            // 记录当前主模组状态
            previous_main_mode = current_display_mode;
            new_mode = DISPLAY_MODE_SETTINGS;
            printf("🔄 SWIPE DOWN: %d -> Settings (saved previous: %d)\n", current_display_mode, previous_main_mode);
            break;
            
        case SWIPE_UP:
            // 向上滑 - 退出Settings模组，回到上一次的主模组
            if (current_display_mode != DISPLAY_MODE_SETTINGS) {
                printf("🚫 SWIPE UP: Not in Settings mode\n");
                return;
            }
            
            new_mode = previous_main_mode;
            printf("🔄 SWIPE UP: Settings -> %d (restored previous module)\n", new_mode);
            break;
            
        default:
            return;
    }
    
    if (new_mode != current_display_mode) {
        // 映射显示模式到UI状态
        switch (new_mode) {
            case DISPLAY_MODE_ANGLE:    target_state = UI_STATE_ANGLE; break;
            case DISPLAY_MODE_LEVEL:    target_state = UI_STATE_LEVEL; break;
            case DISPLAY_MODE_LASER:    target_state = UI_STATE_LASER; break;
            case DISPLAY_MODE_SETTINGS: target_state = UI_STATE_SETTINGS; break;
            default: return;
        }
        
        esp_err_t ret = ui_state_request_transition(target_state, 5000); // 5秒超时
        if (ret == ESP_OK) {
            printf("✅ SWIPE: UI transition requested to state %d\n", target_state);
        } else {
            printf("❌ SWIPE: UI transition failed: %s\n", esp_err_to_name(ret));
        }
    }
}

// 检查触摸是否在右上角深度休眠区域内
static bool is_touch_in_sleep_area(uint16_t x, uint16_t y) {
    // 移除冗余的 y >= SLEEP_AREA_Y1 检查，因为 SLEEP_AREA_Y1 = 0 且 y 是 uint16_t
    return (x >= SLEEP_AREA_X1 && x <= SLEEP_AREA_X2 && 
            y <= SLEEP_AREA_Y2);
}

// 使用新状态管理器的安全显示模式切换 (已弃用 - 改为滑动切换)
/*
static esp_err_t switch_display_mode_safe(void) {
    // 检查是否正在转换中
    if (ui_state_is_transitioning()) {
        printf("switch_display_mode_safe: UI state manager is transitioning, ignoring\n");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取当前状态
    ui_state_t current_ui_state = ui_state_get_current();
    ui_state_t target_ui_state;
    
    printf("switch_display_mode_safe: current UI state is %s\n", ui_state_get_name(current_ui_state));
    
    // 确定目标状态
    switch (current_ui_state) {
        case UI_STATE_ANGLE:
            target_ui_state = UI_STATE_LEVEL;
            break;
        case UI_STATE_LEVEL:
            target_ui_state = UI_STATE_LASER;
            break;
        case UI_STATE_LASER:
            target_ui_state = UI_STATE_SETTINGS;
            break;
        case UI_STATE_SETTINGS:
            target_ui_state = UI_STATE_ANGLE;
            break;
        default:
            ESP_LOGE("user_app", "Unknown UI state: %d", current_ui_state);
            return ESP_ERR_INVALID_STATE;
    }
    
    // 请求状态转换
    esp_err_t ret = ui_state_request_transition(target_ui_state, 5000);  // 5秒超时
    if (ret != ESP_OK) {
        ESP_LOGE("user_app", "Failed to request UI transition: %s", esp_err_to_name(ret));
        return ret;
    }
    
    printf("switch_display_mode_safe: requested transition to %s\n", ui_state_get_name(target_ui_state));
    return ESP_OK;
}
*/

// UI状态变化回调函数
static void on_ui_state_changed(ui_state_t old_state, ui_state_t new_state) {
    ESP_LOGI("user_app", "UI state changed: %s -> %s", 
             ui_state_get_name(old_state), ui_state_get_name(new_state));
    
    // 更新previous_main_mode逻辑
    display_mode_t old_mode = current_display_mode;
    
    // 更新本地状态以保持兼容性
    switch (new_state) {
        case UI_STATE_ANGLE:
            current_display_mode = DISPLAY_MODE_ANGLE;
            break;
        case UI_STATE_LEVEL:
            current_display_mode = DISPLAY_MODE_LEVEL;
            break;
        case UI_STATE_LASER:
            current_display_mode = DISPLAY_MODE_LASER;
            break;
        case UI_STATE_SETTINGS:
            current_display_mode = DISPLAY_MODE_SETTINGS;
            break;
        default:
            ESP_LOGW("user_app", "Unknown UI state: %d", new_state);
            break;
    }
    
    // 管理previous_main_mode：如果是从主模组切换到Settings，更新previous_main_mode
    if (current_display_mode == DISPLAY_MODE_SETTINGS && 
        old_mode != DISPLAY_MODE_SETTINGS &&
        (old_mode == DISPLAY_MODE_ANGLE || old_mode == DISPLAY_MODE_LEVEL || old_mode == DISPLAY_MODE_LASER)) {
        previous_main_mode = old_mode;
        printf("🔄 SAVED previous main mode: %d before entering Settings\n", previous_main_mode);
    }
    
    mode_switching = false;  // 重置切换标志
}

// 触摸事件处理器 (已弃用 - 改为滑动切换)
/*
static void display_mode_touch_handler(uint16_t x, uint16_t y) {
    if (!is_touch_valid()) {
        return; // 防抖过滤
    }
    
    // 检查是否在左下角切换区域
    if (!is_touch_in_switch_area(x, y)) {
        printf("Touch outside switch area, ignoring\n");
        return;
    }
    
    printf("display_mode_touch_handler: valid touch in switch area detected\n");
    switch_display_mode_safe();
}
*/

// 深度休眠处理函数
static void deep_sleep_handler(void) {
    printf("🌙 DEEP SLEEP: Preparing for deep sleep with countdown...\n");
    
    // 显示倒计时屏幕
    countdown_screen_show();
    
    // 倒计时3秒，但每个数字显示时间缩短50% (0.5秒每个数字，总共1.5秒)
    for (int i = 3; i > 0; i--) {
        countdown_update_display(i);
        printf("🌙 COUNTDOWN: %d seconds remaining\n", i);
        vTaskDelay(pdMS_TO_TICKS(500));  // 等待0.5秒 (缩短50%)
        
        // 在倒计时期间检查是否取消（通过触摸释放）
        uint16_t raw_x, raw_y;
        if (!getTouch(&raw_x, &raw_y)) {
            printf("🌙 COUNTDOWN: Cancelled - touch released during countdown\n");
            countdown_screen_hide();
            return;  // 取消深睡眠
        }
    }
    
    // 倒计时结束，不隐藏倒计时屏幕，直接进入深度休眠
    // 这样屏幕会保持倒计时状态，避免露出底层UI
    
    // 关闭屏幕背光 - 使用专门的关闭函数避免触发NVS保存
    printf("🌙 DEEP SLEEP: Turning off display...\n");
    shutdownDisplay();
    
    // 延迟确保屏幕关闭和所有清理操作完成
    vTaskDelay(pdMS_TO_TICKS(200));
    
    printf("🌙 DEEP SLEEP: Using RESET button wake-up strategy\n");
    printf("🌙 DEEP SLEEP: No automatic wake-up timer - stable deep sleep mode\n");
    printf("🌙 DEEP SLEEP: To wake up: Press RESET button to restart the device\n");
    printf("🌙 DEEP SLEEP: This avoids GPIO0 conflicts and ensures maximum power saving\n");
    
    // 进入深度休眠 - 不配置任何唤醒源，确保最稳定的深度睡眠
    printf("🌙 DEEP SLEEP: Entering deep sleep mode...\n");
    
    // 确保所有输出都被刷新
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_deep_sleep_start();
}

// 电池保护关机前的回调函数
static void battery_protection_shutdown_callback(void) {
    ESP_LOGW("user_app", "🔋 BATTERY PROTECTION: Shutdown callback triggered");
    
    // 停止所有任务和定时器
    printf("🔋 BATTERY PROTECTION: Stopping all tasks and timers...\n");
    
    // 停止AMOLED防烧屏保护
    extern esp_err_t amoled_burn_protection_stop(void);
    amoled_burn_protection_stop();
    
    // 其他清理工作...
    printf("🔋 BATTERY PROTECTION: Cleanup completed\n");
}

static void angle_update_task(void *arg) {
    float pitch = 0, roll = 0, yaw = 0;
    // 将当前任务添加到看门狗
    esp_task_wdt_add(NULL);
    
    // 等待LVGL完全就绪，防止冷启动竞态条件
    while (!lvgl_ready) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("angle_update_task: LVGL ready, starting angle updates\n");
    
    while (1) {
        // 无论何时都要重置看门狗，避免因为mode_switching导致的重启
        esp_task_wdt_reset();
        
        angle_calc_update();
        angle_calc_get(&pitch, &roll, &yaw);
        
        // 根据当前显示模式更新相应的显示
        if (!mode_switching) { // 只在非切换状态下更新显示
            // 获取当前UI状态，避免在转换期间更新已删除的UI
            ui_state_t current_ui_state = ui_state_get_current();
            
            // 检查UI状态管理器是否正在转换中
            if (ui_state_is_transitioning()) {
                // 转换期间不更新任何UI，避免访问已删除的对象
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            
            if (current_ui_state == UI_STATE_ANGLE && current_display_mode == DISPLAY_MODE_ANGLE) {
                angle_display_update(pitch, roll);
            } else if (current_ui_state == UI_STATE_LEVEL && current_display_mode == DISPLAY_MODE_LEVEL) {
                level_display_update(roll); // level display只需要roll角度
            } else if (current_ui_state == UI_STATE_LASER && current_display_mode == DISPLAY_MODE_LASER) {
                // laser模式：不需要周期性更新，距离只在测量时显示
            } else if (current_ui_state == UI_STATE_SETTINGS && current_display_mode == DISPLAY_MODE_SETTINGS) {
                // settings模式：每1秒读取一次电池电压，避免频繁ADC读取
                uint32_t now = esp_timer_get_time() / 1000; // 转换为毫秒
                if (now - last_voltage_update >= VOLTAGE_UPDATE_INTERVAL_MS) {
                    last_voltage_update = now;
                    float battery_voltage;
                    adc_get_value(&battery_voltage);
                    settings_ui_update_battery_info(battery_voltage); // 同时更新电压和百分比
                    
                    // 同时更新亮度显示
                    uint8_t current_brightness = getBrightness();
                    settings_ui_update_brightness(current_brightness);
                }
            }
        }
        
        // 主动让出CPU时间
        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(50)); // 恢复20Hz刷新频率
    }
}

// 触摸监控任务
static void touch_monitor_task(void *arg) {
    uint16_t raw_x, raw_y;
    uint16_t screen_x, screen_y;
    bool touch_detected = false;
    uint32_t current_time = 0;
    
    printf("touch_monitor_task: started\n");
    
    // 添加当前任务到看门狗
    esp_task_wdt_add(NULL);
    printf("touch_monitor_task: added to watchdog monitoring\n");
    
    // 等待LVGL完全就绪，防止冷启动竞态条件
    while (!lvgl_ready) {
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset(); // 防止看门狗超时
    }
    printf("touch_monitor_task: LVGL ready, starting touch monitoring\n");
    
    while (1) {
        current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        touch_detected = getTouch(&raw_x, &raw_y);
        
        // 处理激光模式的长按测量功能
        if (ui_state_get_current() == UI_STATE_LASER && current_display_mode == DISPLAY_MODE_LASER) {
            // 在激光模式下，处理长按测量
            uint32_t press_duration = touch_detected ? (current_time - (touch_detected ? current_time : current_time)) : 0;
            laser_handle_long_press(touch_detected, press_duration);
        }
        
        if (touch_detected) {
            // 转换触摸坐标到屏幕坐标系
            transform_touch_coordinates(raw_x, raw_y, &screen_x, &screen_y);
            
            // 检查是否在设置界面的底部区域（提示矩形区域）
            if (ui_state_get_current() == UI_STATE_SETTINGS) {
                // 底部20像素高度的区域（全宽度）
                if (screen_y >= EXAMPLE_LCD_V_RES - 20) {
                    // 触摸在底部区域，激活并高亮提示矩形
                    if (!bottom_touch_hint_active) {
                        bottom_touch_hint_active = true;
                        settings_ui_update_swipe_hint(true);
                    }
                }
                // 如果已经激活，无论触摸移动到哪里都保持高亮状态
                // 只有在触摸释放时才会恢复正常状态
            }
            
            // 检查是否在右上角深度休眠区域
            if (is_touch_in_sleep_area(screen_x, screen_y)) {
                // 深度休眠区域的长按检测逻辑保持不变
                if (!long_press_active) {
                    // 开始长按检测
                    long_press_active = true;
                    long_press_start_time = current_time;
                    long_press_start_x = screen_x;
                    long_press_start_y = screen_y;
                    printf("🌙 LONG PRESS: Started at (%d,%d), need %dms\n", 
                           screen_x, screen_y, LONG_PRESS_DURATION_MS);
                } else {
                    // 检查是否仍在同一区域（允许小范围移动）
                    uint16_t dx = (screen_x > long_press_start_x) ? 
                                  (screen_x - long_press_start_x) : (long_press_start_x - screen_x);
                    uint16_t dy = (screen_y > long_press_start_y) ? 
                                  (screen_y - long_press_start_y) : (long_press_start_y - screen_y);
                    
                    if (dx <= 20 && dy <= 20) { // 允许20像素范围内的移动
                        uint32_t elapsed = current_time - long_press_start_time;
                        if (elapsed >= LONG_PRESS_DURATION_MS) {
                            printf("🌙 LONG PRESS: 3 seconds completed! Entering deep sleep...\n");
                            deep_sleep_handler();
                            // 不会执行到这里，因为设备已经进入深度休眠
                        } else {
                            // 显示进度（每500ms打印一次）
                            if (elapsed % 500 == 0) {
                                printf("🌙 LONG PRESS: Progress %dms / %dms\n", 
                                       (int)elapsed, LONG_PRESS_DURATION_MS);
                            }
                        }
                    } else {
                        // 移动超出范围，取消长按
                        printf("🌙 LONG PRESS: Cancelled due to movement (dx=%d, dy=%d)\n", dx, dy);
                        long_press_active = false;
                    }
                }
                
                // 在深度休眠区域时取消滑动检测
                if (swipe_active) {
                    printf("🚀 SWIPE: Cancelled - in sleep area\n");
                    // 重置设置界面的视觉偏移
                    if (ui_state_get_current() == UI_STATE_SETTINGS) {
                        settings_ui_update_swipe_offset(0);
                    }
                    swipe_active = false;
                }
            } else {
                // 不在深度休眠区域，取消长按并处理滑动检测
                if (long_press_active) {
                    printf("🌙 LONG PRESS: Cancelled - moved outside sleep area\n");
                    long_press_active = false;
                }
                
                // 滑动检测逻辑
                if (!swipe_active) {
                    // 开始滑动检测
                    swipe_active = true;
                    swipe_start_x = screen_x;
                    swipe_start_y = screen_y;
                    swipe_last_x = screen_x;  // 初始化最后位置
                    swipe_last_y = screen_y;
                    swipe_start_time = current_time;
                } else {
                    // 继续跟踪滑动，更新最后位置
                    swipe_last_x = screen_x;
                    swipe_last_y = screen_y;
                    
                    // 在设置界面且从底部开始的滑动，添加视觉跟随效果
                    if (ui_state_get_current() == UI_STATE_SETTINGS && bottom_touch_hint_active) {
                        int delta_y = screen_y - swipe_start_y;  // 计算Y轴移动距离
                        
                        // 只有向上滑动才显示视觉效果
                        if (delta_y < 0) {
                            // 实时更新设置界面的偏移（1:1跟随手指移动）
                            settings_ui_update_swipe_offset(delta_y);
                        }
                    }
                }
            }
        } else {
            // 没有触摸，检查是否需要结束滑动或长按
            if (long_press_active) {
                printf("🌙 LONG PRESS: Cancelled - touch released\n");
                long_press_active = false;
            }
            
            if (swipe_active) {
                // 滑动结束，使用最后记录的位置检测滑动方向
                uint32_t swipe_duration = current_time - swipe_start_time;
                
                // 检查是否允许上滑：只有在设置界面且从底部区域开始触摸才允许
                bool allow_up_swipe = (ui_state_get_current() == UI_STATE_SETTINGS) && bottom_touch_hint_active;
                printf("🔍 SWIPE DEBUG: UI_STATE=%d, bottom_hint_active=%d, allow_up_swipe=%d\n", 
                       ui_state_get_current(), bottom_touch_hint_active, allow_up_swipe);
                
                swipe_direction_t direction = detect_swipe(swipe_start_x, swipe_start_y, 
                                                          swipe_last_x, swipe_last_y, swipe_duration, allow_up_swipe);
                
                if (direction != SWIPE_NONE) {
                    printf("🚀 SWIPE: Completed - processing direction %d\n", direction);
                    handle_swipe_switch(direction);
                }
                
                // 滑动结束，重置设置界面的视觉偏移
                if (ui_state_get_current() == UI_STATE_SETTINGS) {
                    settings_ui_update_swipe_offset(0);  // 重置到原始位置
                }
                
                swipe_active = false;
            }
            
            // 触摸释放时，如果在设置界面，恢复提示矩形正常状态
            // 注意：这个重置要在滑动检测之后，避免影响allow_up_swipe判断
            if (ui_state_get_current() == UI_STATE_SETTINGS && bottom_touch_hint_active) {
                bottom_touch_hint_active = false;
                settings_ui_update_swipe_hint(false);
            }
        }
        
        // 重置看门狗定时器，防止重启
        esp_task_wdt_reset();
        
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_INTERVAL_MS));
    }
}

#ifdef LASER_TEST_MODE
// 激光测试任务
static void laser_test_task(void *arg) {
    float test_distance = 1.0f;
    int direction = 1;  // 1 为增加，-1 为减少
    
    printf("Laser test task started\n");
    
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
#endif

// 传感器后台初始化任务
static void sensor_init_task(void *pvParameters)
{
    ESP_LOGI("sensor_init", "Starting sensor initialization in background...");
    
    // 在后台完成传感器校准（包含2.2秒延迟）
    angle_calc_init(0.05f, 0.3f); // 50ms, beta=0.3
    angle_calc_set_pitch_lpf_alpha(0.2f); // 设置pitch低通滤波系数为0.2，更平滑
    angle_calc_set_roll_lpf_alpha(0.15f);  // 设置roll低通滤波系数为0.15，比pitch更平滑以减少视觉抖动
    
    ESP_LOGI("sensor_init", "Sensor initialization completed");
    
    // 创建角度更新任务
    xTaskCreate(angle_update_task, "angle_update_task", 4096, NULL, 3, NULL); // 优先级降低到3，低于LVGL任务(5)
    
    // 自删除任务
    vTaskDelete(NULL);
}

void user_top_init(void)
{
#ifdef LASER_TEST_MODE
  // 激光测试模式
  printf("Starting in LASER TEST MODE\n");
  lv_obj_t *parent = lv_scr_act();
  laser_ui_init(parent, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
  laser_ui_set_status_text("Laser Test Mode");
  laser_ui_update_distance(1.5f);
  
  // 创建激光测试任务
  xTaskCreate(laser_test_task, "laser_test", 4096, NULL, 3, NULL);
  printf("Laser test mode initialized\n");
#else
  // 正常双显示模式
  lv_obj_t *parent = lv_scr_act();
  // 恢复默认启动angle_display模式
  angle_display_init(parent, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
  
  // 给LVGL一些时间完成初始化，防止竞态条件（优化：减少延迟）
  vTaskDelay(pdMS_TO_TICKS(10));
  
  // UI初始化完成后，从NVS恢复保存的亮度设置
  uint8_t saved_brightness = loadSavedBrightness();
  setBrightnes(saved_brightness);  // 恢复上次保存的亮度
  printf("Restored brightness from NVS: %d (%.1f%%)\n", saved_brightness, (saved_brightness * 100.0f) / 255.0f);
  
  lvgl_ready = true; // 标记LVGL已准备就绪
  printf("user_top_init: LVGL marked as ready\n");
  
  // 注册UI状态变化回调
  esp_err_t callback_ret = ui_state_register_callback(on_ui_state_changed);
  if (callback_ret != ESP_OK) {
      ESP_LOGW("user_app", "Failed to register UI state callback: %s", esp_err_to_name(callback_ret));
  } else {
      ESP_LOGI("user_app", "UI state callback registered successfully");
  }
  
  // 将传感器初始化移到后台任务，避免阻塞UI显示
  xTaskCreate(sensor_init_task, "sensor_init", 8192, NULL, 4, NULL); // 优先级4，适中
  xTaskCreate(touch_monitor_task, "touch_monitor_task", 8192, NULL, 2, NULL); // 增加触摸监控任务栈大小防止溢出
#endif
}
void user_gui_screen(lv_ui *ui)
{
  lv_obj_add_event_cb(ui->screen_btn_1, screen_btn_event_handler, LV_EVENT_ALL, ui);    //事件
  lv_obj_add_event_cb(ui->screen_btn_2, screen_btn_event_handler, LV_EVENT_ALL, ui);    //事件
  lv_obj_add_event_cb(ui->screen_imgbtn_1, screen_btn_event_handler, LV_EVENT_ALL, ui); //事件
  lv_obj_add_event_cb(ui->screen_imgbtn_2, screen_btn_event_handler, LV_EVENT_ALL, ui); //事件
  lv_obj_add_event_cb(ui->screen_btn_3, screen_btn_event_handler, LV_EVENT_ALL, ui); //事件
  lv_obj_add_event_cb(ui->screen_btn_4, screen_btn_event_handler, LV_EVENT_ALL, ui); //事件
  lv_obj_add_event_cb(ui->screen_slider_1, screen_btn_event_handler, LV_EVENT_ALL, ui); 
}
void user_app_init(lv_ui *ui)
{
  // 初始化电池保护功能
  esp_err_t ret = battery_protection_init(battery_protection_shutdown_callback);
  if (ret == ESP_OK) {
    // 启动电池保护监控
    ret = battery_protection_start();
    if (ret == ESP_OK) {
      ESP_LOGI("user_app", "🔋 Battery protection started successfully");
    } else {
      ESP_LOGE("user_app", "🔋 Failed to start battery protection: %s", esp_err_to_name(ret));
    }
  } else {
    ESP_LOGE("user_app", "🔋 Failed to initialize battery protection: %s", esp_err_to_name(ret));
  }

  xTaskCreate(example_app_task, "example_app_task", 4096, (void *)ui, 2, NULL); // 增加栈大小避免溢出
  xTaskCreate(esp_wifi_scan_w, "esp_wifi_scan_w", 4096, ui, 2, &pxWifiTask); // 增加栈大小
  xTaskCreate(esp_ble_scan_w, "esp_ble_scan_w", 4096, ui, 2, &pxBleTask); // 增加栈大小
  xTaskCreate(color_user, "color_user", 2048, ui, 6, NULL); // 稍微增加栈大小
}
void clock_task_callback(void *arg)
{
  static uint8_t bat = 0;
  lv_ui *ui = (lv_ui *)arg;
  clock_iniput.out_seconds++;
  int16_t Seconds_ars = clock_iniput.out_seconds * 6 - 90;
  lv_img_set_angle(ui->screen_img_3, Seconds_ars * 10);
  if(clock_iniput.out_seconds == 60)
  {
    clock_iniput.out_seconds = 0;
    clock_iniput.out_minutes++;
    int16_t Minutes_ars = clock_iniput.out_minutes * 6 - 90;
    lv_img_set_angle(ui->screen_img_2, Minutes_ars * 10);
    if( (clock_iniput.out_minutes == 12) || (clock_iniput.out_minutes == 24) || (clock_iniput.out_minutes == 36) || (clock_iniput.out_minutes == 48) || (clock_iniput.out_minutes == 60))
    bat = 1;
    else
    bat = 0;
  }
  if(clock_iniput.out_minutes == 60)
  {
    clock_iniput.minutes = 0;
  }
  if( bat == 1 )
  {
    bat = 0;
    clock_iniput.out_Hours++;
    int16_t Hours_ars = clock_iniput.out_Hours * 6 - 90;
    lv_img_set_angle(ui->screen_img_1, Hours_ars * 10);
  }
  if(clock_iniput.out_Hours == 60)
  {
    clock_iniput.out_Hours = 0;
  }
}
void SetTheClock_start(lv_ui *ui)
{
  const esp_timer_create_args_t clock_tick_timer_args = 
  {
    .callback = &clock_task_callback,
    .name = "clock_task",
    .arg = ui,
  };
  esp_timer_handle_t clock_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&clock_tick_timer_args, &clock_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(clock_tick_timer, 1000 * 1000));              //1s
}
void out_time(ClockModule * clock)
{
  clock->out_Hours = clock->Hours * 5;
  clock->out_minutes = clock->minutes;
  clock->out_seconds = clock->seconds;
  uint8_t bat = clock->out_minutes / 12;
  clock->out_Hours += bat;

  int16_t Hours_ars = clock_iniput.out_Hours * 6 - 90;
  int16_t Minutes_ars = clock_iniput.out_minutes * 6 - 90;
  int16_t Seconds_ars = clock_iniput.out_seconds * 6 - 90;
  lv_img_set_angle(guider_ui.screen_img_1, Hours_ars * 10);
  lv_img_set_angle(guider_ui.screen_img_2, Minutes_ars * 10);
  lv_img_set_angle(guider_ui.screen_img_3, Seconds_ars * 10);
}

void example_app_task(void *pro)
{
  lv_ui *obj = (lv_ui *)pro;
  char adc_values[15] = {0};
  char sd_buff[8] = {0};
  float adc_value;
  float sd_value = 0;
  float acc[3];
  float gyro[3];
  char imu_buf[200] = {""};
  uint32_t stimes = 0;
  uint32_t adc_test = 0;
  uint32_t imu_test = 0;
  sd_value = sd_cadr_get_value();
  if(sd_value != 0)
  {
    sprintf(sd_buff,"%.2fG",sd_value);
    lv_label_set_text(obj->screen_label_7, sd_buff);
  }
  qmi8658_init();
  for(;;)
  {
    if(stimes - adc_test > 0) //1s
    {
      adc_test = stimes;
      adc_get_value(&adc_value);
      if(adc_value)
      {
        sprintf(adc_values,"%.2fV",adc_value);
        lv_label_set_text(obj->screen_label_8, adc_values);
      }
    }
    if(stimes - imu_test > 0) //1s
    {
      imu_test = stimes;
      qmi8658_read_xyz(acc,gyro);
      snprintf(imu_buf,200,"IMU:\n %.2fmg\n %.2fmg\n %.2fmg\n %.2fdps\n %.2fdps\n %.2fdps\n",acc[0],acc[1],acc[2],gyro[0],gyro[1],gyro[2]);
      lv_span_set_text(obj->screen_spangroup_1_span, imu_buf);
      //printf("x:%f y:%f z:%f gyro_x:%f gyro_y:%f gyro_z:%f\n",acc[0],acc[1],acc[2],gyro[0],gyro[1],gyro[2]);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    stimes++;
  }
}



void lv_clear_list(lv_obj_t *obj,uint8_t value) 
{
	for(signed char i = value-1; i>=0; i--)
	{
		lv_obj_t *imte = lv_obj_get_child(obj,i);
		lv_obj_add_flag(imte,LV_OBJ_FLAG_HIDDEN);
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
void lv_stop_roll(lv_obj_t *obj,uint8_t value,uint8_t mode)
{
  if(mode == 1)
  {
    //lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    for(signed char i = value-1; i>=0; i--)
	  {
		  lv_obj_t *imte = lv_obj_get_child(obj,i);
      lv_obj_t *txt = lv_obj_get_child(imte,1);
		  lv_label_set_long_mode(txt, LV_LABEL_LONG_DOT);
    }
	}
  else
  {
    //lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    for(signed char i = value-1; i>=0; i--)
	  {
		  lv_obj_t *imte = lv_obj_get_child(obj,i);
      lv_obj_t *txt = lv_obj_get_child(imte,1);
		  lv_label_set_long_mode(txt, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
  }
}
/*事件*/
static void screen_btn_event_handler (lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_ui *ui = (lv_ui *)e->user_data;
  lv_obj_t * module = e->current_target;
  switch (code)
  {
    case LV_EVENT_CLICKED:
    {
      EventBits_t even = xEventGroupWaitBits(TaskEven,(0x01<<1) | (0x01<<2),pdFALSE,pdFALSE,10);
      if(module == ui->screen_btn_1)
      {
        if( even & (0x01<<1) )
        {
          esp_wifi_ble_setscan(0); //ble
          xEventGroupClearBits( TaskEven,(0x01<<1) );
          ble_scan_setconf();
          xTaskNotifyGive(pxBleTask);
        }
      }
      else if(module == ui->screen_btn_2) //wifi
      {
        if( even & (0x01<<2) )
        {
          esp_wifi_ble_setscan(1); //wifi

          xEventGroupClearBits( TaskEven,(0x01<<2) );
          xTaskNotifyGive(pxWifiTask);
        }
      }
      else if(module == ui->screen_imgbtn_1)
      {
        lv_obj_clear_flag(ui->screen_cont_1,LV_OBJ_FLAG_HIDDEN); //显示
        lv_obj_add_flag(ui->screen_cont_2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->screen_cont_3, LV_OBJ_FLAG_HIDDEN);   
      }
      else if(module == ui->screen_imgbtn_2)
      {
        lv_obj_clear_flag(ui->screen_cont_3,LV_OBJ_FLAG_HIDDEN); //显示
        lv_obj_add_flag(ui->screen_cont_2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->screen_cont_1, LV_OBJ_FLAG_HIDDEN);   
      }
      else if( (module == ui->screen_btn_3) || (module == ui->screen_btn_4) )
      {
        lv_obj_clear_flag(ui->screen_cont_2,LV_OBJ_FLAG_HIDDEN); //显示
        lv_obj_add_flag(ui->screen_cont_3, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->screen_cont_1, LV_OBJ_FLAG_HIDDEN);
      }
      else if( module == ui->screen_slider_1 )
      {
        uint8_t value = lv_slider_get_value(module);
        setBrightnes(value);
      }
      break;
    }
    default:
      break;
  }
}
//wifi :1 ble:0
void esp_wifi_ble_setscan(uint8_t mode)
{
  static uint8_t wifi_ble_flag = 0;
  if(mode != wifi_ble_flag)
  {
    if(mode) //wifi 需要释放ble
    {
      ble_scan_Deinit();
      espwifi_Init();
    }
    else
    {
      espwifi_Deinit();
      ble_scan_Init();
    }
    wifi_ble_flag = mode;
  }
}
void color_user(void *arg)
{
  lv_ui *ui = (lv_ui *)arg;
  lv_obj_clear_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //不可点击
  lv_obj_clear_flag(ui->screen_cont_5,LV_OBJ_FLAG_HIDDEN); //显示
  lv_obj_add_flag(ui->screen_cont_4, LV_OBJ_FLAG_HIDDEN);   
  lv_obj_clear_flag(ui->screen_img_4,LV_OBJ_FLAG_HIDDEN); //显示
  lv_obj_add_flag(ui->screen_img_5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_6, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));
  lv_obj_clear_flag(ui->screen_img_5,LV_OBJ_FLAG_HIDDEN); //显示
  lv_obj_add_flag(ui->screen_img_4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_6, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));
  lv_obj_clear_flag(ui->screen_img_6,LV_OBJ_FLAG_HIDDEN); //显示
  lv_obj_add_flag(ui->screen_img_4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_5, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));
  lv_obj_clear_flag(ui->screen_cont_4,LV_OBJ_FLAG_HIDDEN); //显示
  lv_obj_add_flag(ui->screen_cont_5, LV_OBJ_FLAG_HIDDEN);  
  lv_obj_add_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //可点击
  vTaskDelete(NULL); //删除任务
}
void esp_wifi_scan_w(void *arg)
{
  lv_ui *wifi_obj = (lv_ui *)arg;
	static wifi_ap_record_t recdata;
  static uint16_t rec = 0;
	static const char *imgbox = NULL;
	static lv_obj_t *imte;
	static lv_obj_t *label;
  for(;;)
  {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);                                      //等待任务通知
    lv_stop_roll(wifi_obj->screen_list_2,rec,1);
		lv_clear_list(wifi_obj->screen_list_2,rec);
    lv_stop_roll(wifi_obj->screen_list_2,rec,0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_start(NULL,true));               //扫描可用AP
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&rec));
    if(rec != 0)
    {
			if(rec > 20)
			{
				rec = 21;
				for(uint8_t i = 0; i<rec; i++)
      	{
      	  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_record(&recdata));
					imgbox = (const char*)(&(recdata.ssid[0]));
					if (imgbox == NULL)
					break;
					imte = lv_obj_get_child(wifi_obj->screen_list_2,i);
					if (imte != NULL)
					{
						label = lv_obj_get_child(imte,1);
						if(label != NULL)
            {
    				 lv_label_set_text(label,imgbox);
							lv_obj_clear_flag(imte,LV_OBJ_FLAG_HIDDEN);    //显示
						}
					}
					imgbox = NULL;
					imte = NULL;
					label = NULL;
					vTaskDelay(pdMS_TO_TICKS(300));
      	}
				esp_wifi_clear_ap_list();
			}
			else
			{
      	for(uint8_t i = 0; i<rec; i++)
      	{
      	  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_record(&recdata));
					imgbox = (const char*)(&(recdata.ssid[0]));
					if (imgbox == NULL)
					break;
					imte = lv_obj_get_child(wifi_obj->screen_list_2,i); //获取子对象
					if (imte != NULL)
					{
						label = lv_obj_get_child(imte,1); //获取子子对象
						if(label != NULL)
            {
    				 lv_label_set_text(label,imgbox);
							lv_obj_clear_flag(imte,LV_OBJ_FLAG_HIDDEN);    //显示
						}
					}
					imgbox = NULL;
					imte = NULL;
					label = NULL;
					vTaskDelay(pdMS_TO_TICKS(100));
      	}
			}
    }
    xEventGroupSetBits( TaskEven,(0x01<<2) );
  }
}
void esp_ble_scan_w(void *arg)
{
  lv_ui *wifi_obj = (lv_ui *)arg;
  static uint16_t rec = 0;
  uint8_t mac[6];
  static lv_obj_t *imte;
	static lv_obj_t *label;
  char imgbox[24] = {0};
  for(;;)
  {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    lv_stop_roll(wifi_obj->screen_list_1,rec,1);     
    lv_clear_list(wifi_obj->screen_list_1,rec);
    lv_stop_roll(wifi_obj->screen_list_1,rec,0);
    rec = 0;
    for(;xQueueReceive(ble_Queue,mac,3500) == pdTRUE;)
    {
      imte = lv_obj_get_child(wifi_obj->screen_list_1,rec);
      if (imte != NULL)
			{
				label = lv_obj_get_child(imte,1);
				if(label != NULL)
        {
          sprintf(imgbox,"%d:%d:%d:%d:%d:%d",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    		 lv_label_set_text(label,imgbox);
					lv_obj_clear_flag(imte,LV_OBJ_FLAG_HIDDEN);    //显示
				}
			}
			imte = NULL;
			label = NULL;
      rec++;
      vTaskDelay(pdMS_TO_TICKS(100));
      if(rec == 21)
      break;
    }
    xEventGroupSetBits( TaskEven,(0x01<<1) );
  }
}

// ===== 倒计时UI实现 (方案4：临时替换显示内容) =====

// 显示倒计时屏幕
static void countdown_screen_show(void) {
    if (countdown_screen != NULL) {
        printf("countdown_screen_show: screen already exists\n");
        return;
    }
    
    printf("countdown_screen_show: creating countdown screen\n");
    
    // 创建全屏遮罩层
    countdown_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(countdown_screen, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    lv_obj_align(countdown_screen, LV_ALIGN_CENTER, 0, 0);
    
    // 设置样式 - 半透明黑色背景
    lv_obj_set_style_bg_opa(countdown_screen, 200, LV_PART_MAIN);  // 78% 不透明度
    lv_obj_set_style_bg_color(countdown_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(countdown_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(countdown_screen, 0, LV_PART_MAIN);
    
    // 创建倒计时数字标签 (大号字体)
    countdown_label = lv_label_create(countdown_screen);
    lv_label_set_text(countdown_label, "3");
    lv_obj_set_style_text_color(countdown_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(countdown_label, &lv_font_montserrat_48, 0);  // 大号字体
    lv_obj_align(countdown_label, LV_ALIGN_CENTER, 0, -40);  // 稍微上移
    
    // 创建提示文字标签
    countdown_hint_label = lv_label_create(countdown_screen);
    lv_label_set_text(countdown_hint_label, "Entering sleep mode...\nRelease to cancel");
    lv_obj_set_style_text_color(countdown_hint_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(countdown_hint_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(countdown_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(countdown_hint_label, LV_ALIGN_CENTER, 0, 60);  // 稍微下移
    
    printf("countdown_screen_show: countdown screen created successfully\n");
}

// 隐藏倒计时屏幕
static void countdown_screen_hide(void) {
    if (countdown_screen != NULL) {
        printf("countdown_screen_hide: removing countdown screen\n");
        
        // 获取LVGL锁保护清理过程
        if (example_lvgl_lock(1000)) {
            // 验证对象有效性
            if (lv_obj_is_valid(countdown_screen)) {
                lv_obj_del(countdown_screen);
            }
            countdown_screen = NULL;
            countdown_label = NULL;
            countdown_hint_label = NULL;
            
            // 强制刷新显示确保清理完成
            lv_refr_now(NULL);
            example_lvgl_unlock();
            
            printf("countdown_screen_hide: cleanup completed\n");
        } else {
            printf("countdown_screen_hide: failed to acquire LVGL lock\n");
            // 即使获取锁失败，也要清除指针避免野指针
            countdown_screen = NULL;
            countdown_label = NULL;
            countdown_hint_label = NULL;
        }
        
        // 给系统时间完成清理
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// 更新倒计时显示
static void countdown_update_display(int seconds) {
    if (countdown_label != NULL) {
        char countdown_text[8];
        snprintf(countdown_text, sizeof(countdown_text), "%d", seconds);
        lv_label_set_text(countdown_label, countdown_text);
        printf("countdown_update_display: updated to %d seconds\n", seconds);
    }
}
