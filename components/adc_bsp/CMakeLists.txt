idf_component_register(
  SRCS "adc_bsp.c"
  PRIV_REQUIRES esp_adc esp_driver_tsens
  INCLUDE_DIRS "./")
