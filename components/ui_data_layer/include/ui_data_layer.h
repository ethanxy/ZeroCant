#ifndef UI_DATA_LAYER_H
#define UI_DATA_LAYER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 数据类型定义
typedef struct {
    float voltage;              // 电池电压 (V)
    int battery_percentage;     // 电池百分比 (0-100)
    uint8_t brightness;         // 屏幕亮度 (0-255)
    float angle_pitch;          // 俯仰角 (度)
    float angle_roll;           // 翻滚角 (度)
    float laser_distance;       // 激光距离 (米)
    bool data_valid;            // 数据有效性
    uint32_t timestamp;         // 时间戳 (毫秒)
} ui_data_snapshot_t;

// 数据更新事件
typedef enum {
    DATA_EVENT_BATTERY_UPDATE,   // 电池数据更新
    DATA_EVENT_BRIGHTNESS_UPDATE,// 亮度数据更新
    DATA_EVENT_ANGLE_UPDATE,     // 角度数据更新
    DATA_EVENT_LASER_UPDATE,     // 激光数据更新
    DATA_EVENT_ALL_UPDATE        // 所有数据更新
} data_event_t;

// 数据更新消息
typedef struct {
    data_event_t event_type;
    ui_data_snapshot_t data;
    uint32_t sequence_id;
} ui_data_message_t;

// 数据更新回调
typedef void (*ui_data_update_cb_t)(const ui_data_snapshot_t *data);

// 数据发布者
typedef struct {
    const char *topic;
    ui_data_update_cb_t callbacks[8];  // 最多8个订阅者
    int subscriber_count;
    SemaphoreHandle_t mutex;
} data_publisher_t;

// 数据主题常量
#define DATA_TOPIC_BATTERY    "battery"
#define DATA_TOPIC_BRIGHTNESS "brightness" 
#define DATA_TOPIC_ANGLE      "angle"
#define DATA_TOPIC_LASER      "laser"
#define DATA_TOPIC_ALL        "all"

// 主要接口函数
/**
 * @brief 初始化数据访问层
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_layer_init(void);

/**
 * @brief 反初始化数据访问层
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_layer_deinit(void);

/**
 * @brief 获取数据快照
 * @param snapshot 数据快照指针
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_get_snapshot(ui_data_snapshot_t *snapshot);

/**
 * @brief 设置屏幕亮度
 * @param brightness 亮度值 (0-255)
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_set_brightness(uint8_t brightness);

/**
 * @brief 获取当前亮度
 * @param brightness 亮度值指针
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_get_brightness(uint8_t *brightness);

/**
 * @brief 订阅数据更新
 * @param topic 数据主题
 * @param callback 回调函数
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_subscribe(const char *topic, ui_data_update_cb_t callback);

/**
 * @brief 取消订阅数据更新
 * @param topic 数据主题
 * @param callback 回调函数
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_unsubscribe(const char *topic, ui_data_update_cb_t callback);

/**
 * @brief 发布数据更新
 * @param topic 数据主题
 * @param data 数据指针
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_publish(const char *topic, const ui_data_snapshot_t *data);

/**
 * @brief 更新电池数据
 * @param voltage 电池电压
 * @param percentage 电池百分比
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_update_battery(float voltage, int percentage);

/**
 * @brief 更新角度数据
 * @param pitch 俯仰角
 * @param roll 翻滚角
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_update_angle(float pitch, float roll);

/**
 * @brief 更新激光距离数据
 * @param distance 激光距离
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_update_laser(float distance);

/**
 * @brief 获取数据更新统计
 * @param total_updates 总更新次数
 * @param error_count 错误次数
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_data_get_statistics(uint32_t *total_updates, uint32_t *error_count);

// 配置参数
#define UI_DATA_QUEUE_SIZE           10
#define UI_DATA_UPDATE_TIMEOUT_MS    500
#define UI_DATA_LOCK_TIMEOUT_MS      100
#define UI_DATA_MAX_TOPICS           8
#define UI_DATA_CACHE_VALID_MS       50

// 错误代码
#define ESP_ERR_UI_DATA_INVALID      (ESP_ERR_USER_BASE + 10)
#define ESP_ERR_UI_DATA_TIMEOUT      (ESP_ERR_USER_BASE + 11)
#define ESP_ERR_UI_DATA_FULL         (ESP_ERR_USER_BASE + 12)
#define ESP_ERR_UI_DATA_NOT_FOUND    (ESP_ERR_USER_BASE + 13)

#ifdef __cplusplus
}
#endif

#endif // UI_DATA_LAYER_H
