#ifndef IDLE_SLEEP_H
#define IDLE_SLEEP_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDLE_SLEEP_TIMEOUT_MS           600000  /* 10 minutes */
#define IDLE_COUNTDOWN_SEC              5
#define GYRO_MOTION_DPS                 2.0f
#define GYRO_MOTION_CONFIRM_FRAMES      3
#define IDLE_UI_POLL_MS                 200
#define IDLE_CHECK_PERIOD_MS            1000

typedef enum {
    IDLE_SLEEP_ACTIVE = 0,
    IDLE_SLEEP_COUNTDOWN,
    IDLE_SLEEP_SHUTDOWN,
} idle_sleep_state_t;

typedef void (*idle_sleep_shutdown_callback_t)(void);

/**
 * @brief Initialize idle sleep. Call before start.
 * @param shutdown_callback Optional cleanup before deep sleep (may be NULL).
 * @note start() must be called while holding the LVGL lock (creates lv_timer).
 */
esp_err_t idle_sleep_init(idle_sleep_shutdown_callback_t shutdown_callback);
esp_err_t idle_sleep_start(void);
esp_err_t idle_sleep_stop(void);

/** Touch / laser / any explicit user action — resets idle timer; cancels countdown. */
void idle_sleep_on_activity(void);

/** Feed gyro sample (dps). Motion above threshold resets idle timer. */
void idle_sleep_feed_gyro(float gx, float gy, float gz);

idle_sleep_state_t idle_sleep_get_state(void);
bool idle_sleep_is_shutting_down(void);
esp_err_t idle_sleep_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* IDLE_SLEEP_H */
