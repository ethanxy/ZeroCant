#include "battery_protection.h"

#include "adc_bsp.h"
#include "amoled_burn_protection.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>

static const char *TAG = "battery_protection";

#define LASER_POWER_GPIO 45

typedef enum {
    UI_NONE = 0,
    UI_WARN_BANNER,
    UI_SHUTDOWN_FULL,
} battery_ui_mode_t;

typedef struct {
    bool initialized;
    bool monitoring_active;
    bool shutting_down;
    battery_protection_state_t current_state;

    TaskHandle_t monitor_task;
    lv_timer_t *ui_timer;

    battery_shutdown_callback_t shutdown_callback;

    int warn_confirm_count;
    int force_confirm_count;
    int64_t warn_enter_us;

    int countdown_seconds;
    float last_voltage;

    volatile battery_ui_mode_t ui_mode;
    volatile int ui_countdown;
    volatile float ui_voltage;
    volatile bool ui_dim_requested;
    volatile bool shutdown_now;

    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *detail_label;
    lv_obj_t *countdown_label;
    battery_ui_mode_t shown_mode;
    int shown_countdown;
} battery_protection_context_t;

static battery_protection_context_t g_ctx;

extern void setBrightnessTemporary(uint8_t brig);
extern void shutdownDisplay(void);

static void monitor_task(void *arg);
static void ui_timer_cb(lv_timer_t *timer);
static float read_battery_avg(void);
static void enter_warning(float voltage);
static void enter_shutdown(float voltage);
static void return_to_normal(const char *reason);
static void request_ui(battery_ui_mode_t mode, float voltage, int countdown);
static void destroy_ui_objects(void);
static void apply_ui(void);
static void shutdown_peripherals(void);
static void emergency_shutdown(void);
static void process_voltage(float voltage);

