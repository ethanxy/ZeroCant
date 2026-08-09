# MVP holdover solver + editable NVS-backed ammo profile
idf_component_register(
    SRCS
        "ballistic_holdover.c"
        "ballistic_profile.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_common" "log" "nvs_flash"
)
