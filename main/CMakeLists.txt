idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES amoled_burn_protection lvgl i2c_bsp esp_touch user_app memory_diag laser adc_bsp nvs_flash ui_state_manager angle_display level_display action_display settings battery_protection idle_sleep)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
