#include <stdio.h>
#include "user_app.h"
#include "sd_card_bsp.h"
#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"
#include "esp_timer.h"
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
#include "idle_sleep.h"

// 外部函数声明
extern void setBrightnes(uint8_t brig);  // LCD亮度控制函数
extern uint8_t loadSavedBrightness(void);  // NVS亮度加载函数

// 编译时选择启动模式
// #define LASER_TEST_MODE  // 取消注释以启用激光测试模式
#include "level_display.h"
#include "action_display.h"
#include "main.h"  // 包含LVGL锁函数和shutdownDisplay函数声明
#include "esp_task_wdt.h"
#include "touch_bsp.h"
#include "ui_state_manager.h" // 添加UI状态管理器
#include "amoled_burn_protection.h"

#define EXAMPLE_LCD_H_RES 280
#define EXAMPLE_LCD_V_RES 456

/* 0 = 隐藏全屏色块水平仪入口（保留模块代码，便于日后恢复）
 * 1 = 重新启用 Level 滑动入口 */
#ifndef FEATURE_UI_LEVEL
#define FEATURE_UI_LEVEL  0
#endif

// 显示模式管理
typedef enum {
    DISPLAY_MODE_ANGLE = 0,
    DISPLAY_MODE_LEVEL = 1,
    DISPLAY_MODE_LASER = 2,
    DISPLAY_MODE_ACTION = 3,
    DISPLAY_MODE_SETTINGS = 4
} display_mode_t;

static display_mode_t current_display_mode = DISPLAY_MODE_ANGLE;
static display_mode_t previous_main_mode = DISPLAY_MODE_ANGLE; // 记录进入settings前的模组
static bool mode_switching = false;

#if !FEATURE_UI_LEVEL
/* Level 下线时，把残留的 LEVEL 主模式回落到 ANGLE */
static display_mode_t sanitize_main_mode(display_mode_t mode)
{
    return (mode == DISPLAY_MODE_LEVEL) ? DISPLAY_MODE_ANGLE : mode;
}
#endif
// 已移除：未使用的触摸防抖变量
// static uint32_t last_touch_time = 0;
// static const uint32_t TOUCH_DEBOUNCE_MS = 500;

// 滑动检测状态管理
static bool swipe_active = false;
static uint16_t swipe_start_x = 0;
static uint16_t swipe_start_y = 0;
static uint16_t swipe_last_x = 0;  // 滑动过程中的最后位置
static uint16_t swipe_last_y = 0;  // 滑动过程中的最后位置
static uint32_t swipe_start_time = 0;

// 设置界面底部提示矩形状态管理
static bool bottom_touch_hint_active = false;  // 跟踪是否已在底部区域触摸过
static bool settings_open_preview = false;    // 下滑打开时的跟手预览
static bool settings_close_pending = false;   // 上滑关闭中：勿把偏移弹回 0
static int settings_drag_offset = 0;          // 当前 sheet 偏移（0=展开，负=上移）

#define SWIPE_MIN_DISTANCE 60    // 最小滑动距离（像素）从80减少到60
#define SWIPE_MAX_TIME_MS 800    // 最大滑动时间（毫秒）
#define SWIPE_MIN_TIME_MS 100    // 最小滑动时间（毫秒）避免误触
#define SWIPE_MAX_Y_DEVIATION 160 // X轴最大偏移（像素）允许更倾斜的滑动，从80px增加到160px
#define SWIPE_BOTTOM_ZONE_PX 48  // 关闭手势起点：底部区域高度
#define SWIPE_FOLLOW_START_PX 10 // 开始跟手预览的最小位移
#define SWIPE_OPEN_COMMIT_PX 100 // 下滑松手后确认打开
#define SWIPE_CLOSE_COMMIT_PX 40 // 上滑松手后确认关闭
/* 主界面底部为按键区：从此处起滑不触发 Settings 打开预览，避免抢走 Measure 等点击 */
#define SWIPE_OPEN_EXCLUDE_BOTTOM_PX 100

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
static void touch_monitor_task(void *arg);

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

    /* 与刷屏像素微移对齐 */
    amoled_burn_protection_map_touch(screen_x, screen_y);
    
    // 边界检查
    if (*screen_x >= EXAMPLE_LCD_H_RES) *screen_x = EXAMPLE_LCD_H_RES - 1;
    if (*screen_y >= EXAMPLE_LCD_V_RES) *screen_y = EXAMPLE_LCD_V_RES - 1;
}

