#include <stdio.h>
#include "touch_bsp.h"
#include "i2c_bsp.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define I2C_ADDR_FT3168 0x38
#define TOUCH_CACHE_MS 5

static SemaphoreHandle_t s_touch_mutex = NULL;
static uint16_t s_cached_x = 0;
static uint16_t s_cached_y = 0;
static uint8_t s_cached_pressed = 0;
static TickType_t s_cached_tick = 0;
static bool s_cache_valid = false;

void touch_Init(void)
{
  if (s_touch_mutex == NULL) {
    s_touch_mutex = xSemaphoreCreateMutex();
  }

  uint8_t data = 0x00;
  esp_err_t ret = I2C_writr_buff(I2C_ADDR_FT3168,0x00,&data,1); //切换正常模式
  if (ret != ESP_OK) {
    printf("Warning: Touch controller initialization failed: %s\n", esp_err_to_name(ret));
    printf("Touch functionality may not work properly\n");
    // 不使用ESP_ERROR_CHECK，避免系统重启
  }
}

uint8_t getTouch(uint16_t *x,uint16_t *y)
{
  if (s_touch_mutex == NULL) {
    s_touch_mutex = xSemaphoreCreateMutex();
  }

  if (s_touch_mutex) {
    xSemaphoreTake(s_touch_mutex, portMAX_DELAY);
  }

  TickType_t now = xTaskGetTickCount();
  if (s_cache_valid && (now - s_cached_tick) < pdMS_TO_TICKS(TOUCH_CACHE_MS)) {
    if (x) {
      *x = s_cached_x;
    }
    if (y) {
      *y = s_cached_y;
    }
    uint8_t pressed = s_cached_pressed;
    if (s_touch_mutex) {
      xSemaphoreGive(s_touch_mutex);
    }
    return pressed;
  }

  uint8_t data = 0;
  uint8_t buf[4];
  uint8_t pressed = 0;
  uint16_t raw_x = 0;
  uint16_t raw_y = 0;

  I2C_read_buff(I2C_ADDR_FT3168,0x02,&data,1);
  if(data)
  {
    I2C_read_buff(I2C_ADDR_FT3168,0x03,buf,4);
    raw_y = (((uint16_t)buf[0] & 0x0f)<<8) | (uint16_t)buf[1];
    raw_x = (((uint16_t)buf[2] & 0x0f)<<8) | (uint16_t)buf[3];
    pressed = 1;
  }

  s_cached_x = raw_x;
  s_cached_y = raw_y;
  s_cached_pressed = pressed;
  s_cached_tick = now;
  s_cache_valid = true;

  if (x) {
    *x = s_cached_x;
  }
  if (y) {
    *y = s_cached_y;
  }

  if (s_touch_mutex) {
    xSemaphoreGive(s_touch_mutex);
  }
  return pressed;
}
