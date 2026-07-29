idf_component_register(SRCS "MadgwickAHRS.c" "angle_calc.c"
                       INCLUDE_DIRS "." "../qmi8658c"
                       REQUIRES "nvs_flash")
