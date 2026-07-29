/**
 * @file ballistic_engine.c
 * @brief Ballistic engine implementation
 */

#include "ballistic_engine.h"
#include "ballistic_math.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char* TAG = "ballistic";

// Physical constants
#define EARTH_ROTATION_RATE (7.2921159e-5f)  // rad/sec
#define STANDARD_GRAVITY (32.17405f)          // ft/s²
#define STANDARD_AIR_DENSITY (0.076474f)      // lb/ft³ at sea level, 59°F
#define GAS_CONSTANT_DRY_AIR (53.35f)         // ft·lbf/(lbm·°R)

esp_err_t ballistic_init(ballistic_calc_t* calc) {
    if (!calc) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(calc, 0, sizeof(ballistic_calc_t));
    
    // Initialize default engine configuration
    ballistic_calc_set_default_config(&calc->config);
    
    // Initialize advanced ammunition with defaults
    ammo_init(&calc->shot.ammo);
    
    // Initialize default rifle configuration
    calc->shot.rifle.sight_height_in = 2.0f;
    calc->shot.rifle.zero_range_yd = 100.0f;
    calc->shot.rifle.barrel_twist_in = 12.0f;
    calc->shot.rifle.cant_angle_deg = 0.0f;
    
    // Initialize standard atmosphere
    calc->shot.atmosphere.altitude_ft = 0.0f;
    calc->shot.atmosphere.temperature_f = 59.0f;
    calc->shot.atmosphere.pressure_inhg = 29.92f;
    calc->shot.atmosphere.humidity_percent = 50.0f;
    calc->shot.atmosphere.co2_ppm = 400.0f;
    calc->shot.atmosphere.use_standard_atmosphere = true;
    
    // Initialize advanced effects (disabled by default)
    calc->shot.spin_drift.enable_calculation = false;
    calc->shot.coriolis.enable_calculation = false;
    
    calc->initialized = true;
    return ESP_OK;
}

float calculate_air_density(const atmosphere_t* atmosphere, float altitude_ft) {
    if (!atmosphere) {
        return 1.0f;
    }
    
    if (atmosphere->use_standard_atmosphere) {
        // ICAO Standard Atmosphere model
        float temperature_ratio = (459.67f + atmosphere->temperature_f) / 518.67f;  // °R ratio
        float pressure_ratio = atmosphere->pressure_inhg / 29.92f;
        
        // Altitude correction
        float altitude_factor = expf(-altitude_ft / 26000.0f);
        
        return (pressure_ratio / temperature_ratio) * altitude_factor;
    } else {
        // Simplified model using barometric formula
        float pressure_ratio = atmosphere->pressure_inhg / 29.92f;
        float temperature_ratio = (459.67f + atmosphere->temperature_f) / 518.67f;
        
        // Humidity correction (water vapor is less dense than dry air)
        float humidity_factor = 1.0f - (atmosphere->humidity_percent / 100.0f) * 0.01f;
        
        return pressure_ratio / temperature_ratio * humidity_factor;
    }
}

float calculate_stability_factor(float twist_rate_in, float bullet_length_in, 
                                float bullet_diameter_in, float velocity_fps) {
    if (twist_rate_in <= 0.0f || bullet_length_in <= 0.0f || bullet_diameter_in <= 0.0f) {
        return 1.0f;
    }
    
    // Miller stability formula (simplified)
    float twist_rate_cal = twist_rate_in / bullet_diameter_in;
    float length_cal = bullet_length_in / bullet_diameter_in;
    float velocity_factor = velocity_fps / 2800.0f;  // Normalized to standard velocity
    
    float sg = powf(velocity_factor, 2) * powf(bullet_diameter_in, 2) / 
               (powf(twist_rate_cal, 2) * powf(length_cal, 3));
    
    return sg;
}

float calculate_spin_drift(const spin_drift_params_t* params, float time_sec, 
                          float range_ft, float velocity_fps) {
    if (!params || !params->enable_calculation || time_sec <= 0.0f) {
        return 0.0f;
    }
    
    // Simplified spin drift calculation based on time of flight
    // Real implementation would need more complex Magnus force modeling
    float drift_coefficient = 1.25f;  // Empirical coefficient
    
    if (params->twist_rate_in <= 0.0f) {
        return 0.0f;
    }
    
    // Calculate spin rate
    float spin_rate_rps = velocity_fps / (params->twist_rate_in / 12.0f);  // revolutions per second
    
    // Spin drift is proportional to time^(4/3) and spin rate
    float drift_inches = drift_coefficient * powf(time_sec, 4.0f/3.0f) * 
                         (spin_rate_rps / 1000.0f) * (range_ft / 1000.0f);
    
    // Apply direction (right-hand twist drifts right)
    if (!params->right_hand_twist) {
        drift_inches = -drift_inches;
    }
    
    return drift_inches;
}

