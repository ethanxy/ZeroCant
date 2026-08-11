#ifndef BATTERY_PROTECTION_H
#define BATTERY_PROTECTION_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Voltage thresholds (V) */
#define BATTERY_WARN_VOLTAGE            3.40f
#define BATTERY_FORCE_VOLTAGE           3.35f
#define BATTERY_RECOVER_VOLTAGE         3.50f
#define BATTERY_URGENT_VOLTAGE          3.30f

/* Timing */
#define BATTERY_CHECK_INTERVAL_MS       60000
#define BATTERY_WARN_MAX_DURATION_MS    120000
#define BATTERY_SHUTDOWN_DELAY_SEC      10
#define BATTERY_URGENT_SHUTDOWN_SEC     5
#define BATTERY_CONFIRM_COUNT           2
#define BATTERY_SAMPLE_COUNT            5
#define BATTERY_UI_POLL_MS              200
#define BATTERY_LOW_BRIGHTNESS_PERCENT  10

typedef enum {
    BATTERY_PROTECTION_NORMAL = 0,
    BATTERY_PROTECTION_WARNING,
    BATTERY_PROTECTION_SHUTDOWN,
} battery_protection_state_t;

typedef void (*battery_shutdown_callback_t)(void);

esp_err_t battery_protection_init(battery_shutdown_callback_t shutdown_callback);
esp_err_t battery_protection_start(void);
esp_err_t battery_protection_stop(void);
esp_err_t battery_protection_check_now(void);
battery_protection_state_t battery_protection_get_state(void);
float battery_protection_get_last_voltage(void);
bool battery_protection_is_shutting_down(void);
esp_err_t battery_protection_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_PROTECTION_H */
