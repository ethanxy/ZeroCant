idf_component_register(SRCS "battery_protection.c"
                       INCLUDE_DIRS "."
                       REQUIRES "esp_timer" "adc_bsp" "nvs_flash" "esp_system" "freertos" "lvgl" "driver" "amoled_burn_protection")