void calculate_coriolis_effect(const coriolis_params_t* params, float time_sec, 
                              float velocity_fps, float* horizontal_drift_in, 
                              float* vertical_drift_in) {
    if (!params || !params->enable_calculation || time_sec <= 0.0f) {
        if (horizontal_drift_in) *horizontal_drift_in = 0.0f;
        if (vertical_drift_in) *vertical_drift_in = 0.0f;
        return;
    }
    
    float lat_rad = params->latitude_deg * M_PI / 180.0f;
    float azimuth_rad = params->azimuth_deg * M_PI / 180.0f;
    
    // Coriolis acceleration components
    float omega_earth = EARTH_ROTATION_RATE;
    float range_ft = velocity_fps * time_sec;
    
    // Horizontal deflection (primary effect)
    float horizontal_accel = 2.0f * omega_earth * velocity_fps * sinf(lat_rad) * cosf(azimuth_rad);
    float horizontal_drift_ft = 0.5f * horizontal_accel * time_sec * time_sec;
    
    // Vertical deflection (secondary effect)
    float vertical_accel = 2.0f * omega_earth * velocity_fps * cosf(lat_rad) * sinf(azimuth_rad);
    float vertical_drift_ft = 0.5f * vertical_accel * time_sec * time_sec;
    
    if (horizontal_drift_in) {
        *horizontal_drift_in = horizontal_drift_ft * 12.0f;  // Convert to inches
    }
    if (vertical_drift_in) {
        *vertical_drift_in = vertical_drift_ft * 12.0f;      // Convert to inches
    }
}

// RK4 integration with advanced effects
static void calc_acceleration(ballistic_vector_t* accel,
                                     const ballistic_vector_t* position,
                                     const ballistic_vector_t* velocity,
                                     const shot_t* shot,
                                     float current_time) {
    // Get current altitude
    float current_altitude = shot->atmosphere.altitude_ft + position->y;
    
    // Calculate air density at current altitude
    float air_density_ratio = calculate_air_density(&shot->atmosphere, current_altitude);
    
    // Get current velocity magnitude and mach number
    float velocity_magnitude = ballistic_vector_magnitude(velocity);
    float speed_of_sound = 1116.45f * sqrtf((shot->atmosphere.temperature_f + 459.67f) / 518.67f);
    float mach = velocity_magnitude / speed_of_sound;
    
    // Get current BC (considering multi-BC model)
    float current_bc = ammo_get_bc(&shot->ammo, velocity_magnitude, shot->atmosphere.temperature_f);
    
    // Get drag coefficient for current conditions
    float drag_coefficient = get_drag_coefficient_for_model(shot->ammo.bc_model.drag_table_type, mach);
    
    // Calculate effective drag
    float bullet_area = M_PI * powf(shot->ammo.bullet_diameter_in / 2.0f / 12.0f, 2);  // ft²
    float bullet_mass = shot->ammo.bullet_weight_gr / 7000.0f / 32.17405f;  // slugs
    float form_factor = drag_coefficient / current_bc;
    
    float drag_force = 0.5f * air_density_ratio * STANDARD_AIR_DENSITY * 
                      drag_coefficient * bullet_area * velocity_magnitude * velocity_magnitude;
    
    float drag_accel = drag_force / bullet_mass;
    
    // Apply drag in opposite direction of velocity
    ballistic_vector_t velocity_unit;
    ballistic_vector_normalize(&velocity_unit, velocity);
    
    accel->x = -drag_accel * velocity_unit.x;
    accel->y = GRAVITY_CONSTANT - drag_accel * velocity_unit.y;
    accel->z = -drag_accel * velocity_unit.z;
    
    // Add wind effect
    ballistic_vector_t wind_vector = {0.0f, 0.0f, 0.0f};
    float current_range = ballistic_vector_magnitude(position);
    
    // Find applicable wind layer
    for (int i = 0; i < shot->wind_count; i++) {
        if (current_range <= shot->winds[i].until_range_ft) {
            float wind_rad = shot->winds[i].direction_deg * M_PI / 180.0f;
            wind_vector.x = shot->winds[i].velocity_fps * cosf(wind_rad);
            wind_vector.z = shot->winds[i].velocity_fps * sinf(wind_rad);
            break;
        }
    }
    
    // Relative velocity for wind effect
    ballistic_vector_t relative_velocity;
    ballistic_vector_sub(&relative_velocity, velocity, &wind_vector);
    float relative_speed = ballistic_vector_magnitude(&relative_velocity);
    
    if (relative_speed > 0.0f) {
        ballistic_vector_normalize(&velocity_unit, &relative_velocity);
        float wind_drag_accel = 0.5f * air_density_ratio * STANDARD_AIR_DENSITY * 
                               drag_coefficient * bullet_area * relative_speed * relative_speed / bullet_mass;
        
        accel->x += -wind_drag_accel * velocity_unit.x + drag_accel * velocity_unit.x;
        accel->z += -wind_drag_accel * velocity_unit.z + drag_accel * velocity_unit.z;
    }
}

