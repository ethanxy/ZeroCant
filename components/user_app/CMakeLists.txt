idf_component_register(
  SRCS "user_app.c"
  PRIV_REQUIRES sd_card_bsp lvgl ui_bsp adc_bsp esp_wifi_bsp ble_scan_bsp qmi8658c angle_display angle_calc level_display esp_touch laser settings ui_state_manager main battery_protection
  INCLUDE_DIRS "./")
