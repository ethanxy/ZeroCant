idf_component_register(SRCS "settings.c" "settings_ammo.c"
                    INCLUDE_DIRS "."
                    REQUIRES "lvgl" "angle_calc" "esp_app_format" "ballistic_calc")
