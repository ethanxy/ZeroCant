#include "amoled_burn_protection.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "amoled_burn_protection";

typedef struct {
    amoled_burn_protection_config_t config;
    esp_timer_handle_t timer_handle;
    int8_t current_offset_x;
    int8_t current_offset_y;
    uint8_t pattern_step;
    bool is_running;
    bool is_initialized;
} amoled_burn_protection_context_t;

static amoled_burn_protection_context_t g_context = {0};

/*
 * 默认 5 分钟、最大 2px。
 * 偏移必须为偶数：SH8601 + LVGL 1/4 高缓冲在奇数平移时会产生水平接缝黑线。
 */
static const amoled_burn_protection_config_t default_config = {
    .offset_interval_ms = 300000,
    .max_offset_pixels = 2,
    .enable_random_offset = false,
    .on_offset_changed = NULL,
};

/* 单位步长，实际偏移 = step * max_offset（默认即 0/±2） */
static const int8_t k_patterns[][2] = {
    {0, 0},  {1, 0},  {1, 1},
    {0, 1},  {-1, 1}, {-1, 0},
    {-1, -1}, {0, -1}, {1, -1},
};

static int8_t snap_even_offset(int8_t v)
{
    /* 向 0 收成偶数，避免 ±1 这类奇数平移 */
    if (v >= 0) {
        return (int8_t)(v & ~1);
    }
    return (int8_t)(-((-v) & ~1));
}

static void generate_next_offset(int8_t *offset_x, int8_t *offset_y)
{
    uint8_t max_px = g_context.config.max_offset_pixels ?
                     g_context.config.max_offset_pixels : 2;
    /* 至少 2，保证能落到非零偶数 */
    if (max_px < 2) {
        max_px = 2;
    }
    max_px = (uint8_t)snap_even_offset((int8_t)max_px);
    if (max_px == 0) {
        max_px = 2;
    }

    if (g_context.config.enable_random_offset) {
        int8_t nx = (int8_t)((rand() % (max_px * 2 + 1)) - max_px);
        int8_t ny = (int8_t)((rand() % (max_px * 2 + 1)) - max_px);
        *offset_x = snap_even_offset(nx);
        *offset_y = snap_even_offset(ny);
        return;
    }

    const uint8_t count = (uint8_t)(sizeof(k_patterns) / sizeof(k_patterns[0]));
    g_context.pattern_step = (uint8_t)((g_context.pattern_step + 1) % count);
    /* patterns are -1/0/1；乘 max_px(2) → -2/0/2（偶数，避免缓冲条带接缝） */
    *offset_x = (int8_t)(k_patterns[g_context.pattern_step][0] * (int8_t)max_px);
    *offset_y = (int8_t)(k_patterns[g_context.pattern_step][1] * (int8_t)max_px);
}

static void burn_protection_timer_callback(void *arg)
{
    (void)arg;
    if (!g_context.is_running) {
        return;
    }

    int8_t nx = 0;
    int8_t ny = 0;
    generate_next_offset(&nx, &ny);
    g_context.current_offset_x = nx;
    g_context.current_offset_y = ny;
    ESP_LOGI(TAG, "Pixel shift -> (%d, %d)", (int)nx, (int)ny);

    if (g_context.config.on_offset_changed) {
        g_context.config.on_offset_changed();
    }
}

esp_err_t amoled_burn_protection_init(const amoled_burn_protection_config_t *config)
{
    if (g_context.is_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (config) {
        memcpy(&g_context.config, config, sizeof(g_context.config));
    } else {
        memcpy(&g_context.config, &default_config, sizeof(g_context.config));
    }

    if (g_context.config.offset_interval_ms < 1000) {
        g_context.config.offset_interval_ms = 1000;
    }
    if (g_context.config.max_offset_pixels < 2) {
        g_context.config.max_offset_pixels = 2;
    }
    g_context.config.max_offset_pixels =
        (uint8_t)snap_even_offset((int8_t)g_context.config.max_offset_pixels);
    if (g_context.config.max_offset_pixels == 0) {
        g_context.config.max_offset_pixels = 2;
    }

    esp_timer_create_args_t timer_args = {
        .callback = burn_protection_timer_callback,
        .arg = NULL,
        .name = "amoled_burn_shift"
    };

    esp_err_t ret = esp_timer_create(&timer_args, &g_context.timer_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Create timer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_context.current_offset_x = 0;
    g_context.current_offset_y = 0;
    g_context.pattern_step = 0;
    g_context.is_running = false;
    g_context.is_initialized = true;

    ESP_LOGI(TAG, "Init OK (interval=%lums, max=%upx even-only, random=%s)",
             (unsigned long)g_context.config.offset_interval_ms,
             (unsigned)g_context.config.max_offset_pixels,
             g_context.config.enable_random_offset ? "yes" : "no");
    return ESP_OK;
}

esp_err_t amoled_burn_protection_start(void)
{
    if (!g_context.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_context.is_running) {
        return ESP_OK;
    }

    esp_err_t ret = esp_timer_start_periodic(
        g_context.timer_handle,
        (uint64_t)g_context.config.offset_interval_ms * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start timer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_context.is_running = true;
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

esp_err_t amoled_burn_protection_stop(void)
{
    if (!g_context.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_context.is_running) {
        return ESP_OK;
    }

    esp_timer_stop(g_context.timer_handle);
    g_context.is_running = false;
    amoled_burn_protection_reset_offset();
    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}

esp_err_t amoled_burn_protection_reset_offset(void)
{
    if (!g_context.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    g_context.current_offset_x = 0;
    g_context.current_offset_y = 0;
    g_context.pattern_step = 0;
    return ESP_OK;
}

esp_err_t amoled_burn_protection_get_offset(int8_t *offset_x, int8_t *offset_y)
{
    if (!g_context.is_initialized) {
        if (offset_x) {
            *offset_x = 0;
        }
        if (offset_y) {
            *offset_y = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (offset_x) {
        *offset_x = g_context.current_offset_x;
    }
    if (offset_y) {
        *offset_y = g_context.current_offset_y;
    }
    return ESP_OK;
}

void amoled_burn_protection_map_touch(uint16_t *x, uint16_t *y)
{
    if (!g_context.is_initialized) {
        return;
    }

    int8_t ox = g_context.current_offset_x;
    int8_t oy = g_context.current_offset_y;
    if (ox == 0 && oy == 0) {
        return;
    }

    if (x) {
        int32_t nx = (int32_t)(*x) - (int32_t)ox;
        if (nx < 0) {
            nx = 0;
        }
        *x = (uint16_t)nx;
    }
    if (y) {
        int32_t ny = (int32_t)(*y) - (int32_t)oy;
        if (ny < 0) {
            ny = 0;
        }
        *y = (uint16_t)ny;
    }
}
