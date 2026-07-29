idf_component_register(
    SRCS "ui_state_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES "freertos" "esp_common" "log" "lvgl"
)
