/**
 * @file multi_bc_model.c
 * @brief Multi-BC model implementation
 */

#include "multi_bc_model.h"
#include "ballistic_math.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "multi_bc";

// Multi-BC model functions
esp_err_t multi_bc_init(multi_bc_model_t* model, uint8_t drag_table_type) {
    if (!model) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(model, 0, sizeof(multi_bc_model_t));
    model->drag_table_type = drag_table_type;
    model->is_sorted = true;  // Empty model is considered sorted
    
    return ESP_OK;
}

esp_err_t multi_bc_add_point(multi_bc_model_t* model, float mach, float bc) {
    if (!model || model->point_count >= MAX_BC_POINTS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (mach < 0.0f || bc <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    model->points[model->point_count].mach = mach;
    model->points[model->point_count].bc = bc;
    model->point_count++;
    model->is_sorted = false;  // Need to sort after adding
    
    return ESP_OK;
}

static int bc_point_compare(const void* a, const void* b) {
    const bc_point_t* pa = (const bc_point_t*)a;
    const bc_point_t* pb = (const bc_point_t*)b;
    
    if (pa->mach < pb->mach) return -1;
    if (pa->mach > pb->mach) return 1;
    return 0;
}

void multi_bc_sort_points(multi_bc_model_t* model) {
    if (!model || model->point_count <= 1) {
        return;
    }
    
    qsort(model->points, model->point_count, sizeof(bc_point_t), bc_point_compare);
    model->is_sorted = true;
}

float multi_bc_get_bc_for_mach(const multi_bc_model_t* model, float mach) {
    if (!model || model->point_count == 0) {
        return 0.0f;
    }
    
    if (!model->is_sorted) {
        ESP_LOGW(TAG, \"BC model not sorted, results may be incorrect\");
    }
    
    // Single point case
    if (model->point_count == 1) {
        return model->points[0].bc;
    }
    
    // Below range - extrapolate from first two points
    if (mach <= model->points[0].mach) {
        if (model->point_count == 1) {
            return model->points[0].bc;
        }
        return ballistic_interpolate_linear(mach,
                                          model->points[0].mach, model->points[0].bc,
                                          model->points[1].mach, model->points[1].bc);
    }
    
    // Above range - extrapolate from last two points  
    if (mach >= model->points[model->point_count - 1].mach) {
        if (model->point_count == 1) {
            return model->points[0].bc;
        }
        int last = model->point_count - 1;
        return ballistic_interpolate_linear(mach,
                                          model->points[last-1].mach, model->points[last-1].bc,
                                          model->points[last].mach, model->points[last].bc);
    }
    
    // Within range - find bracketing points and interpolate
    for (int i = 0; i < model->point_count - 1; i++) {
        if (mach >= model->points[i].mach && mach <= model->points[i+1].mach) {
            return ballistic_interpolate_linear(mach,
                                              model->points[i].mach, model->points[i].bc,
                                              model->points[i+1].mach, model->points[i+1].bc);
        }
    }
    
    // Fallback - should not reach here
    return model->points[0].bc;
}

bool multi_bc_validate(const multi_bc_model_t* model) {
    if (!model || model->point_count == 0) {
        return false;
    }
    
    // Check for positive values
    for (int i = 0; i < model->point_count; i++) {
        if (model->points[i].mach < 0.0f || model->points[i].bc <= 0.0f) {
            return false;
        }
    }
    
    // Check for proper ordering if sorted
    if (model->is_sorted && model->point_count > 1) {
        for (int i = 0; i < model->point_count - 1; i++) {
            if (model->points[i].mach > model->points[i+1].mach) {
                return false;
            }
        }
    }
    
    return true;
}

// Temperature sensitivity functions
void temp_sens_init(temp_sensitivity_t* temp_sens, 
                   float ref_velocity_fps, 
                   float ref_temp_f) {
    if (!temp_sens) return;
    
    temp_sens->reference_velocity_fps = ref_velocity_fps;
    temp_sens->reference_temp_f = ref_temp_f;
    temp_sens->velocity_temp_coeff = 0.0f;
    temp_sens->enabled = false;
}

esp_err_t temp_sens_calc_coefficient(temp_sensitivity_t* temp_sens,
                                    float velocity_at_temp_fps,
                                    float test_temp_f) {
    if (!temp_sens) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float temp_diff = test_temp_f - temp_sens->reference_temp_f;
    if (fabsf(temp_diff) < 0.1f) {
        ESP_LOGW(TAG, \"Temperature difference too small for accurate coefficient calculation\");
        return ESP_ERR_INVALID_ARG;
    }
    
    float velocity_diff = velocity_at_temp_fps - temp_sens->reference_velocity_fps;
    temp_sens->velocity_temp_coeff = velocity_diff / temp_diff;
    temp_sens->enabled = true;
    
    ESP_LOGI(TAG, \"Temperature sensitivity: %.2f fps/°F\", temp_sens->velocity_temp_coeff);
    
    return ESP_OK;
}

float temp_sens_get_velocity_for_temp(const temp_sensitivity_t* temp_sens,
                                     float current_temp_f) {
    if (!temp_sens || !temp_sens->enabled) {
        return temp_sens ? temp_sens->reference_velocity_fps : 0.0f;
    }
    
    float temp_diff = current_temp_f - temp_sens->reference_temp_f;
    return temp_sens->reference_velocity_fps + (temp_sens->velocity_temp_coeff * temp_diff);
}

// Advanced ammo functions
esp_err_t advanced_ammo_init(advanced_ammo_t* ammo) {
    if (!ammo) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(ammo, 0, sizeof(advanced_ammo_t));
    
    // Initialize default values
    ammo->bullet_weight_gr = 175.0f;
    ammo->bullet_diameter_in = 0.308f;
    ammo->bullet_length_in = 1.240f;
    ammo->base_muzzle_velocity_fps = 2600.0f;
    ammo->reference_temp_f = 70.0f;
    ammo->twist_rate_in = 12.0f;
    ammo->stability_factor = 1.5f;
    ammo->enable_spin_drift = false;
    
    // Initialize BC model with G1 default
    multi_bc_init(&ammo->bc_model, 1);
    multi_bc_add_point(&ammo->bc_model, 0.0f, 0.5f);  // Default BC
    multi_bc_sort_points(&ammo->bc_model);
    
    // Initialize temperature sensitivity (disabled by default)
    temp_sens_init(&ammo->temp_sens, ammo->base_muzzle_velocity_fps, ammo->reference_temp_f);
    
    return ESP_OK;
}

float advanced_ammo_get_muzzle_velocity(const advanced_ammo_t* ammo, float temp_f) {
    if (!ammo) {
        return 0.0f;
    }
    
    return temp_sens_get_velocity_for_temp(&ammo->temp_sens, temp_f);
}

float advanced_ammo_get_bc(const advanced_ammo_t* ammo, float velocity_fps, float temp_f) {
    if (!ammo) {
        return 0.0f;
    }
    
    // Calculate mach number (approximate speed of sound at sea level)
    float speed_of_sound = 1116.45f;  // fps at 70°F, sea level
    
    // Adjust speed of sound for temperature
    float temp_factor = sqrtf((temp_f + 459.67f) / 529.67f);  // Rankine temperature ratio
    speed_of_sound *= temp_factor;
    
    float mach = velocity_fps / speed_of_sound;
    
    return multi_bc_get_bc_for_mach(&ammo->bc_model, mach);
}