// 检查触摸是否在左下角切换区域内（已弃用 - 改为滑动切换）
/*
static bool is_touch_in_switch_area(uint16_t x, uint16_t y) {
    printf("COORDINATE DEBUG: screen(%d,%d) vs switch_area(x=%d-%d, y=%d-%d)\n", 
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
    printf("触摸区域分析: %s (屏幕四等分)\n", region);
    
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

static void settings_sheet_apply_offset(int offset_y) {
    if (offset_y > 0) {
        offset_y = 0;
    }
    if (offset_y < -EXAMPLE_LCD_V_RES) {
        offset_y = -EXAMPLE_LCD_V_RES;
    }
    settings_drag_offset = offset_y;
    if (example_lvgl_lock(30)) {
        settings_ui_update_swipe_offset(offset_y);
        example_lvgl_unlock();
    }
}

static bool settings_sheet_ensure_preview(void) {
    if (settings_ui_is_initialized()) {
        // 已有界面（含上次残留）也必须进入预览态，松手才能走提交/取消
        settings_open_preview = true;
        return true;
    }
    if (!example_lvgl_lock(100)) {
        printf("SETTINGS PREVIEW: Failed to lock LVGL\n");
        return false;
    }
    settings_ui_init(lv_scr_act(), EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    settings_ui_update_swipe_offset(-EXAMPLE_LCD_V_RES);
    example_lvgl_unlock();
    settings_drag_offset = -EXAMPLE_LCD_V_RES;
    settings_open_preview = settings_ui_is_initialized();
    return settings_open_preview;
}

static void settings_sheet_cancel_preview(void) {
    if (!settings_open_preview && !settings_ui_is_initialized()) {
        return;
    }
    if (example_lvgl_lock(100)) {
        settings_ui_cleanup();
        example_lvgl_unlock();
    }
    settings_open_preview = false;
    settings_drag_offset = 0;
}

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
            printf("SWIPE: Invalid duration (%dms), range: %d-%dms\n", 
                   (int)duration_ms, SWIPE_MIN_TIME_MS, SWIPE_MAX_TIME_MS);
            return SWIPE_NONE;
        }
    } else {
        printf("SWIPE: Settings exit swipe - time limit bypassed (duration: %dms)\n", (int)duration_ms);
    }
    
    // 判断是水平滑动还是垂直滑动
    if (abs_dx > abs_dy) {
        // 水平滑动 - 检查X轴位移是否足够
        if (abs_dx < SWIPE_MIN_DISTANCE) {
            return SWIPE_NONE;
        }
        
        // 检查Y轴偏移是否在允许范围内
        if (abs_dy > SWIPE_MAX_Y_DEVIATION) {
            printf("SWIPE: Excessive Y deviation (%d > %d) for horizontal swipe\n", abs_dy, SWIPE_MAX_Y_DEVIATION);
            return SWIPE_NONE;
        }
        
        // 确定水平滑动方向
        if (dx > 0) {
            printf("SWIPE: Detected RIGHT swipe (previous main module)\n");
            return SWIPE_RIGHT;
        } else {
            printf("SWIPE: Detected LEFT swipe (next main module)\n");
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
            printf("SWIPE: Excessive X deviation (%d > %d) for vertical swipe\n", abs_dx, SWIPE_MAX_Y_DEVIATION);
            return SWIPE_NONE;
        }
        
        // 确定垂直滑动方向
        if (dy > 0) {
            printf("SWIPE: Detected DOWN swipe (enter settings)\n");
            return SWIPE_DOWN;
        } else {
            // 上滑检测 - 需要检查是否允许
            if (!allow_up_swipe) {
                printf("SWIPE: UP swipe not allowed (not started from bottom area)\n");
                return SWIPE_NONE;
            }
            printf("SWIPE: Detected UP swipe (exit settings) - started from bottom\n");
            return SWIPE_UP;
        }
    }
}

// 根据滑动方向切换组件；返回是否成功发起状态切换（或无需切换）
static bool handle_swipe_switch(swipe_direction_t direction) {
    if (direction == SWIPE_NONE) return false;
    
    display_mode_t new_mode = current_display_mode;
    ui_state_t target_state;
    
    switch (direction) {
        case SWIPE_LEFT:
            // 向左滑 - 下一个主模组 (Angle / Action / Laser，可选 Level)
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("SWIPE LEFT: Settings mode doesn't support horizontal switching\n");
                return false;
            }
            
            switch (current_display_mode) {
#if FEATURE_UI_LEVEL
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_LEVEL; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_ACTION; break;
                case DISPLAY_MODE_ACTION: new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_ANGLE; break;
#else
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_ACTION; break;
                case DISPLAY_MODE_ACTION: new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_ACTION; break; /* 残留 LEVEL 时跳过 */
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_ANGLE; break;
#endif
                default: return false;
            }
            printf("SWIPE LEFT: %d -> %d (next main module)\n", current_display_mode, new_mode);
            break;
            
        case SWIPE_RIGHT:
            // 向右滑 - 上一个主模组 (Angle / Action / Laser，可选 Level)
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("SWIPE RIGHT: Settings mode doesn't support horizontal switching\n");
                return false;
            }
            
            switch (current_display_mode) {
#if FEATURE_UI_LEVEL
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_ACTION; break;
                case DISPLAY_MODE_ACTION: new_mode = DISPLAY_MODE_LEVEL; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_ANGLE; break;
#else
                case DISPLAY_MODE_ANGLE:  new_mode = DISPLAY_MODE_LASER; break;
                case DISPLAY_MODE_LASER:  new_mode = DISPLAY_MODE_ACTION; break;
                case DISPLAY_MODE_ACTION: new_mode = DISPLAY_MODE_ANGLE; break;
                case DISPLAY_MODE_LEVEL:  new_mode = DISPLAY_MODE_ANGLE; break; /* 残留 LEVEL 时回退 */
#endif
                default: return false;
            }
            printf("SWIPE RIGHT: %d -> %d (previous main module)\n", current_display_mode, new_mode);
            break;
            
        case SWIPE_DOWN:
            // 向下滑 - 进入Settings模组
            if (current_display_mode == DISPLAY_MODE_SETTINGS) {
                printf("SWIPE DOWN: Already in Settings mode\n");
                return false;
            }
            
            // 记录当前主模组状态
            previous_main_mode = current_display_mode;
            new_mode = DISPLAY_MODE_SETTINGS;
            printf("SWIPE DOWN: %d -> Settings (saved previous: %d)\n", current_display_mode, previous_main_mode);
            break;
            
        case SWIPE_UP:
            // 向上滑 - 退出Settings模组，回到上一次的主模组
            if (current_display_mode != DISPLAY_MODE_SETTINGS) {
                printf("SWIPE UP: Not in Settings mode\n");
                return false;
            }
            
            new_mode = previous_main_mode;
