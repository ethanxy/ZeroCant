/**
 * @file multi_bc_model.h
 * @brief Multi-BC model support for velocity-dependent ballistic coefficients
 * 
 * This implements the DragModelMultiBC functionality from py-ballisticcalc,
 * allowing ballistic coefficients to vary with velocity/Mach number.
 */

#ifndef MULTI_BC_MODEL_H
#define MULTI_BC_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BC_POINTS 10

/**
 * @brief Single BC point for multi-BC model
 */
typedef struct {
    float mach;     // Mach number
    float bc;       // Ballistic coefficient at this Mach
} bc_point_t;

/**
 * @brief Multi-BC drag model
 */
typedef struct {
    bc_point_t points[MAX_BC_POINTS];
    uint8_t point_count;
    uint8_t drag_table_type;  // 1=G1, 7=G7, etc.
    bool is_sorted;
} multi_bc_model_t;

/**
 * @brief Temperature sensitivity model
 */
typedef struct {
    float reference_velocity_fps;
    float reference_temp_f;
    float velocity_temp_coeff;  // fps per degree F
    bool enabled;
} temp_sensitivity_t;

/**
 * @brief Advanced ammunition properties
 */
typedef struct {
    // Basic properties
    float bullet_weight_gr;
    float bullet_diameter_in;
    float bullet_length_in;
    float base_muzzle_velocity_fps;
    float reference_temp_f;
    
    // Multi-BC model
    multi_bc_model_t bc_model;
    
    // Temperature sensitivity
    temp_sensitivity_t temp_sens;
    
    // Spin drift properties
    float twist_rate_in;        // Barrel twist rate (1 in X inches)
    float stability_factor;     // Gyroscopic stability factor
    bool enable_spin_drift;
} advanced_ammo_t;

// Multi-BC model functions
/**
 * @brief Initialize multi-BC model
 */
esp_err_t multi_bc_init(multi_bc_model_t* model, uint8_t drag_table_type);

/**
 * @brief Add BC point to multi-BC model
 */
esp_err_t multi_bc_add_point(multi_bc_model_t* model, float mach, float bc);

/**
 * @brief Sort BC points by Mach number (required before use)
 */
void multi_bc_sort_points(multi_bc_model_t* model);

/**
 * @brief Get interpolated BC for given Mach number
 */
float multi_bc_get_bc_for_mach(const multi_bc_model_t* model, float mach);

/**
 * @brief Validate BC model (check for proper ordering, etc.)
 */
bool multi_bc_validate(const multi_bc_model_t* model);

// Temperature sensitivity functions
/**
 * @brief Initialize temperature sensitivity model
 */
void temp_sens_init(temp_sensitivity_t* temp_sens, 
                   float ref_velocity_fps, 
                   float ref_temp_f);

/**
 * @brief Calculate powder temperature sensitivity coefficient
 */
esp_err_t temp_sens_calc_coefficient(temp_sensitivity_t* temp_sens,
                                    float velocity_at_temp_fps,
                                    float test_temp_f);

/**
 * @brief Get velocity for given temperature
 */
float temp_sens_get_velocity_for_temp(const temp_sensitivity_t* temp_sens,
                                     float current_temp_f);

// Advanced ammo functions
/**
 * @brief Initialize advanced ammunition model
 */
esp_err_t advanced_ammo_init(advanced_ammo_t* ammo);

/**
 * @brief Get effective muzzle velocity considering temperature
 */
float advanced_ammo_get_muzzle_velocity(const advanced_ammo_t* ammo, float temp_f);

/**
 * @brief Get ballistic coefficient for velocity and temperature
 */
float advanced_ammo_get_bc(const advanced_ammo_t* ammo, float velocity_fps, float temp_f);

#ifdef __cplusplus
}
#endif

#endif // MULTI_BC_MODEL_H
