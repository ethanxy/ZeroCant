/**
 * @file ballistic_holdover.c
 * @brief Slim point-mass RK4 holdover solver (G1 / G7)
 */

#include "ballistic_calc.h"
#include "ballistic_profile.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "ballistic";

#define GRAVITY_MPS2          9.80665f
#define FPS_TO_MPS            0.3048f
#define M_TO_IN               39.3700787f
#define STANDARD_DENSITY      1.225f   /* kg/m³ sea level */
#define SPEED_OF_SOUND_MPS    340.29f
#define MAX_SIM_TIME_S        8.0f
#define DT_S                  0.002f
#define MIN_DISTANCE_M        1.0f
#define MAX_DISTANCE_M        1500.0f

typedef struct {
    float mach;
    float cd;
} drag_entry_t;

static const drag_entry_t s_g1_table[] = {
    {0.00f, 0.2629f}, {0.05f, 0.2558f}, {0.10f, 0.2487f}, {0.15f, 0.2413f},
    {0.20f, 0.2344f}, {0.25f, 0.2278f}, {0.30f, 0.2214f}, {0.35f, 0.2155f},
    {0.40f, 0.2104f}, {0.45f, 0.2061f}, {0.50f, 0.2032f}, {0.55f, 0.2020f},
    {0.60f, 0.2034f}, {0.65f, 0.2165f}, {0.70f, 0.2230f}, {0.75f, 0.2313f},
    {0.80f, 0.2417f}, {0.85f, 0.2546f}, {0.90f, 0.2706f}, {0.95f, 0.2901f},
    {1.00f, 0.3136f}, {1.05f, 0.3415f}, {1.10f, 0.3734f}, {1.15f, 0.4084f},
    {1.20f, 0.4448f}, {1.25f, 0.4805f}, {1.30f, 0.5136f}, {1.35f, 0.5427f},
    {1.40f, 0.5677f}, {1.45f, 0.5883f}, {1.50f, 0.6053f}, {1.55f, 0.6191f},
    {1.60f, 0.6393f}, {1.65f, 0.6518f}, {1.70f, 0.6589f}, {1.75f, 0.6621f},
    {1.80f, 0.6625f}, {1.85f, 0.6607f}, {1.90f, 0.6573f}, {1.95f, 0.6528f},
    {2.00f, 0.6474f}, {2.10f, 0.6347f}, {2.20f, 0.6210f}, {2.30f, 0.6072f},
    {2.40f, 0.5934f}, {2.50f, 0.5804f}, {2.60f, 0.5685f}, {2.80f, 0.5467f},
    {3.00f, 0.5274f}, {3.50f, 0.4889f}, {4.00f, 0.4615f}, {5.00f, 0.4061f},
};

static const drag_entry_t s_g7_table[] = {
    {0.00f, 0.1198f}, {0.05f, 0.1197f}, {0.10f, 0.1196f}, {0.15f, 0.1194f},
    {0.20f, 0.1193f}, {0.25f, 0.1194f}, {0.30f, 0.1194f}, {0.35f, 0.1193f},
    {0.40f, 0.1193f}, {0.45f, 0.1193f}, {0.50f, 0.1194f}, {0.55f, 0.1193f},
    {0.60f, 0.1194f}, {0.65f, 0.1196f}, {0.70f, 0.1200f}, {0.75f, 0.1211f},
    {0.80f, 0.1230f}, {0.85f, 0.1263f}, {0.90f, 0.1318f}, {0.95f, 0.1412f},
    {1.00f, 0.1550f}, {1.05f, 0.1755f}, {1.10f, 0.2000f}, {1.15f, 0.2220f},
    {1.20f, 0.2400f}, {1.25f, 0.2530f}, {1.30f, 0.2570f}, {1.35f, 0.2520f},
    {1.40f, 0.2460f}, {1.45f, 0.2390f}, {1.50f, 0.2320f}, {1.55f, 0.2250f},
    {1.60f, 0.2190f}, {1.65f, 0.2140f}, {1.70f, 0.2100f}, {1.75f, 0.2060f},
    {1.80f, 0.2030f}, {1.85f, 0.2000f}, {1.90f, 0.1970f}, {1.95f, 0.1950f},
    {2.00f, 0.1920f}, {2.10f, 0.1880f}, {2.20f, 0.1840f}, {2.30f, 0.1810f},
    {2.40f, 0.1780f}, {2.50f, 0.1750f}, {2.60f, 0.1720f}, {2.80f, 0.1680f},
    {3.00f, 0.1640f}, {3.50f, 0.1560f}, {4.00f, 0.1500f}, {5.00f, 0.1420f},
};

#define G1_TABLE_SIZE (sizeof(s_g1_table) / sizeof(s_g1_table[0]))
#define G7_TABLE_SIZE (sizeof(s_g7_table) / sizeof(s_g7_table[0]))

