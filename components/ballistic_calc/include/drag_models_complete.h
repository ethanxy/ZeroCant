/**
 * @file drag_models_complete.h
 * @brief Complete drag model library including all standard models
 * 
 * Implements G1, G2, G5, G6, G7, G8, GI drag models from py-ballisticcalc
 */

#ifndef DRAG_MODELS_COMPLETE_H
#define DRAG_MODELS_COMPLETE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Drag model types
typedef enum {
    DRAG_MODEL_G1 = 1,
    DRAG_MODEL_G2 = 2,
    DRAG_MODEL_G5 = 5,
    DRAG_MODEL_G6 = 6,
    DRAG_MODEL_G7 = 7,
    DRAG_MODEL_G8 = 8,
    DRAG_MODEL_GI = 9,   // Ingalls tables
    DRAG_MODEL_CUSTOM = 99
} drag_model_type_t;

// Maximum table size for any drag model
#define MAX_DRAG_TABLE_SIZE 200

/**
 * @brief Drag table entry
 */
typedef struct {
    float mach;     // Mach number
    float cd;       // Drag coefficient
} drag_table_entry_t;

/**
 * @brief Complete drag model definition
 */
typedef struct {
    drag_model_type_t type;
    const drag_table_entry_t* table;
    uint16_t table_size;
    const char* name;
    const char* description;
} drag_model_def_t;

// Get drag model definition by type
const drag_model_def_t* get_drag_model_def(drag_model_type_t type);

// Get drag coefficient for mach number using specific model
float get_drag_coefficient_for_model(drag_model_type_t model_type, float mach);

// Convert between drag models (approximate)
float convert_bc_between_models(float bc_value, 
                               drag_model_type_t from_model, 
                               drag_model_type_t to_model,
                               float mach);

// Get all available drag models
const drag_model_def_t** get_all_drag_models(uint8_t* count);

#ifdef __cplusplus
}
#endif

#endif // DRAG_MODELS_COMPLETE_H
