#include "ballistic_profile.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "bal_profile";
static const char *NVS_NS = "ballistic";
static const char *NVS_KEY = "profile";

/* Defaults: 68gr G1 BC 0.355 @ 2775 fps, 100 m zero, 1.5 in sight (.223 dia) */
static const ballistic_profile_t s_factory_defaults = {
    .name = "68gr G1",
    .bullet_weight_gr = 68.0f,
    .muzzle_velocity_fps = 2775.0f,
    .bc = 0.355f,
    .drag_model = BALLISTIC_DRAG_G1,
    .zero_range_m = 100.0f,
    .sight_height_m = 0.0381f,      /* 1.5 inches — typical scope; edit if needed */
    .bullet_diameter_in = 0.224f,   /* .223 / 5.56 */
    .temp_sens_enabled = false,
    .wind_fps = 0.0f,
};

static ballistic_profile_t s_profile;
static bool s_inited = false;

void ballistic_profile_refresh_name(void) {
    const char *model = (s_profile.drag_model == BALLISTIC_DRAG_G7) ? "G7" : "G1";
    snprintf(s_profile.name, sizeof(s_profile.name), "%.0fgr %s",
             s_profile.bullet_weight_gr, model);
}

void ballistic_profile_reset_defaults(void) {
    s_profile = s_factory_defaults;
    ballistic_profile_refresh_name();
}

esp_err_t ballistic_profile_init(void) {
    ballistic_profile_reset_defaults();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = sizeof(s_profile);
        err = nvs_get_blob(h, NVS_KEY, &s_profile, &len);
        nvs_close(h);
        if (err == ESP_OK && len == sizeof(s_profile)) {
            /* Sanitize */
            if (s_profile.bullet_weight_gr < 20.0f || s_profile.bullet_weight_gr > 300.0f ||
                s_profile.bc < 0.05f || s_profile.bc > 1.5f ||
                s_profile.muzzle_velocity_fps < 500.0f ||
                s_profile.zero_range_m < 10.0f ||
                s_profile.sight_height_m < 0.005f ||
                s_profile.bullet_diameter_in < 0.15f) {
                ESP_LOGW(TAG, "NVS profile invalid, using defaults");
                ballistic_profile_reset_defaults();
            } else {
                if (s_profile.drag_model != BALLISTIC_DRAG_G7) {
                    s_profile.drag_model = BALLISTIC_DRAG_G1;
                }
                s_profile.temp_sens_enabled = false;
                s_profile.wind_fps = 0.0f;
                ballistic_profile_refresh_name();
                ESP_LOGI(TAG, "Loaded profile from NVS: %s", s_profile.name);
            }
        } else {
            ballistic_profile_reset_defaults();
            ESP_LOGI(TAG, "No NVS profile, using defaults: %s", s_profile.name);
        }
    } else {
        ESP_LOGI(TAG, "NVS open failed (%s), using defaults", esp_err_to_name(err));
    }

    s_inited = true;
    return ESP_OK;
}

const ballistic_profile_t *ballistic_profile_get(void) {
    if (!s_inited) {
        ballistic_profile_init();
    }
    return &s_profile;
}

ballistic_profile_t *ballistic_profile_get_mut(void) {
    if (!s_inited) {
        ballistic_profile_init();
    }
    return &s_profile;
}

esp_err_t ballistic_profile_save(void) {
    if (!s_inited) {
        ballistic_profile_init();
    }
    ballistic_profile_refresh_name();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, &s_profile, sizeof(s_profile));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved profile: %s BC=%.3f MV=%.0f zero=%.0fm",
                 s_profile.name, s_profile.bc, s_profile.muzzle_velocity_fps,
                 s_profile.zero_range_m);
    }
    return err;
}
