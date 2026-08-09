#include "settings.h"
#include "angle_calc.h"
#include "lvgl.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <string.h>

// 电池电压到百分比转换参数（基于LiPo实际放电曲线）
#define BATTERY_VOLTAGE_MIN 3.40f   // 放电截止点 (0%)
#define BATTERY_VOLTAGE_MAX 4.20f   // 满电 (100%)

// LiPo电池电压-容量映射表（非线性）
typedef struct {
    float voltage;
    int percentage;
} battery_curve_point_t;

static const battery_curve_point_t battery_curve[] = {
    {4.20f, 100},  // 满电
    {4.15f, 95},
    {4.10f, 90},
    {4.05f, 85},
    {4.00f, 80},
    {3.95f, 75},
    {3.90f, 65},
    {3.85f, 60},
    {3.80f, 55},
    {3.75f, 50},   // 半容量附近
    {3.70f, 40},
    {3.65f, 30},
    {3.60f, 25},
    {3.55f, 15},
    {3.50f, 10},
    {3.45f, 5},
    {3.40f, 0}     // 放电截止点
};

#define BATTERY_CURVE_SIZE (sizeof(battery_curve) / sizeof(battery_curve[0]))

// 亮度控制参数
#define BRIGHTNESS_MIN_PERCENT 10   // 最低亮度百分比
#define BRIGHTNESS_MAX_PERCENT 100  // 最高亮度百分比
#define BRIGHTNESS_STEP 10          // 亮度调节步长

static lv_obj_t *settings_screen = NULL;
static lv_obj_t *voltage_label = NULL;
static lv_obj_t *battery_percentage_label = NULL;
static lv_obj_t *brightness_label = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *calibration_button = NULL;
static lv_obj_t *calibration_label = NULL;
static lv_obj_t *swipe_hint_rect = NULL;  // 向上滑动提示矩形
static lv_obj_t *version_label = NULL;    // 固件版本号
static int screen_width = 240;
static int screen_height = 240;

// Build YYYYMMDDHH from compiler __DATE__/__TIME__ when app descriptor has no version.
static void format_version_from_build_time(char *out, size_t out_len) {
    // __DATE__ = "Mmm dd yyyy", __TIME__ = "hh:mm:ss"
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_str[4] = {0};
    int day = 0, year = 0, hour = 0;
    if (sscanf(__DATE__, "%3s %d %d", month_str, &day, &year) != 3) {
        snprintf(out, out_len, "unknown");
        return;
    }
    if (sscanf(__TIME__, "%d:", &hour) != 1) {
        hour = 0;
    }
    const char *p = strstr(months, month_str);
    int month = p ? (int)(p - months) / 3 + 1 : 0;
    snprintf(out, out_len, "%04d%02d%02d%02d", year, month, day, hour);
}

static const char *resolve_firmware_version(void) {
    static char fallback_version[16];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc && app_desc->version[0] != '\0' &&
        strcmp(app_desc->version, "1") != 0 &&
        strcmp(app_desc->version, "0.0.0") != 0) {
        return app_desc->version;
    }
    format_version_from_build_time(fallback_version, sizeof(fallback_version));
    return fallback_version;
}

// 外部函数声明
extern void setBrightnes(uint8_t brig);  // 注意：原函数名有拼写错误
extern uint8_t getBrightness(void);

// 根据电池电压计算百分比的辅助函数（基于非线性LiPo放电曲线）
static int calculate_battery_percentage(float voltage) {
    // 边界处理
    if (voltage <= BATTERY_VOLTAGE_MIN) {
        return 0;
    }
    if (voltage >= BATTERY_VOLTAGE_MAX) {
        return 100;
    }
    
    // 在映射表中查找合适的区间进行线性插值
    for (int i = 0; i < BATTERY_CURVE_SIZE - 1; i++) {
        float v_high = battery_curve[i].voltage;
        float v_low = battery_curve[i + 1].voltage;
        int p_high = battery_curve[i].percentage;
        int p_low = battery_curve[i + 1].percentage;
        
        // 如果电压在当前区间内，进行线性插值
        if (voltage <= v_high && voltage >= v_low) {
            // 线性插值公式：percentage = p_low + (voltage - v_low) * (p_high - p_low) / (v_high - v_low)
            float percentage = p_low + (voltage - v_low) * (p_high - p_low) / (v_high - v_low);
            return (int)(percentage + 0.5f); // 四舍五入
        }
    }
    
    // 如果没有找到合适区间（理论上不应该发生），返回默认值
    return 0;
}

