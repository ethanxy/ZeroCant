#ifndef ADC_BSP_H
#define ADC_BSP_H

void adc_bsp_init(void);
void adc_bsp_deinit(void);
void adc_example(void* parmeter);
void adc_get_value(float *value);

// 调试函数：获取未滤波的电压值（用于对比测试）
void adc_get_raw_voltage(float *raw_voltage, float *filtered_voltage);

#endif