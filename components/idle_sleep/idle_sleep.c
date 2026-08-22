#include "idle_sleep.h"

#include "amoled_burn_protection.h"
#include "battery_protection.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <math.h>
#include <string.h>

static const char *TAG = "idle_sleep";

typedef enum {
    UI_NONE = 0,
    UI_COUNTDOWN,
} idle_ui_mode_t;

typedef struct {
    bool initialized;
    bool monitoring_active;
    bool shutting_down;
    idle_sleep_state_t current_state;

    TaskHandle_t monitor_task;
    lv_timer_t *ui_timer;

    idle_sleep_shutdown_callback_t shutdown_callback;

    int64_t last_activity_us;
    int motion_confirm_count;
    int countdown_seconds;

    volatile idle_ui_mode_t ui_mode;
    volatile int ui_countdown;
    volatile bool shutdown_now;

    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *detail_label;
    lv_obj_t *countdown_label;
    idle_ui_mode_t shown_mode;
    int shown_countdown;
} idle_sleep_context_t;

static idle_sleep_context_t g_ctx;

extern void shutdownDisplay(void);

static void monitor_task(void *arg);
static void ui_timer_cb(lv_timer_t *timer);
static void note_activity(void);
static void enter_countdown(void);
static void cancel_countdown(const char *reason);
static void request_ui(idle_ui_mode_t mode, int countdown);
static void destroy_ui_objects(void);
static void apply_ui(void);
static void emergency_shutdown(void);

static void note_activity(void)
{
    g_ctx.last_activity_us = esp_timer_get_time();
    g_ctx.motion_confirm_count = 0;

    if (g_ctx.current_state == IDLE_SLEEP_COUNTDOWN) {
        cancel_countdown("activity");
    }
}

