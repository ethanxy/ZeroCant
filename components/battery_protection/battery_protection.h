#ifndef BATTERY_PROTECTION_H
#define BATTERY_PROTECTION_H

#include "esp_err.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 电池保护配置
#define BATTERY_CRITICAL_VOLTAGE        3.4f    // 临界电压阈值
#define BATTERY_CHECK_INTERVAL_MS       300000  // 5分钟检查间隔
#define BATTERY_SHUTDOWN_DELAY_SEC      10      // 关机倒计时秒数
#define BATTERY_LOW_BRIGHTNESS_PERCENT  10      // 低电量时的亮度百分比

// 电池保护状态
typedef enum {
    BATTERY_PROTECTION_IDLE,           // 空闲状态
    BATTERY_PROTECTION_WARNING,        // 警告状态
    BATTERY_PROTECTION_SHUTDOWN        // 关机倒计时状态
} battery_protection_state_t;

// 电池保护回调类型
typedef void (*battery_shutdown_callback_t)(void);

/**
 * @brief 初始化电池保护功能
 * @param shutdown_callback 关机前的回调函数（可选，用于保存数据等）
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t battery_protection_init(battery_shutdown_callback_t shutdown_callback);

/**
 * @brief 启动电池保护监控
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t battery_protection_start(void);

/**
 * @brief 停止电池保护监控
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t battery_protection_stop(void);

/**
 * @brief 手动触发电池检查
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t battery_protection_check_now(void);

/**
 * @brief 获取当前电池保护状态
 * @return 当前保护状态
 */
battery_protection_state_t battery_protection_get_state(void);

/**
 * @brief 获取最后记录的电池电压
 * @return 最后记录的电压值，如果未记录则返回0.0f
 */
float battery_protection_get_last_voltage(void);

/**
 * @brief 反初始化电池保护功能
 * @return ESP_OK 成功，其他值为错误
 */
esp_err_t battery_protection_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_PROTECTION_H
