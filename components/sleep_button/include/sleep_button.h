#ifndef SLEEP_BUTTON_H
#define SLEEP_BUTTON_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化深度休眠按键
 * @return esp_err_t ESP_OK on success
 */
esp_err_t sleep_button_init(void);

/**
 * @brief 进入深度休眠模式
 */
void sleep_button_enter_deep_sleep(void);

/**
 * @brief 检查唤醒原因并处理
 */
void sleep_button_check_wakeup_reason(void);

#ifdef __cplusplus
}
#endif

#endif // SLEEP_BUTTON_H
