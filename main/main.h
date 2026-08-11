#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lock LVGL mutex
 * @param timeout_ms Timeout in milliseconds, -1 for infinite wait
 * @return true if lock acquired successfully, false otherwise
 */
bool example_lvgl_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 */
void example_lvgl_unlock(void);

/**
 * @brief Shutdown display hardware for deep sleep
 */
void shutdownDisplay(void);

/**
 * @brief Clear physical screen edge bands left by AMOLED pixel-shift
 */
void display_clear_burn_edges(void);

#ifdef __cplusplus
}
#endif
