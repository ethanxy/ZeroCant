#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AMOLED 防烧屏：周期像素微移（刷屏偏移 + 触摸同步）
 *
 * 偏移限制为偶数像素，避免 SH8601 + LVGL 分条缓冲在奇数平移时出现接缝黑线。
 */
typedef struct {
    uint32_t offset_interval_ms;    /**< 偏移间隔，默认 300000ms（5 分钟） */
    uint8_t max_offset_pixels;      /**< 最大偏移（偶数），默认 2 */
    bool enable_random_offset;      /**< true=随机；false=固定环绕（默认） */
    /** 偏移更新后回调（用于整屏 invalidate）；可 NULL */
    void (*on_offset_changed)(void);
} amoled_burn_protection_config_t;

esp_err_t amoled_burn_protection_init(const amoled_burn_protection_config_t *config);
esp_err_t amoled_burn_protection_start(void);
esp_err_t amoled_burn_protection_stop(void);
esp_err_t amoled_burn_protection_reset_offset(void);
esp_err_t amoled_burn_protection_get_offset(int8_t *offset_x, int8_t *offset_y);

/**
 * @brief 将物理触摸坐标映射到逻辑 UI 坐标（减去当前像素偏移）
 */
void amoled_burn_protection_map_touch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
