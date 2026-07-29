#include <stdio.h>
#include "touch_bsp.h"
#include "i2c_bsp.h"
#include "esp_err.h"
#define I2C_ADDR_FT3168 0x38

void touch_Init(void)
{
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
  uint8_t data = 0;
  uint8_t buf[4];
  I2C_read_buff(I2C_ADDR_FT3168,0x02,&data,1);
  if(data)
  {
    I2C_read_buff(I2C_ADDR_FT3168,0x03,buf,4);
    *y = (((uint16_t)buf[0] & 0x0f)<<8) | (uint16_t)buf[1];
    *x = (((uint16_t)buf[2] & 0x0f)<<8) | (uint16_t)buf[3];
    return 1;
  }
  return 0;
}