#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AMOLED防烧屏组件配置结构体
 */
typedef struct {
    uint32_t offset_interval_ms;    ///< 偏移间隔时间(毫秒), 默认30000ms
    uint8_t max_offset_pixels;      ///< 最大偏移像素数, 默认2像素
    bool enable_random_offset;      ///< 是否启用随机偏移模式, 默认true
    lv_disp_t* target_display;      ///< 目标显示器，NULL为默认显示器
} amoled_burn_protection_config_t;

/**
 * @brief 初始化AMOLED防烧屏保护
 * 
 * @param config 配置参数，NULL使用默认配置
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t amoled_burn_protection_init(const amoled_burn_protection_config_t* config);

/**
 * @brief 启动防烧屏保护
 * 
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t amoled_burn_protection_start(void);

/**
 * @brief 停止防烧屏保护
 * 
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t amoled_burn_protection_stop(void);

/**
 * @brief 重置显示偏移到原点
 * 
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t amoled_burn_protection_reset_offset(void);

/**
 * @brief 获取当前偏移状态
 * 
 * @param offset_x 当前X轴偏移像素数
 * @param offset_y 当前Y轴偏移像素数
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t amoled_burn_protection_get_offset(int8_t* offset_x, int8_t* offset_y);

#ifdef __cplusplus
}
#endif