static float read_battery_avg(void)
{
    float sum = 0.0f;
    int ok = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        float v = 0.0f;
        adc_get_value(&v);
        if (v > 0.5f) {
            sum += v;
            ok++;
        }
        if (i + 1 < BATTERY_SAMPLE_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    if (ok == 0) {
        return g_ctx.last_voltage;
    }
    return sum / (float)ok;
}

static void request_ui(battery_ui_mode_t mode, float voltage, int countdown)
{
    g_ctx.ui_mode = mode;
    g_ctx.ui_voltage = voltage;
    g_ctx.ui_countdown = countdown;
}

static void shutdown_peripherals(void)
{
    gpio_set_level(LASER_POWER_GPIO, 0);
    ESP_LOGI(TAG, "Laser power disabled (GPIO%d)", LASER_POWER_GPIO);
}

static void destroy_ui_objects(void)
{
    if (g_ctx.root) {
        lv_obj_del(g_ctx.root);
        g_ctx.root = NULL;
        g_ctx.title_label = NULL;
        g_ctx.detail_label = NULL;
        g_ctx.countdown_label = NULL;
    }
    g_ctx.shown_mode = UI_NONE;
    g_ctx.shown_countdown = -1;
}

static void apply_ui(void)
{
    battery_ui_mode_t mode = g_ctx.ui_mode;
    float voltage = g_ctx.ui_voltage;
    int countdown = g_ctx.ui_countdown;

    if (g_ctx.ui_dim_requested) {
        uint8_t low = (uint8_t)(255 * BATTERY_LOW_BRIGHTNESS_PERCENT / 100);
        setBrightnessTemporary(low);
        g_ctx.ui_dim_requested = false;
    }

    if (mode == UI_NONE) {
        destroy_ui_objects();
        return;
    }

    if (g_ctx.shown_mode != mode) {
        destroy_ui_objects();

        if (mode == UI_WARN_BANNER) {
            g_ctx.root = lv_obj_create(lv_layer_top());
            lv_obj_set_size(g_ctx.root, LV_PCT(100), 56);
            lv_obj_align(g_ctx.root, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_style_bg_color(g_ctx.root, lv_color_hex(0x8B0000), 0);
            lv_obj_set_style_bg_opa(g_ctx.root, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_ctx.root, 0, 0);
            lv_obj_set_style_radius(g_ctx.root, 0, 0);
            lv_obj_clear_flag(g_ctx.root, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(g_ctx.root, LV_OBJ_FLAG_FLOATING);

            g_ctx.title_label = lv_label_create(g_ctx.root);
            lv_label_set_text(g_ctx.title_label, "Low battery");
            lv_obj_set_style_text_color(g_ctx.title_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(g_ctx.title_label, &lv_font_montserrat_16, 0);
            lv_obj_align(g_ctx.title_label, LV_ALIGN_LEFT_MID, 12, -8);

            g_ctx.detail_label = lv_label_create(g_ctx.root);
            lv_label_set_text_fmt(g_ctx.detail_label, "%.2f V", voltage);
            lv_obj_set_style_text_color(g_ctx.detail_label, lv_color_hex(0xFFD700), 0);
            lv_obj_set_style_text_font(g_ctx.detail_label, &lv_font_montserrat_14, 0);
            lv_obj_align(g_ctx.detail_label, LV_ALIGN_LEFT_MID, 12, 12);
        } else if (mode == UI_SHUTDOWN_FULL) {
            amoled_burn_protection_reset_offset();

            g_ctx.root = lv_obj_create(lv_layer_top());
            lv_obj_set_size(g_ctx.root, LV_PCT(100), LV_PCT(100));
            lv_obj_set_style_bg_color(g_ctx.root, lv_color_hex(0x600000), 0);
            lv_obj_set_style_bg_opa(g_ctx.root, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_ctx.root, 0, 0);
            lv_obj_set_style_radius(g_ctx.root, 0, 0);
            lv_obj_clear_flag(g_ctx.root, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(g_ctx.root, LV_OBJ_FLAG_FLOATING);

            g_ctx.title_label = lv_label_create(g_ctx.root);
            lv_label_set_text(g_ctx.title_label, "Battery low - shutting down");
            lv_obj_set_style_text_color(g_ctx.title_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_align(g_ctx.title_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(g_ctx.title_label, &lv_font_montserrat_20, 0);
            lv_obj_align(g_ctx.title_label, LV_ALIGN_CENTER, 0, -60);

            g_ctx.countdown_label = lv_label_create(g_ctx.root);
            lv_label_set_text_fmt(g_ctx.countdown_label, "%d", countdown);
            lv_obj_set_style_text_color(g_ctx.countdown_label, lv_color_hex(0xFFFF00), 0);
            lv_obj_set_style_text_font(g_ctx.countdown_label, &lv_font_montserrat_24, 0);
            lv_obj_align(g_ctx.countdown_label, LV_ALIGN_CENTER, 0, 0);

            g_ctx.detail_label = lv_label_create(g_ctx.root);
            lv_label_set_text_fmt(g_ctx.detail_label, "%.2f V\nPress RESET to wake", voltage);
            lv_obj_set_style_text_color(g_ctx.detail_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_align(g_ctx.detail_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(g_ctx.detail_label, &lv_font_montserrat_14, 0);
            lv_obj_align(g_ctx.detail_label, LV_ALIGN_CENTER, 0, 60);
        }

        g_ctx.shown_mode = mode;
        g_ctx.shown_countdown = countdown;
        return;
    }

    if (g_ctx.detail_label) {
        if (mode == UI_WARN_BANNER) {
            lv_label_set_text_fmt(g_ctx.detail_label, "%.2f V", voltage);
        } else if (mode == UI_SHUTDOWN_FULL) {
            lv_label_set_text_fmt(g_ctx.detail_label, "%.2f V\nPress RESET to wake", voltage);
        }
    }
    if (mode == UI_SHUTDOWN_FULL && g_ctx.countdown_label &&
        g_ctx.shown_countdown != countdown) {
        lv_label_set_text_fmt(g_ctx.countdown_label, "%d", countdown);
        g_ctx.shown_countdown = countdown;
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (g_ctx.shutdown_now) {
        g_ctx.shutdown_now = false;
        destroy_ui_objects();
        emergency_shutdown();
        return;
    }

    apply_ui();
}

static void return_to_normal(const char *reason)
{
    ESP_LOGI(TAG, "Recovered to NORMAL (%s), V=%.2f", reason, g_ctx.last_voltage);

    g_ctx.current_state = BATTERY_PROTECTION_NORMAL;
    g_ctx.warn_confirm_count = 0;
    g_ctx.force_confirm_count = 0;
    g_ctx.warn_enter_us = 0;
    g_ctx.countdown_seconds = 0;
    g_ctx.shutting_down = false;
    g_ctx.ui_dim_requested = false;
    request_ui(UI_NONE, g_ctx.last_voltage, 0);
}

static void enter_warning(float voltage)
{
    if (g_ctx.current_state == BATTERY_PROTECTION_WARNING) {
        request_ui(UI_WARN_BANNER, voltage, 0);
        return;
    }

    ESP_LOGW(TAG, "Enter WARNING (%.2f V)", voltage);
    g_ctx.current_state = BATTERY_PROTECTION_WARNING;
    g_ctx.warn_enter_us = esp_timer_get_time();
    g_ctx.force_confirm_count = 0;
    request_ui(UI_WARN_BANNER, voltage, 0);
}

static void enter_shutdown(float voltage)
{
    if (g_ctx.current_state == BATTERY_PROTECTION_SHUTDOWN) {
        return;
    }

    int delay_sec = (voltage <= BATTERY_URGENT_VOLTAGE)
                        ? BATTERY_URGENT_SHUTDOWN_SEC
                        : BATTERY_SHUTDOWN_DELAY_SEC;

    ESP_LOGW(TAG, "Enter SHUTDOWN countdown %ds (%.2f V)", delay_sec, voltage);

    g_ctx.current_state = BATTERY_PROTECTION_SHUTDOWN;
    g_ctx.shutting_down = true;
    g_ctx.countdown_seconds = delay_sec;
    g_ctx.ui_dim_requested = true;

    shutdown_peripherals();
    request_ui(UI_SHUTDOWN_FULL, voltage, delay_sec);
}

static void process_voltage(float voltage)
{
    g_ctx.last_voltage = voltage;
    ESP_LOGI(TAG, "Battery %.2f V (state=%d)", voltage, (int)g_ctx.current_state);

    if (g_ctx.current_state == BATTERY_PROTECTION_SHUTDOWN) {
        if (voltage >= BATTERY_RECOVER_VOLTAGE) {
            return_to_normal("voltage recovered during countdown");
        }
        return;
    }

    if (voltage >= BATTERY_RECOVER_VOLTAGE) {
        g_ctx.warn_confirm_count = 0;
        g_ctx.force_confirm_count = 0;
        if (g_ctx.current_state == BATTERY_PROTECTION_WARNING) {
            return_to_normal("voltage recovered");
        }
        return;
    }

    if (voltage <= BATTERY_FORCE_VOLTAGE) {
        g_ctx.force_confirm_count++;
        g_ctx.warn_confirm_count++;
    } else if (voltage <= BATTERY_WARN_VOLTAGE) {
        g_ctx.force_confirm_count = 0;
        g_ctx.warn_confirm_count++;
    } else {
        g_ctx.force_confirm_count = 0;
        if (g_ctx.warn_confirm_count > 0) {
            g_ctx.warn_confirm_count--;
        }
        if (g_ctx.current_state == BATTERY_PROTECTION_WARNING) {
            request_ui(UI_WARN_BANNER, voltage, 0);
        }
        return;
    }

    if (g_ctx.force_confirm_count >= BATTERY_CONFIRM_COUNT) {
        enter_shutdown(voltage);
        return;
    }

    if (g_ctx.current_state == BATTERY_PROTECTION_WARNING) {
        int64_t elapsed_ms =
            (esp_timer_get_time() - g_ctx.warn_enter_us) / 1000;
        if (elapsed_ms >= BATTERY_WARN_MAX_DURATION_MS &&
            voltage <= BATTERY_WARN_VOLTAGE) {
            enter_shutdown(voltage);
            return;
        }
        request_ui(UI_WARN_BANNER, voltage, 0);
        return;
    }

    if (g_ctx.warn_confirm_count >= BATTERY_CONFIRM_COUNT) {
        enter_warning(voltage);
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Monitor task running");

    /* First sample shortly after start */
    vTaskDelay(pdMS_TO_TICKS(500));

    while (g_ctx.monitoring_active) {
        if (g_ctx.current_state == BATTERY_PROTECTION_SHUTDOWN) {
            float voltage = read_battery_avg();
            g_ctx.last_voltage = voltage;

            if (voltage >= BATTERY_RECOVER_VOLTAGE) {
                return_to_normal("voltage recovered in countdown");
                continue;
            }

            g_ctx.countdown_seconds--;
            request_ui(UI_SHUTDOWN_FULL, voltage, g_ctx.countdown_seconds);
            ESP_LOGW(TAG, "Shutdown in %d s (%.2f V)", g_ctx.countdown_seconds, voltage);

            if (g_ctx.countdown_seconds <= 0) {
                g_ctx.shutdown_now = true;
                /* Wait for LVGL timer to run emergency_shutdown */
                while (g_ctx.monitoring_active) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        process_voltage(read_battery_avg());
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }

    g_ctx.monitor_task = NULL;
    vTaskDelete(NULL);
}

static void emergency_shutdown(void)
{
    ESP_LOGW(TAG, "Emergency deep sleep");

    g_ctx.monitoring_active = false;

    if (g_ctx.shutdown_callback) {
        g_ctx.shutdown_callback();
    }

    amoled_burn_protection_stop();
    shutdown_peripherals();
    shutdownDisplay();
    vTaskDelay(pdMS_TO_TICKS(200));
    fflush(stdout);
    esp_deep_sleep_start();
}

esp_err_t battery_protection_init(battery_shutdown_callback_t shutdown_callback)
{
    if (g_ctx.initialized) {
        return ESP_OK;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.shutdown_callback = shutdown_callback;
    g_ctx.current_state = BATTERY_PROTECTION_NORMAL;
    g_ctx.shown_countdown = -1;
    g_ctx.initialized = true;

    ESP_LOGI(TAG,
             "Init: warn=%.2f force=%.2f recover=%.2f check=%ds confirm=%d",
             BATTERY_WARN_VOLTAGE, BATTERY_FORCE_VOLTAGE, BATTERY_RECOVER_VOLTAGE,
             BATTERY_CHECK_INTERVAL_MS / 1000, BATTERY_CONFIRM_COUNT);
    return ESP_OK;
}

esp_err_t battery_protection_start(void)
{
    if (!g_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_ctx.monitoring_active) {
        return ESP_OK;
    }

    /*
     * Caller must hold the LVGL lock (non-recursive) when creating the UI timer.
     */
    g_ctx.ui_timer = lv_timer_create(ui_timer_cb, BATTERY_UI_POLL_MS, NULL);
    if (!g_ctx.ui_timer) {
        return ESP_ERR_NO_MEM;
    }

    g_ctx.monitoring_active = true;

    BaseType_t ok = xTaskCreate(monitor_task, "batt_protect", 4096, NULL, 2,
                                &g_ctx.monitor_task);
    if (ok != pdPASS) {
        lv_timer_del(g_ctx.ui_timer);
        g_ctx.ui_timer = NULL;
        g_ctx.monitoring_active = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Monitoring started (every %d s)", BATTERY_CHECK_INTERVAL_MS / 1000);
    return ESP_OK;
}

esp_err_t battery_protection_stop(void)
{
    if (!g_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_ctx.monitoring_active && !g_ctx.monitor_task) {
        return ESP_OK;
    }

    g_ctx.monitoring_active = false;

    /* Give monitor task a moment to exit */
    for (int i = 0; i < 50 && g_ctx.monitor_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (g_ctx.ui_timer) {
        destroy_ui_objects();
        lv_timer_del(g_ctx.ui_timer);
        g_ctx.ui_timer = NULL;
    }

    g_ctx.current_state = BATTERY_PROTECTION_NORMAL;
    g_ctx.shutting_down = false;
    request_ui(UI_NONE, 0, 0);
    return ESP_OK;
}

esp_err_t battery_protection_check_now(void)
{
    if (!g_ctx.initialized || !g_ctx.monitoring_active) {
        return ESP_ERR_INVALID_STATE;
    }
    process_voltage(read_battery_avg());
    return ESP_OK;
}

battery_protection_state_t battery_protection_get_state(void)
{
    return g_ctx.current_state;
}

float battery_protection_get_last_voltage(void)
{
    return g_ctx.last_voltage;
}

bool battery_protection_is_shutting_down(void)
{
    return g_ctx.shutting_down;
}

esp_err_t battery_protection_deinit(void)
{
    if (!g_ctx.initialized) {
        return ESP_OK;
    }

    battery_protection_stop();
    memset(&g_ctx, 0, sizeof(g_ctx));
    return ESP_OK;
}
