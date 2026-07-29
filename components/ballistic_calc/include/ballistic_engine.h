/**
 * @file ballistic_engine.h
 * @brief Ballistic engine with complete py-ballisticcalc feature set
 */

#ifndef BALLISTIC_ENGINE_H
#define BALLISTIC_ENGINE_H

#include "ballistic_calc.h"
#include "multi_bc_model.h"
#include "drag_models_complete.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Spin drift calculation parameters
 */
typedef struct {
    float twist_rate_in;        // Barrel twist rate (1 in X inches)
    float bullet_length_in;     // Bullet length in inches
    float stability_factor;     // Gyroscopic stability factor
    bool right_hand_twist;      // True for right-hand twist
    bool enable_calculation;    // Enable spin drift calculation
} spin_drift_params_t;

/**
 * @brief Coriolis effect parameters
 */
typedef struct {
    float latitude_deg;         // Shooting latitude in degrees
    float azimuth_deg;          // Shooting azimuth (bearing) in degrees
    bool enable_calculation;    // Enable Coriolis calculation
} coriolis_params_t;

/**
 * @brief Advanced atmospheric model
 */
typedef struct {
    float altitude_ft;          // Altitude in feet
    float temperature_f;        // Temperature in Fahrenheit
    float pressure_inhg;        // Barometric pressure in inHg
    float humidity_percent;     // Relative humidity percentage
    float co2_ppm;              // CO2 concentration in ppm (default 400)
    bool use_standard_atmosphere; // Use ICAO standard atmosphere model
} atmosphere_t;

/**
 * @brief Advanced shot configuration
 */
typedef struct {
    // Basic ammunition
    ammo_t ammo;
    
    // Rifle configuration
    rifle_config_t rifle;
    
    // Advanced atmospheric conditions
    atmosphere_t atmosphere;
    
    // Wind layers
    wind_layer_t winds[MAX_WIND_LAYERS];
    uint8_t wind_count;
    
    // Advanced physics effects
    spin_drift_params_t spin_drift;
    coriolis_params_t coriolis;
    
    // Shot geometry
    float look_angle_deg;       // Look angle to target
    float cant_angle_deg;       // Rifle cant angle
    float barrel_elevation_deg; // Barrel elevation
} shot_t;

/**
 * @brief Enhanced trajectory point with additional data
 */
typedef struct {
    // Basic trajectory data
    float time_sec;
    float range_ft;
    float drop_in;
    float windage_in;
    float velocity_fps;
    float energy_ftlb;
    float mach_number;
    float flight_angle_deg;
    
    // Advanced data
    float spin_drift_in;        // Spin drift deflection
    float coriolis_drift_in;    // Coriolis deflection
    float total_drop_in;        // Total drop including look angle
    float drag_coefficient;     // Current drag coefficient
    float ballistic_coefficient; // Current BC (for multi-BC)
    float air_density_ratio;    // Current air density ratio
    float wind_velocity_fps;    // Effective wind velocity
    float stability_factor;     // Current stability factor
} trajectory_point_t;

/**
 * @brief Advanced trajectory result
 */
typedef struct {
    trajectory_point_t points[MAX_TRAJECTORY_POINTS];
    uint16_t point_count;
    bool calculation_complete;
    float max_range_ft;
    float time_of_flight_sec;
    float max_height_ft;        // Maximum trajectory height
    float danger_space_ft;      // Danger space for target height
    const char* termination_reason;
} trajectory_result_t;

/**
 * @brief Advanced ballistic calculator
 */
typedef struct {
    engine_config_t config;
    shot_t shot;
    trajectory_result_t result;
    bool initialized;
} ballistic_calc_t;

// Advanced calculation functions
/**
 * @brief Initialize advanced ballistic calculator
 */
esp_err_t ballistic_init(ballistic_calc_t* calc);

/**
 * @brief Calculate advanced trajectory with all effects
 */
esp_err_t ballistic_trajectory(ballistic_calc_t* calc,
                                       float max_range_ft,
                                       float range_step_ft);

/**
 * @brief Find zero angle considering all effects
 */
esp_err_t ballistic_find_zero(ballistic_calc_t* calc,
                                      float zero_distance_yd,
                                      float* zero_angle_deg);

/**
 * @brief Calculate danger space for target height
 */
esp_err_t ballistic_danger_space(ballistic_calc_t* calc,
                                         float target_distance_yd,
                                         float target_height_in,
                                         float* near_distance_yd,
                                         float* far_distance_yd);

/**
 * @brief Generate range card data
 */
esp_err_t ballistic_range_card(ballistic_calc_t* calc,
                                       float start_range_yd,
                                       float end_range_yd,
                                       float range_step_yd,
                                       trajectory_point_t* range_data,
                                       uint16_t* data_count);

// Advanced physics calculations
/**
 * @brief Calculate spin drift effect
 */
float calculate_spin_drift(const spin_drift_params_t* params,
                          float time_sec,
                          float range_ft,
                          float velocity_fps);

/**
 * @brief Calculate Coriolis effect
 */
void calculate_coriolis_effect(const coriolis_params_t* params,
                              float time_sec,
                              float velocity_fps,
                              float* horizontal_drift_in,
                              float* vertical_drift_in);

/**
 * @brief Calculate advanced atmospheric density
 */
float calculate_air_density(const atmosphere_t* atmosphere,
                                    float altitude_ft);

/**
 * @brief Calculate gyroscopic stability factor
 */
float calculate_stability_factor(float twist_rate_in,
                                float bullet_length_in,
                                float bullet_diameter_in,
                                float velocity_fps);

#ifdef __cplusplus
}
#endif

#endif // BALLISTIC_ENGINE_H