#if !FEATURE_UI_LEVEL
            new_mode = sanitize_main_mode(new_mode);
#endif
            printf("SWIPE UP: Settings -> %d (restored previous module)\n", new_mode);
            break;
            
        default:
            return false;
    }
    
    if (new_mode != current_display_mode) {
        // 映射显示模式到UI状态
        switch (new_mode) {
            case DISPLAY_MODE_ANGLE:    target_state = UI_STATE_ANGLE; break;
            case DISPLAY_MODE_LEVEL:    target_state = UI_STATE_LEVEL; break;
            case DISPLAY_MODE_LASER:    target_state = UI_STATE_LASER; break;
            case DISPLAY_MODE_ACTION:   target_state = UI_STATE_ACTION; break;
            case DISPLAY_MODE_SETTINGS: target_state = UI_STATE_SETTINGS; break;
            default: return false;
        }
        
        esp_err_t ret = ui_state_request_transition(target_state, 5000); // 5秒超时
        if (ret == ESP_OK) {
            printf("SWIPE: UI transition requested to state %d\n", target_state);
            return true;
        }
        printf("SWIPE: UI transition failed: %s\n", esp_err_to_name(ret));
        return false;
    }
    return true;
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
        case UI_STATE_ACTION:
            current_display_mode = DISPLAY_MODE_ACTION;
            break;
        case UI_STATE_SETTINGS:
            current_display_mode = DISPLAY_MODE_SETTINGS;
            settings_open_preview = false;
            settings_close_pending = false;
            // 打开跟手结束后吸附到完全展开
            settings_sheet_apply_offset(0);
            break;
        default:
            ESP_LOGW("user_app", "Unknown UI state: %d", new_state);
            break;
    }

    if (old_state == UI_STATE_SETTINGS && new_state != UI_STATE_SETTINGS) {
        settings_close_pending = false;
        settings_open_preview = false;
        settings_drag_offset = 0;
    }
    
    // 管理previous_main_mode：如果是从主模组切换到Settings，更新previous_main_mode
    if (current_display_mode == DISPLAY_MODE_SETTINGS && 
        old_mode != DISPLAY_MODE_SETTINGS &&
        (old_mode == DISPLAY_MODE_ANGLE || old_mode == DISPLAY_MODE_LEVEL ||
         old_mode == DISPLAY_MODE_LASER || old_mode == DISPLAY_MODE_ACTION)) {
#if FEATURE_UI_LEVEL
        previous_main_mode = old_mode;
#else
        previous_main_mode = sanitize_main_mode(old_mode);
#endif
        printf("SAVED previous main mode: %d before entering Settings\n", previous_main_mode);
    }
    
    mode_switching = false;  // 重置切换标志

    /* Pixel-shift can leave stale edge pixels across UI teardown/rebuild. */
    display_clear_burn_edges();
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

