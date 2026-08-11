idf_component_register(SRCS "idle_sleep.c"
                       INCLUDE_DIRS "."
                       REQUIRES "esp_timer" "esp_system" "freertos" "lvgl"
                                "amoled_burn_protection" "battery_protection")
