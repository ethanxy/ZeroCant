/**
 * @file ballistic_demo.c
 * @brief Demonstration of advanced ballistic calculation features
 * 
 * This file shows how to use the enhanced ballistic calculator with
 * all the features from py-ballisticcalc including:
 * - Multi-/**
 * @brief Main demo function
 */
void run_ballistic_demos(void) {
    ESP_LOGI(TAG, \"Starting Ballistic Calculator Demos\");
    ESP_LOGI(TAG, \"=============================================\");els
 * - Temperature sensitivity
 * - Multiple drag models (G1, G2, G5, G6, G7, G8, GI)
 * - Spin drift calculation
 * - Coriolis effects
 * - Advanced atmospheric modeling
 */

#include \"ballistic_engine.h\"
#include \"esp_log.h\"
#include <stdio.h>

static const char* TAG = \"ballistic_demo\";

/**
 * @brief Demo 1: Basic trajectory with multi-BC model
 */
void demo_multi_bc_trajectory(void) {
    ESP_LOGI(TAG, \"=== Demo 1: Multi-BC Trajectory Calculation ===\");
    
    ballistic_calc_t calc;
    ballistic_init(&calc);
    
    // Configure ammunition with multi-BC model (.308 Winchester)
    calc.shot.ammo.bullet_weight_gr = 175.0f;
    calc.shot.ammo.bullet_diameter_in = 0.308f;
    calc.shot.ammo.bullet_length_in = 1.240f;
    calc.shot.ammo.base_muzzle_velocity_fps = 2600.0f;
    calc.shot.ammo.reference_temp_f = 70.0f;
    
    // Setup multi-BC model (velocity-dependent BC)
    multi_bc_init(&calc.shot.ammo.bc_model, DRAG_MODEL_G7);
    multi_bc_add_point(&calc.shot.ammo.bc_model, 0.0f, 0.25f);   // Subsonic
    multi_bc_add_point(&calc.shot.ammo.bc_model, 1.0f, 0.23f);   // Transonic  
    multi_bc_add_point(&calc.shot.ammo.bc_model, 1.5f, 0.22f);   // Supersonic
    multi_bc_add_point(&calc.shot.ammo.bc_model, 2.5f, 0.21f);   // High supersonic
    multi_bc_sort_points(&calc.shot.ammo.bc_model);
    
    // Setup temperature sensitivity
    temp_sens_init(&calc.shot.ammo.temp_sens, 2600.0f, 70.0f);
    temp_sens_calc_coefficient(&calc.shot.ammo.temp_sens, 2540.0f, 32.0f);  // -1.6 fps/°F
    
    // Configure rifle (100 yard zero)
    calc.shot.rifle.sight_height_in = 2.0f;
    calc.shot.rifle.zero_range_yd = 100.0f;
    calc.shot.rifle.barrel_twist_in = 12.0f;
    
    // Cold weather conditions
    calc.shot.atmosphere.temperature_f = 32.0f;
    calc.shot.atmosphere.altitude_ft = 2000.0f;
    calc.shot.atmosphere.pressure_inhg = 28.5f;
    calc.shot.atmosphere.humidity_percent = 70.0f;
    
    // Find zero angle
    float zero_angle;
    esp_err_t err = ballistic_find_zero(&calc, 100.0f, &zero_angle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, \"Zero angle for 100yd: %.3f degrees\", zero_angle);
        ESP_LOGI(TAG, \"Muzzle velocity at 32°F: %.1f fps\", 
                 ammo_get_muzzle_velocity(&calc.shot.ammo, 32.0f));
    }
    
    // Calculate trajectory
    err = ballistic_trajectory(&calc, 1000.0f, 100.0f);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, \"\\nTrajectory data:\");
        ESP_LOGI(TAG, \"Range(yd)  Drop(in)  Wind(in)  Velocity(fps)  Mach   BC     Energy(ft-lb)\");
        
        for (int i = 0; i < calc.result.point_count; i++) {
            trajectory_point_t* p = &calc.result.points[i];
            float range_yd = p->range_ft / 3.0f;
            
            ESP_LOGI(TAG, \"%8.0f  %8.1f  %8.1f  %12.0f  %5.2f  %5.3f  %12.0f\",
                     range_yd, p->drop_in, p->windage_in, p->velocity_fps,
                     p->mach_number, p->ballistic_coefficient, p->energy_ftlb);
        }
    }
}

/**
 * @brief Demo 2: Comparison of different drag models
 */
void demo_drag_model_comparison(void) {
    ESP_LOGI(TAG, \"\\n=== Demo 2: Drag Model Comparison ===\");
    
    drag_model_type_t models[] = {DRAG_MODEL_G1, DRAG_MODEL_G7, DRAG_MODEL_G2, DRAG_MODEL_G5};
    const char* model_names[] = {\"G1\", \"G7\", \"G2\", \"G5\"};
    float base_bc = 0.5f;  // G1 BC
    
    ESP_LOGI(TAG, \"Drag coefficients at Mach 1.2 for BC=%.3f:\", base_bc);
    
    for (int i = 0; i < 4; i++) {
        float drag_coeff = get_drag_coefficient_for_model(models[i], 1.2f);
        float converted_bc = convert_bc_between_models(base_bc, DRAG_MODEL_G1, models[i], 1.2f);
        
        ESP_LOGI(TAG, \"%s: Cd=%.4f, Converted BC=%.3f\", 
                 model_names[i], drag_coeff, converted_bc);
    }
    
    // Show how drag varies with Mach number for G1 vs G7
    ESP_LOGI(TAG, \"\\nDrag coefficient vs Mach number:\");
    ESP_LOGI(TAG, \"Mach    G1      G7      Ratio\");
    
    for (float mach = 0.5f; mach <= 3.0f; mach += 0.5f) {
        float cd_g1 = get_drag_coefficient_for_model(DRAG_MODEL_G1, mach);
        float cd_g7 = get_drag_coefficient_for_model(DRAG_MODEL_G7, mach);
        float ratio = cd_g1 / cd_g7;
        
        ESP_LOGI(TAG, \"%4.1f   %6.4f  %6.4f  %6.2f\", mach, cd_g1, cd_g7, ratio);
    }
}

/**
 * @brief Demo 3: Advanced effects (spin drift, Coriolis)
 */
void demo_effects(void) {
    ESP_LOGI(TAG, \"\\n=== Demo 3: Advanced Effects (Spin Drift & Coriolis) ===\");
    
    ballistic_calc_t calc;
    ballistic_init(&calc);
    
    // Long range setup (.338 Lapua Magnum)
    calc.shot.ammo.bullet_weight_gr = 300.0f;
    calc.shot.ammo.bullet_diameter_in = 0.338f;
    calc.shot.ammo.bullet_length_in = 1.67f;
    calc.shot.ammo.base_muzzle_velocity_fps = 2650.0f;
    
    // G7 BC model
    multi_bc_init(&calc.shot.ammo.bc_model, DRAG_MODEL_G7);
    multi_bc_add_point(&calc.shot.ammo.bc_model, 0.0f, 0.375f);
    multi_bc_sort_points(&calc.shot.ammo.bc_model);
    
    calc.shot.rifle.barrel_twist_in = 10.0f;  // Fast twist for heavy bullets
    calc.shot.rifle.sight_height_in = 2.5f;
    
    // Enable spin drift
    calc.shot.spin_drift.enable_calculation = true;
    calc.shot.spin_drift.twist_rate_in = 10.0f;
    calc.shot.spin_drift.bullet_length_in = 1.67f;
    calc.shot.spin_drift.right_hand_twist = true;
    
    // Enable Coriolis (shooting north at 45° latitude)
    calc.shot.coriolis.enable_calculation = true;
    calc.shot.coriolis.latitude_deg = 45.0f;
    calc.shot.coriolis.azimuth_deg = 0.0f;  // Due north
    
    // High altitude conditions
    calc.shot.atmosphere.altitude_ft = 5000.0f;
    calc.shot.atmosphere.temperature_f = 50.0f;
    calc.shot.atmosphere.pressure_inhg = 24.9f;
    
    // Find zero and calculate trajectory
    float zero_angle;
    ballistic_find_zero(&calc, 200.0f, &zero_angle);
    
    esp_err_t err = ballistic_trajectory(&calc, 2000.0f, 200.0f);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, \"\\nLong range trajectory with advanced effects:\");
        ESP_LOGI(TAG, \"Range(yd)  Drop(in)  Wind(in)  SpinDrift(in)  Coriolis(in)  Velocity(fps)\");
        
        for (int i = 0; i < calc.result.point_count; i++) {
            trajectory_point_t* p = &calc.result.points[i];
            float range_yd = p->range_ft / 3.0f;
            
            ESP_LOGI(TAG, \"%8.0f  %8.1f  %8.1f  %11.2f  %11.2f  %12.0f\",
                     range_yd, p->drop_in, p->windage_in, 
                     p->spin_drift_in, p->coriolis_drift_in, p->velocity_fps);
        }
    }
}

/**
 * @brief Demo 4: Temperature sensitivity analysis
 */
void demo_temperature_sensitivity(void) {
    ESP_LOGI(TAG, \"\\n=== Demo 4: Temperature Sensitivity Analysis ===\");
    
    ballistic_calc_t calc;
    ballistic_init(&calc);
    
    // Standard .308 setup
    calc.shot.ammo.bullet_weight_gr = 168.0f;
    calc.shot.ammo.base_muzzle_velocity_fps = 2650.0f;
    calc.shot.ammo.reference_temp_f = 70.0f;
    
    // Configure temperature sensitivity (typical for rifle powder)
    temp_sens_calc_coefficient(&calc.shot.ammo.temp_sens, 2590.0f, 32.0f);  // -1.6 fps/°F
    
    multi_bc_init(&calc.shot.ammo.bc_model, DRAG_MODEL_G1);
    multi_bc_add_point(&calc.shot.ammo.bc_model, 0.0f, 0.45f);
    multi_bc_sort_points(&calc.shot.ammo.bc_model);
    
    ESP_LOGI(TAG, \"Velocity vs Temperature:\");
    ESP_LOGI(TAG, \"Temp(°F)  Velocity(fps)  Delta(fps)\");
    
    float reference_velocity = ammo_get_muzzle_velocity(&calc.shot.ammo, 70.0f);
    
    for (float temp = 0.0f; temp <= 120.0f; temp += 20.0f) {
        float velocity = ammo_get_muzzle_velocity(&calc.shot.ammo, temp);
        float delta = velocity - reference_velocity;
        
        ESP_LOGI(TAG, \"%8.0f  %12.0f  %9.0f\", temp, velocity, delta);
    }
    
    // Show effect on trajectory at 500 yards
    ESP_LOGI(TAG, \"\\nTrajectory at 500 yards for different temperatures:\");
    ESP_LOGI(TAG, \"Temp(°F)  Drop(in)  Velocity(fps)  Energy(ft-lb)\");
    
    for (float temp = 20.0f; temp <= 100.0f; temp += 20.0f) {
        calc.shot.atmosphere.temperature_f = temp;
        
        float zero_angle;
        ballistic_find_zero(&calc, 100.0f, &zero_angle);
        ballistic_trajectory(&calc, 500.0f * 3.0f, 100.0f);
        
        // Find 500 yard data point
        for (int i = 0; i < calc.result.point_count; i++) {
            float range_yd = calc.result.points[i].range_ft / 3.0f;
            if (range_yd >= 495.0f && range_yd <= 505.0f) {
                trajectory_point_t* p = &calc.result.points[i];
                ESP_LOGI(TAG, \"%8.0f  %8.1f  %12.0f  %11.0f\",
                         temp, p->drop_in, p->velocity_fps, p->energy_ftlb);
                break;
            }
        }
    }
}

/**
 * @brief Main demo function
 */
void run_ballistic_demos(void) {
    ESP_LOGI(TAG, \"Starting Advanced Ballistic Calculator Demos\");
    ESP_LOGI(TAG, \"=============================================\");
    
    demo_multi_bc_trajectory();
    demo_drag_model_comparison();
    demo_effects();
    demo_temperature_sensitivity();
    
    ESP_LOGI(TAG, \"\\n=== All demos completed ===\");
}
