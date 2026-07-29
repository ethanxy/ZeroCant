idf_component_register(
    SRCS "ballistic_demo.c"
         "ballistic_engine.c"
         "drag_models_complete.c" 
         "multi_bc_model.c" 
         "ballistic_math.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_common" "m"
)