esp_err_t ballistic_trajectory(ballistic_calc_t* calc,
                                       float max_range_ft,
                                       float range_step_ft) {
    if (!calc || !calc->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Reset result
    memset(&calc->result, 0, sizeof(calc->result));
    
    float dt = calc->config.step_multiplier * DEFAULT_TIME_STEP;
    float time = 0.0f;
    float next_range_target = range_step_ft;
    
    // Initial conditions
    ballistic_vector_t position = {0.0f, -calc->shot.rifle.sight_height_in / 12.0f, 0.0f};  // ft
    ballistic_vector_t velocity;
    
    float muzzle_velocity = ammo_get_muzzle_velocity(&calc->shot.ammo, calc->shot.atmosphere.temperature_f);
    float elevation_rad = calc->shot.barrel_elevation_deg * M_PI / 180.0f;
    
    velocity.x = muzzle_velocity * cosf(elevation_rad);
    velocity.y = muzzle_velocity * sinf(elevation_rad);
    velocity.z = 0.0f;
    
    // Integration loop
    while (calc->result.point_count < MAX_TRAJECTORY_POINTS) {
        // Check termination conditions
        if (position.x >= max_range_ft) {
            calc->result.termination_reason = \"Maximum range reached\";
            break;
        }
        
        float velocity_magnitude = ballistic_vector_magnitude(&velocity);
        if (velocity_magnitude < calc->config.minimum_velocity_fps) {
            calc->result.termination_reason = \"Minimum velocity reached\";
            break;
        }
        
        if (position.y < -calc->config.maximum_drop_ft) {
            calc->result.termination_reason = \"Maximum drop exceeded\";
            break;
        }
        
        // Store trajectory point at range intervals
        if (position.x >= next_range_target || calc->result.point_count == 0) {
            trajectory_point_t* point = &calc->result.points[calc->result.point_count];
            
            point->time_sec = time;
            point->range_ft = position.x;
            point->drop_in = position.y * 12.0f;  // Convert ft to inches
            point->windage_in = position.z * 12.0f;
            point->velocity_fps = velocity_magnitude;
            
            // Calculate energy
            float bullet_mass_lb = calc->shot.ammo.bullet_weight_gr / 7000.0f;
            point->energy_ftlb = 0.5f * bullet_mass_lb * velocity_magnitude * velocity_magnitude / 32.17405f;
            
            // Calculate mach number
            float speed_of_sound = 1116.45f * sqrtf((calc->shot.atmosphere.temperature_f + 459.67f) / 518.67f);
            point->mach_number = velocity_magnitude / speed_of_sound;
            
            // Flight path angle
            point->flight_angle_deg = atan2f(velocity.y, velocity.x) * 180.0f / M_PI;
            
            // Advanced calculations
            if (calc->shot.spin_drift.enable_calculation) {
                point->spin_drift_in = calculate_spin_drift(&calc->shot.spin_drift, time, position.x, velocity_magnitude);
            }
            
            if (calc->shot.coriolis.enable_calculation) {
                float coriolis_h, coriolis_v;
                calculate_coriolis_effect(&calc->shot.coriolis, time, velocity_magnitude, &coriolis_h, &coriolis_v);
                point->coriolis_drift_in = coriolis_h;
            }
            
            // Current ballistic parameters
            point->ballistic_coefficient = ammo_get_bc(&calc->shot.ammo, velocity_magnitude, calc->shot.atmosphere.temperature_f);
            point->air_density_ratio = calculate_air_density(&calc->shot.atmosphere, calc->shot.atmosphere.altitude_ft + position.y);
            point->drag_coefficient = get_drag_coefficient_for_model(calc->shot.ammo.bc_model.drag_table_type, point->mach_number);
            
            calc->result.point_count++;
            next_range_target += range_step_ft;
        }
        
        // RK4 integration step
        ballistic_vector_t k1_v, k1_a, k2_v, k2_a, k3_v, k3_a, k4_v, k4_a;
        ballistic_vector_t temp_pos, temp_vel;
        
        // k1
        k1_v = velocity;
        calc_acceleration(&k1_a, &position, &velocity, &calc->shot, time);
        
        // k2
        ballistic_vector_mul_scalar(&temp_pos, &k1_v, dt * 0.5f);
        ballistic_vector_add(&temp_pos, &position, &temp_pos);
        ballistic_vector_mul_scalar(&temp_vel, &k1_a, dt * 0.5f);
        ballistic_vector_add(&temp_vel, &velocity, &temp_vel);
        k2_v = temp_vel;
        calc_acceleration(&k2_a, &temp_pos, &temp_vel, &calc->shot, time + dt * 0.5f);
        
        // k3
        ballistic_vector_mul_scalar(&temp_pos, &k2_v, dt * 0.5f);
        ballistic_vector_add(&temp_pos, &position, &temp_pos);
        ballistic_vector_mul_scalar(&temp_vel, &k2_a, dt * 0.5f);
        ballistic_vector_add(&temp_vel, &velocity, &temp_vel);
        k3_v = temp_vel;
        calc_acceleration(&k3_a, &temp_pos, &temp_vel, &calc->shot, time + dt * 0.5f);
        
        // k4
        ballistic_vector_mul_scalar(&temp_pos, &k3_v, dt);
        ballistic_vector_add(&temp_pos, &position, &temp_pos);
        ballistic_vector_mul_scalar(&temp_vel, &k3_a, dt);
        ballistic_vector_add(&temp_vel, &velocity, &temp_vel);
        k4_v = temp_vel;
        calc_acceleration(&k4_a, &temp_pos, &temp_vel, &calc->shot, time + dt);
        
        // Update position and velocity
        ballistic_vector_t pos_delta, vel_delta;
        
        // Position update: pos += dt/6 * (k1_v + 2*k2_v + 2*k3_v + k4_v)
        pos_delta = k1_v;
        ballistic_vector_mul_scalar(&temp_vel, &k2_v, 2.0f);
        ballistic_vector_add(&pos_delta, &pos_delta, &temp_vel);
        ballistic_vector_mul_scalar(&temp_vel, &k3_v, 2.0f);
        ballistic_vector_add(&pos_delta, &pos_delta, &temp_vel);
        ballistic_vector_add(&pos_delta, &pos_delta, &k4_v);
        ballistic_vector_mul_scalar(&pos_delta, &pos_delta, dt / 6.0f);
        ballistic_vector_add(&position, &position, &pos_delta);
        
        // Velocity update: vel += dt/6 * (k1_a + 2*k2_a + 2*k3_a + k4_a)
        vel_delta = k1_a;
        ballistic_vector_mul_scalar(&temp_vel, &k2_a, 2.0f);
        ballistic_vector_add(&vel_delta, &vel_delta, &temp_vel);
        ballistic_vector_mul_scalar(&temp_vel, &k3_a, 2.0f);
        ballistic_vector_add(&vel_delta, &vel_delta, &temp_vel);
        ballistic_vector_add(&vel_delta, &vel_delta, &k4_a);
        ballistic_vector_mul_scalar(&vel_delta, &vel_delta, dt / 6.0f);
        ballistic_vector_add(&velocity, &velocity, &vel_delta);
        
        time += dt;
    }
    
    calc->result.calculation_complete = true;
    calc->result.max_range_ft = position.x;
    calc->result.time_of_flight_sec = time;
    
    return ESP_OK;
}

esp_err_t ballistic_find_zero(ballistic_calc_t* calc,
                                      float zero_distance_yd,
                                      float* zero_angle_deg) {
    if (!calc || !zero_angle_deg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float zero_distance_ft = zero_distance_yd * 3.0f;
    float low_angle = -10.0f;
    float high_angle = 10.0f;
    float tolerance = ZERO_FINDING_ACCURACY;
    
    for (int iteration = 0; iteration < MAX_ITERATIONS; iteration++) {
        float test_angle = (low_angle + high_angle) * 0.5f;
        calc->shot.barrel_elevation_deg = test_angle;
        
        esp_err_t err = ballistic_trajectory(calc, zero_distance_ft + 100.0f, 10.0f);
        if (err != ESP_OK) {
            return err;
        }
        
        // Find the point closest to zero distance
        float drop_at_zero = 0.0f;
        bool found = false;
        
        for (int i = 0; i < calc->result.point_count - 1; i++) {
            if (calc->result.points[i].range_ft <= zero_distance_ft && 
                calc->result.points[i+1].range_ft >= zero_distance_ft) {
                
                // Interpolate drop at exact zero distance
                drop_at_zero = ballistic_interpolate_linear(zero_distance_ft,
                    calc->result.points[i].range_ft, calc->result.points[i].drop_in,
                    calc->result.points[i+1].range_ft, calc->result.points[i+1].drop_in);
                found = true;
                break;
            }
        }
        
        if (!found) {
            return ESP_ERR_NOT_FOUND;
        }
        
        if (fabsf(drop_at_zero) < tolerance) {
            *zero_angle_deg = test_angle;
            return ESP_OK;
        }
        
        if (drop_at_zero > 0) {
            high_angle = test_angle;
        } else {
            low_angle = test_angle;
        }
    }
    
    return ESP_ERR_TIMEOUT;
}