static float lerp_cd_table(const drag_entry_t *table, size_t n, float mach) {
    if (mach <= table[0].mach) {
        return table[0].cd;
    }
    if (mach >= table[n - 1].mach) {
        return table[n - 1].cd;
    }
    for (size_t i = 0; i < n - 1; i++) {
        if (mach >= table[i].mach && mach <= table[i + 1].mach) {
            float t = (mach - table[i].mach) / (table[i + 1].mach - table[i].mach);
            return table[i].cd + t * (table[i + 1].cd - table[i].cd);
        }
    }
    return table[n - 1].cd;
}

static float lerp_cd(ballistic_drag_model_t model, float mach) {
    if (model == BALLISTIC_DRAG_G7) {
        return lerp_cd_table(s_g7_table, G7_TABLE_SIZE, mach);
    }
    return lerp_cd_table(s_g1_table, G1_TABLE_SIZE, mach);
}

static float ballistic_drag_factor(const ballistic_profile_t *p) {
    const float mass_kg = p->bullet_weight_gr * 0.00006479891f;
    const float d_in = (p->bullet_diameter_in > 0.1f) ? p->bullet_diameter_in : 0.224f;
    const float diameter_m = d_in / M_TO_IN;
    const float area = (float)M_PI * 0.25f * diameter_m * diameter_m;
    const float mass_lb = p->bullet_weight_gr / 7000.0f;
    const float form_factor = mass_lb / (d_in * d_in * p->bc);
    return (area / mass_kg) * form_factor;
}

typedef struct {
    float x; /* downrange m along ground/LOS plane */
    float y; /* height m */
    float vx;
    float vy;
} state_t;

static void accel_at(const ballistic_profile_t *p, float drag_scale,
                     float x, float y, float vx, float vy,
                     float *ax, float *ay) {
    (void)x;
    (void)y;
    float speed = sqrtf(vx * vx + vy * vy);
    if (speed < 1.0f) {
        *ax = 0.0f;
        *ay = -GRAVITY_MPS2;
        return;
    }
    float mach = speed / SPEED_OF_SOUND_MPS;
    float cd = lerp_cd(p->drag_model, mach);
    float drag = 0.5f * STANDARD_DENSITY * cd * drag_scale * speed;
    *ax = -drag * vx;
    *ay = -GRAVITY_MPS2 - drag * vy;
}

static void rk4_step(const ballistic_profile_t *p, float drag_scale, state_t *s, float dt) {
    float ax1, ay1, ax2, ay2, ax3, ay3, ax4, ay4;

    accel_at(p, drag_scale, s->x, s->y, s->vx, s->vy, &ax1, &ay1);
    float x2 = s->x + 0.5f * dt * s->vx;
    float y2 = s->y + 0.5f * dt * s->vy;
    float vx2 = s->vx + 0.5f * dt * ax1;
    float vy2 = s->vy + 0.5f * dt * ay1;
    accel_at(p, drag_scale, x2, y2, vx2, vy2, &ax2, &ay2);

    float x3 = s->x + 0.5f * dt * vx2;
    float y3 = s->y + 0.5f * dt * vy2;
    float vx3 = s->vx + 0.5f * dt * ax2;
    float vy3 = s->vy + 0.5f * dt * ay2;
    accel_at(p, drag_scale, x3, y3, vx3, vy3, &ax3, &ay3);

    float x4 = s->x + dt * vx3;
    float y4 = s->y + dt * vy3;
    float vx4 = s->vx + dt * ax3;
    float vy4 = s->vy + dt * ay3;
    accel_at(p, drag_scale, x4, y4, vx4, vy4, &ax4, &ay4);

    s->x += dt * (s->vx + 2.0f * vx2 + 2.0f * vx3 + vx4) / 6.0f;
    s->y += dt * (s->vy + 2.0f * vy2 + 2.0f * vy3 + vy4) / 6.0f;
    s->vx += dt * (ax1 + 2.0f * ax2 + 2.0f * ax3 + ax4) / 6.0f;
    s->vy += dt * (ay1 + 2.0f * ay2 + 2.0f * ay3 + ay4) / 6.0f;
}

/** Integrate until range_m; return height at that range (relative to muzzle). */
static bool integrate_to_range(const ballistic_profile_t *p, float drag_scale,
                               float muzzle_angle_rad, float range_m,
                               float *out_y, float *out_time) {
    float mv = p->muzzle_velocity_fps * FPS_TO_MPS;
    state_t s = {
        .x = 0.0f,
        .y = -p->sight_height_m,
        .vx = mv * cosf(muzzle_angle_rad),
        .vy = mv * sinf(muzzle_angle_rad),
    };

    float t = 0.0f;
    float prev_x = s.x;
    float prev_y = s.y;

    while (t < MAX_SIM_TIME_S) {
        prev_x = s.x;
        prev_y = s.y;
        rk4_step(p, drag_scale, &s, DT_S);
        t += DT_S;

        if (s.x >= range_m && prev_x < range_m) {
            float u = (range_m - prev_x) / (s.x - prev_x);
            *out_y = prev_y + u * (s.y - prev_y);
            *out_time = t;
            return true;
        }
        if (s.vx <= 0.0f || s.y < -200.0f) {
            break;
        }
    }
    return false;
}

