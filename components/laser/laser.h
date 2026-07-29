#ifndef LASER_UI_H
#define LASER_UI_H

#include "lvgl.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// SDDM激光测距设备配置
#define LASER_UART_NUM          UART_NUM_1
#define LASER_UART_TXD_PIN      18   // ESP32 TX -> SDDM RX
#define LASER_UART_RXD_PIN      17   // ESP32 RX -> SDDM TX
#define LASER_POWER_PIN         45   // 激光器电源控制引脚
#define LASER_UART_BAUD_RATE    115200

// 测量状态
typedef enum {
    LASER_STATE_IDLE,        // 空闲状态
    LASER_STATE_MEASURING,   // 测量中
    LASER_STATE_SUCCESS,     // 测量成功
    LASER_STATE_ERROR        // 测量错误
} laser_state_t;

/**
 * @brief 初始化激光测距UI
 * @param parent 父对象
 * @param screen_width 屏幕宽度
 * @param screen_height 屏幕高度
 */
void laser_ui_init(lv_obj_t *parent, int screen_width, int screen_height);

/**
 * @brief 设置测量状态文本
 * @param text 状态文本
 */
void laser_ui_set_status_text(const char *text);

/**
 * @brief 初始化激光测距硬件
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t laser_hardware_init(void);

/**
 * @brief 执行激光测距测量
 * @param distance_mm 测量结果（毫米）
 * @param timeout_ms 超时时间（毫秒）
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t laser_measure(float *distance_mm, uint32_t timeout_ms);

/**
 * @brief 处理长按测量事件
 * @param is_pressed 是否按下
 * @param press_duration_ms 按下持续时间（毫秒）
 */
void laser_handle_long_press(bool is_pressed, uint32_t press_duration_ms);

/**
 * @brief 启动单次点击测量
 */
void laser_start_measurement(void);

/**
 * @brief 反初始化激光测距硬件
 */
void laser_hardware_deinit(void);

/**
 * @brief 清理激光测距UI
 */
void laser_ui_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // LASER_UI_H