static void request_ui(idle_ui_mode_t mode, int countdown)
{
    g_ctx.ui_mode = mode;
    g_ctx.ui_countdown = countdown;
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
    idle_ui_mode_t mode = g_ctx.ui_mode;
    int countdown = g_ctx.ui_countdown;

    if (mode == UI_NONE) {
        destroy_ui_objects();
        return;
    }

    if (g_ctx.shown_mode != mode) {
        destroy_ui_objects();
        amoled_burn_protection_reset_offset();

        g_ctx.root = lv_obj_create(lv_layer_top());
        lv_obj_set_size(g_ctx.root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(g_ctx.root, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(g_ctx.root, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_ctx.root, 0, 0);
        lv_obj_set_style_radius(g_ctx.root, 0, 0);
        lv_obj_clear_flag(g_ctx.root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(g_ctx.root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_ctx.root, LV_OBJ_FLAG_FLOATING);

        g_ctx.title_label = lv_label_create(g_ctx.root);
        lv_label_set_text(g_ctx.title_label, "Idle - sleeping");
        lv_obj_set_style_text_color(g_ctx.title_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(g_ctx.title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(g_ctx.title_label, &lv_font_montserrat_20, 0);
        lv_obj_align(g_ctx.title_label, LV_ALIGN_CENTER, 0, -60);

        g_ctx.countdown_label = lv_label_create(g_ctx.root);
        lv_label_set_text_fmt(g_ctx.countdown_label, "%d", countdown);
        lv_obj_set_style_text_color(g_ctx.countdown_label, lv_color_hex(0x7FDBFF), 0);
        lv_obj_set_style_text_font(g_ctx.countdown_label, &lv_font_montserrat_24, 0);
        lv_obj_align(g_ctx.countdown_label, LV_ALIGN_CENTER, 0, 0);

        g_ctx.detail_label = lv_label_create(g_ctx.root);
        lv_label_set_text(g_ctx.detail_label, "Press RESET to wake");
        lv_obj_set_style_text_color(g_ctx.detail_label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_align(g_ctx.detail_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(g_ctx.detail_label, &lv_font_montserrat_14, 0);
        lv_obj_align(g_ctx.detail_label, LV_ALIGN_CENTER, 0, 50);

        g_ctx.shown_mode = mode;
        g_ctx.shown_countdown = countdown;
        return;
    }

    if (g_ctx.countdown_label && g_ctx.shown_countdown != countdown) {
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

static void cancel_countdown(const char *reason)
{
    ESP_LOGI(TAG, "Countdown cancelled (%s)", reason);
    g_ctx.current_state = IDLE_SLEEP_ACTIVE;
    g_ctx.shutting_down = false;
    g_ctx.countdown_seconds = 0;
    g_ctx.last_activity_us = esp_timer_get_time();
    request_ui(UI_NONE, 0);
}

static void enter_countdown(void)
{
    if (g_ctx.current_state == IDLE_SLEEP_COUNTDOWN ||
        g_ctx.current_state == IDLE_SLEEP_SHUTDOWN) {
        return;
    }
    if (battery_protection_is_shutting_down()) {
        return;
    }

    ESP_LOGW(TAG, "Idle timeout — countdown %d s", IDLE_COUNTDOWN_SEC);
    g_ctx.current_state = IDLE_SLEEP_COUNTDOWN;
    g_ctx.shutting_down = true;
    g_ctx.countdown_seconds = IDLE_COUNTDOWN_SEC;
    request_ui(UI_COUNTDOWN, IDLE_COUNTDOWN_SEC);
}

static void emergency_shutdown(void)
{
    ESP_LOGW(TAG, "Idle deep sleep");
    g_ctx.current_state = IDLE_SLEEP_SHUTDOWN;
    g_ctx.monitoring_active = false;

    if (g_ctx.shutdown_callback) {
        g_ctx.shutdown_callback();
    }

    amoled_burn_protection_stop();
    shutdownDisplay();
    vTaskDelay(pdMS_TO_TICKS(200));
    fflush(stdout);
    esp_deep_sleep_start();
}

static void monitor_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Monitor task running (timeout %d min)",
             IDLE_SLEEP_TIMEOUT_MS / 60000);

    while (g_ctx.monitoring_active) {
        if (battery_protection_is_shutting_down()) {
            if (g_ctx.current_state == IDLE_SLEEP_COUNTDOWN) {
                cancel_countdown("battery shutdown");
            }
            vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_PERIOD_MS));
            continue;
        }

        if (g_ctx.current_state == IDLE_SLEEP_COUNTDOWN) {
            g_ctx.countdown_seconds--;
            request_ui(UI_COUNTDOWN, g_ctx.countdown_seconds);
            ESP_LOGW(TAG, "Idle sleep in %d s", g_ctx.countdown_seconds);

            if (g_ctx.countdown_seconds <= 0) {
                g_ctx.shutdown_now = true;
                while (g_ctx.monitoring_active) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int64_t idle_us = esp_timer_get_time() - g_ctx.last_activity_us;
        if (idle_us >= (int64_t)IDLE_SLEEP_TIMEOUT_MS * 1000LL) {
            enter_countdown();
        }

        vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_PERIOD_MS));
    }

    g_ctx.monitor_task = NULL;
    vTaskDelete(NULL);
}

void idle_sleep_on_activity(void)
{
    if (!g_ctx.initialized || !g_ctx.monitoring_active) {
        return;
    }
    note_activity();
}

void idle_sleep_feed_gyro(float gx, float gy, float gz)
{
    if (!g_ctx.initialized || !g_ctx.monitoring_active) {
        return;
    }
    if (battery_protection_is_shutting_down()) {
        return;
    }

    float omega = fabsf(gx) + fabsf(gy) + fabsf(gz);
    if (omega > GYRO_MOTION_DPS) {
        g_ctx.motion_confirm_count++;
        if (g_ctx.motion_confirm_count >= GYRO_MOTION_CONFIRM_FRAMES) {
            note_activity();
        }
    } else {
        g_ctx.motion_confirm_count = 0;
    }
}

esp_err_t idle_sleep_init(idle_sleep_shutdown_callback_t shutdown_callback)
{
    if (g_ctx.initialized) {
        return ESP_OK;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.shutdown_callback = shutdown_callback;
    g_ctx.current_state = IDLE_SLEEP_ACTIVE;
    g_ctx.last_activity_us = esp_timer_get_time();
    g_ctx.shown_countdown = -1;
    g_ctx.initialized = true;

    ESP_LOGI(TAG,
             "Init: timeout=%ds motion=%.1fdps confirm=%d countdown=%ds",
             IDLE_SLEEP_TIMEOUT_MS / 1000, GYRO_MOTION_DPS,
             GYRO_MOTION_CONFIRM_FRAMES, IDLE_COUNTDOWN_SEC);
    return ESP_OK;
}

esp_err_t idle_sleep_start(void)
{
    if (!g_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_ctx.monitoring_active) {
        return ESP_OK;
    }

    /* Caller must hold LVGL lock (non-recursive). */
    g_ctx.ui_timer = lv_timer_create(ui_timer_cb, IDLE_UI_POLL_MS, NULL);
    if (!g_ctx.ui_timer) {
        return ESP_ERR_NO_MEM;
    }

    g_ctx.last_activity_us = esp_timer_get_time();
    g_ctx.monitoring_active = true;

    BaseType_t ok = xTaskCreate(monitor_task, "idle_sleep", 4096, NULL, 2,
                                &g_ctx.monitor_task);
    if (ok != pdPASS) {
        lv_timer_del(g_ctx.ui_timer);
        g_ctx.ui_timer = NULL;
        g_ctx.monitoring_active = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Monitoring started");
    return ESP_OK;
}

esp_err_t idle_sleep_stop(void)
{
    if (!g_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_ctx.monitoring_active && !g_ctx.monitor_task) {
        return ESP_OK;
    }

    g_ctx.monitoring_active = false;
    for (int i = 0; i < 50 && g_ctx.monitor_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (g_ctx.ui_timer) {
        destroy_ui_objects();
        lv_timer_del(g_ctx.ui_timer);
        g_ctx.ui_timer = NULL;
    }

    g_ctx.current_state = IDLE_SLEEP_ACTIVE;
    g_ctx.shutting_down = false;
    request_ui(UI_NONE, 0);
    return ESP_OK;
}

idle_sleep_state_t idle_sleep_get_state(void)
{
    return g_ctx.current_state;
}

bool idle_sleep_is_shutting_down(void)
{
    return g_ctx.shutting_down;
}

esp_err_t idle_sleep_deinit(void)
{
    if (!g_ctx.initialized) {
        return ESP_OK;
    }
    idle_sleep_stop();
    memset(&g_ctx, 0, sizeof(g_ctx));
    return ESP_OK;
}