/** Find muzzle elevation (rad) so impact height at zero_range matches LOS at look_angle. */
static bool find_zero_angle(const ballistic_profile_t *p, float drag_scale,
                            float look_angle_rad, float *out_angle) {
    /* LOS height at zero range relative to muzzle: sight line through scope */
    float zero_m = p->zero_range_m;
    float los_y = tanf(look_angle_rad) * zero_m;

    float lo = look_angle_rad - 0.05f; /* ~-3 deg relative */
    float hi = look_angle_rad + 0.10f;
    float y = 0.0f, t = 0.0f;

    for (int iter = 0; iter < 24; iter++) {
        float mid = 0.5f * (lo + hi);
        if (!integrate_to_range(p, drag_scale, mid, zero_m, &y, &t)) {
            hi = mid;
            continue;
        }
        if (y > los_y) {
            hi = mid;
        } else {
            lo = mid;
        }
        *out_angle = mid;
    }
    return integrate_to_range(p, drag_scale, *out_angle, zero_m, &y, &t);
}

const char *ballistic_profile_name(void) {
    return ballistic_profile_get()->name;
}

void ballistic_run_sanity_check(void) {
    static const float ranges_m[] = {91.44f, 274.32f, 457.2f}; /* 100/300/500 yd */
    ESP_LOGI(TAG, "Sanity check profile=%s", ballistic_profile_name());
    for (size_t i = 0; i < sizeof(ranges_m) / sizeof(ranges_m[0]); i++) {
        ballistic_input_t in = {.distance_m = ranges_m[i], .look_angle_deg = 0.0f};
        ballistic_output_t out = {0};
        esp_err_t err = ballistic_solve_holdover(&in, &out);
        if (err != ESP_OK || !out.valid) {
            ESP_LOGW(TAG, "Sanity %.0fm FAILED (%s)", ranges_m[i], esp_err_to_name(err));
            continue;
        }
        /* Expect near-zero drop at zero range; increasing drop farther out */
        ESP_LOGI(TAG, "Sanity %.0fm -> drop=%.1fin hold=%.2fmil",
                 ranges_m[i], out.drop_in, out.hold_mil);
        if (i == 0 && fabsf(out.drop_in) > 3.0f) {
            ESP_LOGW(TAG, "Zero-range drop unexpectedly large (%.1fin)", out.drop_in);
        }
        if (i > 0 && out.drop_in < 0.0f) {
            ESP_LOGW(TAG, "Drop sign unexpected at %.0fm (%.1fin)", ranges_m[i], out.drop_in);
        }
    }
}

esp_err_t ballistic_solve_holdover(const ballistic_input_t *in, ballistic_output_t *out) {
    if (!in || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->valid = false;

    if (in->distance_m < MIN_DISTANCE_M || in->distance_m > MAX_DISTANCE_M ||
        !isfinite(in->distance_m) || !isfinite(in->look_angle_deg)) {
        ESP_LOGW(TAG, "Invalid input distance=%.2f look=%.2f", in->distance_m, in->look_angle_deg);
        return ESP_ERR_INVALID_ARG;
    }

    const ballistic_profile_t *p = ballistic_profile_get();
    if (p->bc <= 0.0f || p->muzzle_velocity_fps <= 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }

    float drag_scale = ballistic_drag_factor(p);
    float look = in->look_angle_deg * (float)M_PI / 180.0f;
    /* Clamp extreme angles */
    if (look > 0.7f) look = 0.7f;
    if (look < -0.7f) look = -0.7f;

    float muzzle_angle = look;
    if (!find_zero_angle(p, drag_scale, look, &muzzle_angle)) {
        ESP_LOGW(TAG, "Zero solve failed");
        return ESP_FAIL;
    }

    float impact_y = 0.0f;
    float tof = 0.0f;
    if (!integrate_to_range(p, drag_scale, muzzle_angle, in->distance_m, &impact_y, &tof)) {
        ESP_LOGW(TAG, "Trajectory failed at %.1fm", in->distance_m);
        return ESP_FAIL;
    }

    float los_y = tanf(look) * in->distance_m;
    /* Positive drop = impact below LOS (need to hold over) */
    float drop_m = los_y - impact_y;

    out->drop_m = drop_m;
    out->drop_in = drop_m * M_TO_IN;
    out->hold_mil = (in->distance_m > 0.1f) ? (drop_m / in->distance_m) * 1000.0f : 0.0f;
    out->valid = true;

    ESP_LOGI(TAG, "%s @ %.1fm look=%.1f° drop=%.1fin hold=%.2fmil (tof=%.3fs)",
             p->name, in->distance_m, in->look_angle_deg, out->drop_in, out->hold_mil, tof);
    return ESP_OK;
}