// 亮度滑动条事件处理函数
static void brightness_slider_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t slider_value = lv_slider_get_value(slider);
        
        // 将滑动条值(10-100)转换为亮度值(26-255)
        // 10% -> 26, 100% -> 255
        uint8_t brightness = (uint8_t)((slider_value * 255) / 100);
        if (brightness < 26) brightness = 26;  // 确保最低亮度
        
        // 设置屏幕亮度
        setBrightnes(brightness);
        
        // 更新亮度标签显示
        settings_ui_update_brightness(brightness);
    }
}

// 校准按钮事件处理函数
static void calibration_button_event(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        printf("Calibration button clicked - setting current orientation as level\n");
        
        // 执行校准
        angle_calc_calibrate_level();
        
        // 更新按钮状态显示
        if (calibration_label) {
            lv_label_set_text(calibration_label, "Calibrated");
            lv_obj_set_style_text_color(calibration_label, lv_color_hex(0x00FF00), 0); // 绿色
        }
        
        printf("Level calibration completed\n");
    }
}

void settings_ui_init(lv_obj_t *parent, int scr_width, int scr_height) {
    screen_width = scr_width;
    screen_height = scr_height;
    
    printf("settings_ui_init: initializing %dx%d settings UI\n", screen_width, screen_height);
    
    // 检查是否已经初始化
    if (settings_screen) {
        printf("settings_ui_init: Warning - UI already initialized\n");
        return;
    }
    
    // 创建主屏幕容器（全黑背景）
    settings_screen = lv_obj_create(parent);
    if (!settings_screen) {
        printf("settings_ui_init: Failed to create settings_screen\n");
        return;
    }
    
    lv_obj_set_size(settings_screen, screen_width, screen_height);
    lv_obj_align(settings_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(settings_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_screen, 0, 0);
    lv_obj_set_style_pad_all(settings_screen, 0, 0);
    
    // 创建标题标签（屏幕顶部）
    title_label = lv_label_create(settings_screen);
    if (!title_label) {
        printf("settings_ui_init: Failed to create title_label\n");
        return;
    }
    
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);  // 改为24号字体
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);  // 移动到顶部，距离顶部20像素
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // 创建电池电压标签（屏幕中央偏上）
    voltage_label = lv_label_create(settings_screen);
    if (!voltage_label) {
        printf("settings_ui_init: Failed to create voltage_label\n");
        return;
    }
    
    lv_label_set_text(voltage_label, "Voltage: --.-V");
    lv_obj_set_style_text_color(voltage_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(voltage_label, &lv_font_montserrat_16, 0);
    lv_obj_align(voltage_label, LV_ALIGN_LEFT_MID, 20, -100);  // 左对齐，距离左边20px，向上移动100px
    lv_obj_set_style_text_align(voltage_label, LV_TEXT_ALIGN_LEFT, 0);  // 改为左对齐
    
    // 创建电池百分比标签（电压下方）
    battery_percentage_label = lv_label_create(settings_screen);
    if (!battery_percentage_label) {
        printf("settings_ui_init: Failed to create battery_percentage_label\n");
        return;
    }
    
    lv_label_set_text(battery_percentage_label, "Battery: --%");
    lv_obj_set_style_text_color(battery_percentage_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(battery_percentage_label, &lv_font_montserrat_16, 0);
    lv_obj_align(battery_percentage_label, LV_ALIGN_LEFT_MID, 20, -60);  // 左对齐，距离左边20px，向上移动60px
    lv_obj_set_style_text_align(battery_percentage_label, LV_TEXT_ALIGN_LEFT, 0);  // 改为左对齐
    
    // 创建屏幕亮度标签（百分比下方）
    brightness_label = lv_label_create(settings_screen);
    if (!brightness_label) {
        printf("settings_ui_init: Failed to create brightness_label\n");
        return;
    }
    
    lv_label_set_text(brightness_label, "Brightness: ---%");
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_16, 0);
    lv_obj_align(brightness_label, LV_ALIGN_LEFT_MID, 20, -20);  // 左对齐，距离左边20px，向上移动20px
    lv_obj_set_style_text_align(brightness_label, LV_TEXT_ALIGN_LEFT, 0);  // 改为左对齐
    
    // 创建亮度滑动条（亮度标签下方）
    brightness_slider = lv_slider_create(settings_screen);
    if (!brightness_slider) {
        printf("settings_ui_init: Failed to create brightness_slider\n");
        return;
    }
    
    // 设置滑动条属性 - 计算宽度使左右边距对称
    int slider_width = screen_width - 40;  // 屏幕宽度减去左右各20px边距
    lv_obj_set_size(brightness_slider, slider_width, 10);  // 对称宽度，恢复细条外观10px高度
    lv_obj_align(brightness_slider, LV_ALIGN_LEFT_MID, 20, 20);  // 左对齐，距离左边20px，向下移动20px
    lv_slider_set_range(brightness_slider, BRIGHTNESS_MIN_PERCENT, BRIGHTNESS_MAX_PERCENT);  // 10%-100%
    
    // 设置滑动条样式 - 恢复原始外观但保持触摸优化
    lv_obj_set_style_bg_color(brightness_slider, lv_color_white(), LV_PART_MAIN);  // 背景白色
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_50, LV_PART_MAIN);  // 背景透明度恢复50%
    lv_obj_set_style_bg_color(brightness_slider, lv_color_white(), LV_PART_INDICATOR);  // 指示器白色
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);  // 指示器不透明
    lv_obj_set_style_bg_color(brightness_slider, lv_color_white(), LV_PART_KNOB);  // 旋钮白色
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_COVER, LV_PART_KNOB);  // 旋钮不透明
    lv_obj_set_style_radius(brightness_slider, 8, LV_PART_KNOB);  // 旋钮圆角恢复原始8px
    lv_obj_set_style_pad_all(brightness_slider, 6, LV_PART_KNOB);  // 旋钮内边距恢复原始6px
    lv_obj_set_style_width(brightness_slider, 20, LV_PART_KNOB);   // 设置旋钮宽度恢复20px
    lv_obj_set_style_height(brightness_slider, 20, LV_PART_KNOB);  // 设置旋钮高度恢复20px
    
    // 保持触摸范围优化 - 这是关键改进
    lv_obj_set_ext_click_area(brightness_slider, 15);  // 扩展点击区域，增加到15px获得更好触摸体验
    
    // 获取当前亮度并设置滑动条初始值
    uint8_t current_brightness = getBrightness();
    int brightness_percent = (current_brightness * 100) / 255;
    if (brightness_percent < BRIGHTNESS_MIN_PERCENT) brightness_percent = BRIGHTNESS_MIN_PERCENT;
    if (brightness_percent > BRIGHTNESS_MAX_PERCENT) brightness_percent = BRIGHTNESS_MAX_PERCENT;
    lv_slider_set_value(brightness_slider, brightness_percent, LV_ANIM_OFF);
    
    // 添加事件处理
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    
    // 创建校准按钮（亮度滑动条下方）
    calibration_button = lv_btn_create(settings_screen);
    if (!calibration_button) {
        printf("settings_ui_init: Failed to create calibration_button\n");
        return;
    }
    
    // 设置按钮样式和位置
    lv_obj_set_size(calibration_button, 180, 40);  // 按钮大小
    lv_obj_align(calibration_button, LV_ALIGN_LEFT_MID, 20, 80);  // 距离左边20px，向下移动80px
    lv_obj_set_style_bg_color(calibration_button, lv_color_hex(0x404040), 0);  // 深灰色背景
    lv_obj_set_style_bg_opa(calibration_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(calibration_button, lv_color_white(), 0);   // 白色边框
    lv_obj_set_style_border_width(calibration_button, 1, 0);
    lv_obj_set_style_radius(calibration_button, 5, 0);  // 圆角
    
    // 创建按钮标签
    calibration_label = lv_label_create(calibration_button);
    if (!calibration_label) {
        printf("settings_ui_init: Failed to create calibration_label\n");
        return;
    }
    
    lv_label_set_text(calibration_label, "Set Level");
    lv_obj_set_style_text_color(calibration_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(calibration_label, &lv_font_montserrat_16, 0);
    lv_obj_center(calibration_label);  // 标签居中
    
    // 添加按钮事件处理
    lv_obj_add_event_cb(calibration_button, calibration_button_event, LV_EVENT_CLICKED, NULL);
    
    // 固件版本号（底部，提示条上方）
    version_label = lv_label_create(settings_screen);
    if (!version_label) {
        printf("settings_ui_init: Failed to create version_label\n");
        return;
    }

    {
        char version_str[48];
        snprintf(version_str, sizeof(version_str), "FW %s", resolve_firmware_version());
        lv_label_set_text(version_label, version_str);
    }
    lv_obj_set_style_text_color(version_label, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(version_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_clear_flag(version_label, LV_OBJ_FLAG_CLICKABLE);

    // 创建向上滑动提示矩形（屏幕最下方）
    swipe_hint_rect = lv_obj_create(settings_screen);
    if (!swipe_hint_rect) {
        printf("settings_ui_init: Failed to create swipe_hint_rect\n");
        return;
    }
    
    // 设置提示矩形样式和位置
    lv_obj_set_size(swipe_hint_rect, 80, 10);  // 80px宽，10px高
    lv_obj_align(swipe_hint_rect, LV_ALIGN_BOTTOM_MID, 0, -5);  // 底部居中，距离底部5px
    lv_obj_set_style_bg_color(swipe_hint_rect, lv_color_hex(0x808080), 0);  // 灰色背景
    lv_obj_set_style_bg_opa(swipe_hint_rect, LV_OPA_70, 0);  // 70%透明度，不要太显眼
    lv_obj_set_style_border_width(swipe_hint_rect, 0, 0);  // 无边框
    lv_obj_set_style_radius(swipe_hint_rect, 10, 0);  // 圆角矩形
    lv_obj_set_style_pad_all(swipe_hint_rect, 0, 0);  // 无内边距
    
    // 禁用矩形的触摸事件，避免干扰滑动检测
    lv_obj_add_flag(swipe_hint_rect, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(swipe_hint_rect, LV_OBJ_FLAG_CLICKABLE);
    
    printf("settings_ui_init: completed successfully (FW %s)\n", resolve_firmware_version());
}

void settings_ui_update_voltage(float voltage) {
    if (!voltage_label) {
        printf("settings_ui_update_voltage: voltage_label not initialized\n");
        return;
    }
    
    char voltage_str[32];
    snprintf(voltage_str, sizeof(voltage_str), "Voltage: %.2fV", voltage);
    lv_label_set_text(voltage_label, voltage_str);
}

void settings_ui_update_battery_info(float voltage) {
    // 同时更新电压和百分比
    settings_ui_update_voltage(voltage);
    
    // 计算并更新百分比
    int percentage = calculate_battery_percentage(voltage);
    settings_ui_update_battery_percentage(percentage);
}

void settings_ui_update_battery_percentage(int percentage) {
    if (!battery_percentage_label) {
        printf("settings_ui_update_battery_percentage: battery_percentage_label not initialized\n");
        return;
    }
    
    // 限制百分比范围在0-100之间
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    
    char percentage_str[32];
    snprintf(percentage_str, sizeof(percentage_str), "Battery: %d%%", percentage);
    lv_label_set_text(battery_percentage_label, percentage_str);
}

void settings_ui_update_brightness(int brightness) {
    if (!brightness_label) {
        printf("settings_ui_update_brightness: brightness_label not initialized\n");
        return;
    }
    
    // 限制亮度范围在0-255之间，并转换为百分比
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    
    int brightness_percent = (brightness * 100) / 255;
    
    char brightness_str[32];
    snprintf(brightness_str, sizeof(brightness_str), "Brightness: %d%%", brightness_percent);
    lv_label_set_text(brightness_label, brightness_str);
}

void settings_ui_set_brightness_slider(int brightness) {
    if (!brightness_slider) {
        printf("settings_ui_set_brightness_slider: brightness_slider not initialized\n");
        return;
    }
    
    // 限制亮度范围在0-255之间，并转换为百分比
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    
    int brightness_percent = (brightness * 100) / 255;
    if (brightness_percent < BRIGHTNESS_MIN_PERCENT) brightness_percent = BRIGHTNESS_MIN_PERCENT;
    if (brightness_percent > BRIGHTNESS_MAX_PERCENT) brightness_percent = BRIGHTNESS_MAX_PERCENT;
    
    // 设置滑动条值（不触发事件）
    lv_slider_set_value(brightness_slider, brightness_percent, LV_ANIM_OFF);
}

void settings_ui_cleanup(void) {
    printf("settings_ui_cleanup: cleaning up settings UI\n");
    
    // 删除LVGL对象，添加额外的有效性检查
    if (voltage_label && lv_obj_is_valid(voltage_label)) {
        lv_obj_del(voltage_label);
        voltage_label = NULL;
    }
    
    if (battery_percentage_label && lv_obj_is_valid(battery_percentage_label)) {
        lv_obj_del(battery_percentage_label);
        battery_percentage_label = NULL;
    }
    
    if (brightness_label && lv_obj_is_valid(brightness_label)) {
        lv_obj_del(brightness_label);
        brightness_label = NULL;
    }
    
    if (brightness_slider && lv_obj_is_valid(brightness_slider)) {
        lv_obj_del(brightness_slider);
        brightness_slider = NULL;
    }
    
    if (calibration_label && lv_obj_is_valid(calibration_label)) {
        lv_obj_del(calibration_label);
        calibration_label = NULL;
    }
    
    if (calibration_button && lv_obj_is_valid(calibration_button)) {
        lv_obj_del(calibration_button);
        calibration_button = NULL;
    }
    
    if (swipe_hint_rect && lv_obj_is_valid(swipe_hint_rect)) {
        lv_obj_del(swipe_hint_rect);
        swipe_hint_rect = NULL;
    }

    if (version_label && lv_obj_is_valid(version_label)) {
        lv_obj_del(version_label);
        version_label = NULL;
    }
    
    if (title_label && lv_obj_is_valid(title_label)) {
        lv_obj_del(title_label);
        title_label = NULL;
    }
    
    if (settings_screen && lv_obj_is_valid(settings_screen)) {
        lv_obj_del(settings_screen);
        settings_screen = NULL;
    }
    
    printf("settings_ui_cleanup: completed\n");
}

void settings_ui_update_calibration_status(bool calibrated) {
    if (!calibration_label) {
        printf("settings_ui_update_calibration_status: calibration_label not initialized\n");
        return;
    }
    
    if (calibrated) {
        lv_label_set_text(calibration_label, "Calibrated");
        lv_obj_set_style_text_color(calibration_label, lv_color_hex(0x00FF00), 0); // 绿色
    } else {
        lv_label_set_text(calibration_label, "Set Level");
        lv_obj_set_style_text_color(calibration_label, lv_color_white(), 0); // 白色
    }
}

void settings_ui_update_swipe_hint(bool highlighted) {
    if (!swipe_hint_rect) {
        // 静默失败，避免在非Settings模式下产生日志噪音
        return;
    }
    
    if (highlighted) {
        // 高亮状态：白色，更不透明
        lv_obj_set_style_bg_color(swipe_hint_rect, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(swipe_hint_rect, LV_OPA_90, 0);
    } else {
        // 正常状态：灰色，70%透明度
        lv_obj_set_style_bg_color(swipe_hint_rect, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(swipe_hint_rect, LV_OPA_70, 0);
    }
}

void settings_ui_update_swipe_offset(int offset_y) {
    if (!settings_screen) {
        return;
    }

    // 0 = 完全展开；负值 = 向上移出（打开跟手从 -height 滑到 0）
    if (offset_y > 0) {
        offset_y = 0;
    }
    if (offset_y < -screen_height) {
        offset_y = -screen_height;
    }

    lv_obj_set_y(settings_screen, offset_y);
}

bool settings_ui_is_initialized(void) {
    return settings_screen != NULL;
}
