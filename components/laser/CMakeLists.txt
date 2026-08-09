idf_component_register(
    SRCS "laser.c"
    INCLUDE_DIRS "."
    REQUIRES "lvgl" "driver" "ballistic_calc" "angle_calc"
)
