/**
 * @file ballistic_math.c
 * @brief Mathematical utilities for ballistic calculations
 */

#include "ballistic_calc.h"
#include <math.h>
#include <string.h>

// Vector operations
void ballistic_vector_add(ballistic_vector_t* result, 
                         const ballistic_vector_t* a, 
                         const ballistic_vector_t* b) {
    result->x = a->x + b->x;
    result->y = a->y + b->y;
    result->z = a->z + b->z;
}

void ballistic_vector_sub(ballistic_vector_t* result, 
                         const ballistic_vector_t* a, 
                         const ballistic_vector_t* b) {
    result->x = a->x - b->x;
    result->y = a->y - b->y;
    result->z = a->z - b->z;
}

void ballistic_vector_mul_scalar(ballistic_vector_t* result, 
                                const ballistic_vector_t* v, 
                                float scalar) {
    result->x = v->x * scalar;
    result->y = v->y * scalar;
    result->z = v->z * scalar;
}

float ballistic_vector_magnitude(const ballistic_vector_t* v) {
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

void ballistic_vector_normalize(ballistic_vector_t* result, const ballistic_vector_t* v) {
    float mag = ballistic_vector_magnitude(v);
    if (mag > 1e-9f) {
        result->x = v->x / mag;
        result->y = v->y / mag;
        result->z = v->z / mag;
    } else {
        result->x = result->y = result->z = 0.0f;
    }
}

// Unit conversion functions
float ballistic_yards_to_feet(float yards) {
    return yards * 3.0f;
}

float ballistic_feet_to_yards(float feet) {
    return feet / 3.0f;
}

float ballistic_inches_to_feet(float inches) {
    return inches / 12.0f;
}

float ballistic_feet_to_inches(float feet) {
    return feet * 12.0f;
}

float ballistic_grains_to_pounds(float grains) {
    return grains / 7000.0f;
}

// Atmospheric calculations
float ballistic_calc_get_density_ratio(const atmosphere_t* atmosphere, float altitude_ft) {
    // Standard atmosphere model
    const float sea_level_pressure = 29.92f;  // inHg
    const float sea_level_temp = 59.0f;       // °F
    const float lapse_rate = 0.00356f;        // °F/ft
    
    // Temperature at altitude
    float temp_f = atmosphere->temperature_f - (lapse_rate * altitude_ft);
    float temp_r = temp_f + 459.67f;  // Convert to Rankine
    
    // Pressure ratio (simplified barometric formula)
    float pressure_ratio = powf((temp_r / (sea_level_temp + 459.67f)), 5.2561f);
    
    // Density ratio
    float density_ratio = (atmosphere->pressure_inhg / sea_level_pressure) * 
                         pressure_ratio * 
                         ((sea_level_temp + 459.67f) / temp_r);
    
    return density_ratio;
}

// Speed of sound calculation
float ballistic_calc_speed_of_sound(float temperature_f) {
    float temp_r = temperature_f + 459.67f;  // Convert to Rankine
    return sqrtf(1.4f * 1545.0f * temp_r / 28.97f);  // ft/s
}

// Interpolation function for drag tables
float ballistic_interpolate_linear(float x, float x1, float y1, float x2, float y2) {
    if (fabsf(x2 - x1) < 1e-9f) {
        return y1;
    }
    return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
}

// RK4 integration helper - acceleration function
void ballistic_calc_acceleration(ballistic_vector_t* accel,
                                const ballistic_vector_t* velocity,
                                const ballistic_vector_t* wind,
                                float drag_coefficient,
                                float gravity_y) {
    // Relative velocity (bullet velocity relative to air)
    ballistic_vector_t relative_vel;
    ballistic_vector_sub(&relative_vel, velocity, wind);
    
    float relative_speed = ballistic_vector_magnitude(&relative_vel);
    
    // Drag acceleration = -drag_coefficient * relative_velocity * relative_speed
    float drag_magnitude = drag_coefficient * relative_speed;
    
    accel->x = -drag_magnitude * relative_vel.x;
    accel->y = gravity_y - drag_magnitude * relative_vel.y;
    accel->z = -drag_magnitude * relative_vel.z;
}
