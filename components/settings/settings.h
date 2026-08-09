#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化设置UI
 * @param parent 父对象
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 */
void settings_ui_init(lv_obj_t *parent, int screen_width, int screen_height);

/**
 * @brief 更新电池电压显示
 * @param voltage 电池电压（伏特）
 */
void settings_ui_update_voltage(float voltage);

/**
 * @brief 同时更新电池电压和百分比显示
 * @param voltage 电池电压（伏特）
 */
void settings_ui_update_battery_info(float voltage);

/**
 * @brief 更新电池百分比显示
 * @param percentage 电池百分比 (0-100)
 */
void settings_ui_update_battery_percentage(int percentage);

/**
 * @brief 更新屏幕亮度显示
 * @param brightness 屏幕亮度 (0-255)
 */
void settings_ui_update_brightness(int brightness);

/**
 * @brief 设置亮度滑动条的值
 * @param brightness 屏幕亮度 (0-255)
 */
void settings_ui_set_brightness_slider(int brightness);

/**
 * @brief 更新校准按钮状态
 * @param calibrated 是否已校准
 */
void settings_ui_update_calibration_status(bool calibrated);

/**
 * @brief 更新滑动提示矩形状态
 * @param highlighted 是否高亮显示（true=白色，false=灰色）
 */
void settings_ui_update_swipe_hint(bool highlighted);

/**
 * @brief 更新设置界面的滑动偏移（视觉跟随效果）
 * @param offset_y Y轴偏移量（0=完全展开，负值=向上移出屏幕）
 */
void settings_ui_update_swipe_offset(int offset_y);

/**
 * @brief 设置界面是否已创建
 */
bool settings_ui_is_initialized(void);

/**
 * @brief 弹药设置子页是否正在显示（用于屏蔽底部上滑关闭手势）
 */
bool settings_ammo_page_is_open(void);

/**
 * @brief 清理设置UI
 */
void settings_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_UI_H