// 电池保护关机前的回调函数
static void battery_protection_shutdown_callback(void) {
    ESP_LOGW("user_app", "Battery protection: shutdown cleanup");
    idle_sleep_stop();
    amoled_burn_protection_stop();
}

static void idle_sleep_shutdown_callback(void) {
    ESP_LOGW("user_app", "Idle sleep: shutdown cleanup");
    amoled_burn_protection_stop();
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
        {
            float gx = 0, gy = 0, gz = 0;
            angle_calc_get_gyro(&gx, &gy, &gz);
            idle_sleep_feed_gyro(gx, gy, gz);
        }
        
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
                if (example_lvgl_lock(40)) {
                    angle_display_update(pitch, roll);
                    example_lvgl_unlock();
                }
#if FEATURE_UI_LEVEL
            } else if (current_ui_state == UI_STATE_LEVEL && current_display_mode == DISPLAY_MODE_LEVEL) {
                if (example_lvgl_lock(40)) {
                    level_display_update(roll);
                    example_lvgl_unlock();
                }
#endif
            } else if (current_ui_state == UI_STATE_ACTION && current_display_mode == DISPLAY_MODE_ACTION) {
                if (example_lvgl_lock(40)) {
                    action_display_update(pitch, yaw);
                    example_lvgl_unlock();
                }
            } else if (current_ui_state == UI_STATE_LASER && current_display_mode == DISPLAY_MODE_LASER) {
                // laser模式：不需要周期性更新，距离只在测量时显示
            } else if (current_ui_state == UI_STATE_SETTINGS && current_display_mode == DISPLAY_MODE_SETTINGS) {
                // settings模式：每1秒读取一次电池电压，避免频繁ADC读取
                uint32_t now = esp_timer_get_time() / 1000; // 转换为毫秒
                if (now - last_voltage_update >= VOLTAGE_UPDATE_INTERVAL_MS) {
                    last_voltage_update = now;
                    float battery_voltage;
                    adc_get_value(&battery_voltage);
                    if (example_lvgl_lock(40)) {
                        settings_ui_update_battery_info(battery_voltage);
                        uint8_t current_brightness = getBrightness();
                        settings_ui_update_brightness(current_brightness);
                        example_lvgl_unlock();
                    }
                }
            }
        }
        
        // 主动让出CPU时间
        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(25)); // 40Hz 刷新
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
            idle_sleep_on_activity();
            // 转换触摸坐标到屏幕坐标系
            transform_touch_coordinates(raw_x, raw_y, &screen_x, &screen_y);
            
            // 检查是否在设置界面的底部区域（关闭手势起点）
            // 弹药子页打开时禁用，避免抢占 Back/Save 等底部按钮
            if (ui_state_get_current() == UI_STATE_SETTINGS && !settings_close_pending &&
                !settings_ammo_page_is_open()) {
                if (screen_y >= EXAMPLE_LCD_V_RES - SWIPE_BOTTOM_ZONE_PX) {
                    if (!bottom_touch_hint_active) {
                        bottom_touch_hint_active = true;
                        if (example_lvgl_lock(30)) {
                            settings_ui_update_swipe_hint(true);
                            example_lvgl_unlock();
                        }
                    }
                }
            }
            
            // 滑动检测
            if (!swipe_active) {
                swipe_active = true;
                swipe_start_x = screen_x;
                swipe_start_y = screen_y;
                swipe_last_x = screen_x;
                swipe_last_y = screen_y;
                swipe_start_time = current_time;
            } else {
                swipe_last_x = screen_x;
                swipe_last_y = screen_y;

                int delta_x = (int)screen_x - (int)swipe_start_x;
                int delta_y = (int)screen_y - (int)swipe_start_y;
                int abs_dx = delta_x >= 0 ? delta_x : -delta_x;
                int abs_dy = delta_y >= 0 ? delta_y : -delta_y;
                ui_state_t ui_now = ui_state_get_current();

                // 关闭：Settings 内从底部起滑，向上跟手（弹药页打开时禁用）
                if (ui_now == UI_STATE_SETTINGS && bottom_touch_hint_active &&
                    !settings_close_pending && !settings_ammo_page_is_open() &&
                    delta_y < 0) {
                    settings_sheet_apply_offset(delta_y);
                }

                // 打开：主界面下滑时创建 Settings 预览并跟手落下
                // 起点在底部按键区时忽略，避免轻触 Measure 时被当成下滑
                if (ui_now != UI_STATE_SETTINGS && !settings_close_pending &&
                    swipe_start_y < (EXAMPLE_LCD_V_RES - SWIPE_OPEN_EXCLUDE_BOTTOM_PX) &&
                    abs_dy > abs_dx && delta_y >= SWIPE_FOLLOW_START_PX &&
                    abs_dx <= SWIPE_MAX_Y_DEVIATION) {
                    if (settings_sheet_ensure_preview()) {
                        settings_sheet_apply_offset(-EXAMPLE_LCD_V_RES + delta_y);
                    }
                }
            }
        } else {
            if (swipe_active) {
                // 滑动结束，使用最后记录的位置检测滑动方向
                uint32_t swipe_duration = current_time - swipe_start_time;
                int release_dy = (int)swipe_last_y - (int)swipe_start_y;
                
                // 打开预览：按跟手进度决定提交或取消
                if (settings_open_preview) {
                    if (release_dy >= SWIPE_OPEN_COMMIT_PX ||
                        settings_drag_offset > -(EXAMPLE_LCD_V_RES * 3 / 4)) {
                        printf("SWIPE: Open preview committed (dy=%d, offset=%d)\n",
                               release_dy, settings_drag_offset);
                        settings_sheet_apply_offset(0);
                        if (handle_swipe_switch(SWIPE_DOWN)) {
                            settings_open_preview = false;
                        } else {
                            // 状态切换失败：回收预览，避免菜单卡在主界面之上
                            printf("SWIPE: Open commit failed, cancelling preview\n");
                            settings_sheet_cancel_preview();
                        }
                    } else {
                        printf("SWIPE: Open preview cancelled (dy=%d, offset=%d)\n",
                               release_dy, settings_drag_offset);
                        settings_sheet_cancel_preview();
                    }
                } else {
                    // 检查是否允许上滑：只有在设置界面且从底部区域开始触摸才允许
                    bool allow_up_swipe = (ui_state_get_current() == UI_STATE_SETTINGS) &&
                                         bottom_touch_hint_active &&
                                         !settings_ammo_page_is_open();
                    printf("SWIPE DEBUG: UI_STATE=%d, bottom_hint_active=%d, allow_up_swipe=%d\n", 
                           ui_state_get_current(), bottom_touch_hint_active, allow_up_swipe);
                    
                    swipe_direction_t direction = detect_swipe(swipe_start_x, swipe_start_y, 
                                                              swipe_last_x, swipe_last_y, swipe_duration, allow_up_swipe);

                    // 跟手超过阈值时，即使时间窗未命中也允许关闭
                    if (allow_up_swipe && direction == SWIPE_NONE &&
                        settings_drag_offset <= -SWIPE_CLOSE_COMMIT_PX) {
                        direction = SWIPE_UP;
                    }
                    
                    if (direction != SWIPE_NONE) {
                        printf("SWIPE: Completed - processing direction %d\n", direction);
                        if (direction == SWIPE_UP) {
                            // 保持当前偏移，避免先弹回展开再关闭
                            settings_close_pending = true;
                            if (!handle_swipe_switch(direction)) {
                                // 切换失败：恢复可关闭状态并弹回展开
                                settings_close_pending = false;
                                settings_sheet_apply_offset(0);
                            }
                        } else {
                            handle_swipe_switch(direction);
                        }
                    } else if (ui_state_get_current() == UI_STATE_SETTINGS && !settings_close_pending) {
                        // 未触发关闭：弹回完全展开
                        settings_sheet_apply_offset(0);
                    }
                }
                
                swipe_active = false;
            }
            
            // 触摸释放时，如果在设置界面，恢复提示矩形正常状态
            // 注意：这个重置要在滑动检测之后，避免影响allow_up_swipe判断
            if (ui_state_get_current() == UI_STATE_SETTINGS && bottom_touch_hint_active) {
                bottom_touch_hint_active = false;
                if (example_lvgl_lock(30)) {
                    settings_ui_update_swipe_hint(false);
                    example_lvgl_unlock();
                }
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
    angle_calc_init(0.025f, 0.3f); // 25ms @ 40Hz, beta=0.3
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

  /* Init only; start after UI lock path in main. */
  esp_err_t bat_ret = battery_protection_init(battery_protection_shutdown_callback);
  if (bat_ret != ESP_OK) {
      ESP_LOGE("user_app", "Battery protection init failed: %s", esp_err_to_name(bat_ret));
  }

  esp_err_t idle_ret = idle_sleep_init(idle_sleep_shutdown_callback);
  if (idle_ret != ESP_OK) {
      ESP_LOGE("user_app", "Idle sleep init failed: %s", esp_err_to_name(idle_ret));
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
