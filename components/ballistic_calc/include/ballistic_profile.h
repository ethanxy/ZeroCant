/**
 * @file ballistic_profile.h
 * @brief Editable ammunition / rifle profile (NVS-backed)
 */

#ifndef BALLISTIC_PROFILE_H
#define BALLISTIC_PROFILE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BALLISTIC_DRAG_G1 = 1,
    BALLISTIC_DRAG_G7 = 7,
} ballistic_drag_model_t;

typedef struct {
    char name[24];                    /**< Display name (auto or custom) */
    float bullet_weight_gr;           /**< Bullet weight (grains) */
    float muzzle_velocity_fps;        /**< Muzzle velocity (fps) */
    float bc;                         /**< Ballistic coefficient */
    ballistic_drag_model_t drag_model;
    float zero_range_m;               /**< Zero distance (meters) */
    float sight_height_m;             /**< Sight height above bore (meters) */
    float bullet_diameter_in;         /**< Bullet diameter (inches) */
    bool temp_sens_enabled;           /**< Reserved, unused */
    float wind_fps;                   /**< Reserved, unused */
} ballistic_profile_t;

/** Load defaults then overlay NVS if present. Call once at boot. */
esp_err_t ballistic_profile_init(void);

/** Active profile (read-only view). */
const ballistic_profile_t *ballistic_profile_get(void);

/** Mutable active profile (edit then save). */
ballistic_profile_t *ballistic_profile_get_mut(void);

/** Refresh display name from weight + drag model. */
void ballistic_profile_refresh_name(void);

/** Persist current profile to NVS. */
esp_err_t ballistic_profile_save(void);

/** Reset to compile-time defaults (does not auto-save). */
void ballistic_profile_reset_defaults(void);

#ifdef __cplusplus
}
#endif

#endif /* BALLISTIC_PROFILE_H */
