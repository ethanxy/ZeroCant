/**
 * @file ballistic_calc.h
 * @brief Public MVP API: solve holdover at a measured distance
 */

#ifndef BALLISTIC_CALC_H
#define BALLISTIC_CALC_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float distance_m;       /**< Laser range to target (meters) */
    float look_angle_deg;   /**< Line-of-sight angle vs horizontal (+up) */
} ballistic_input_t;

typedef struct {
    float drop_m;           /**< Drop below LOS (positive = impact low) */
    float drop_in;          /**< Same drop in inches */
    float hold_mil;         /**< Holdover in milliradians */
    bool valid;
} ballistic_output_t;

/**
 * @brief Solve drop / holdover for the fixed ammo profile
 *
 * @param in  Distance + look angle
 * @param out Results (valid=false on failure / out-of-range)
 */
esp_err_t ballistic_solve_holdover(const ballistic_input_t *in, ballistic_output_t *out);

/**
 * @brief Display name of the active fixed profile
 */
const char *ballistic_profile_name(void);

/**
 * @brief Log holdover at reference ranges (100/300/500 m). Safe to call once at boot.
 */
void ballistic_run_sanity_check(void);

#ifdef __cplusplus
}
#endif

#endif /* BALLISTIC_CALC_H */
