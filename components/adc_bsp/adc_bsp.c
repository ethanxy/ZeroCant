#include <stdio.h>
#include "adc_bsp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "adc_bsp";
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = false;

// 温度传感器相关
static temperature_sensor_handle_t temp_sensor = NULL;
static bool temp_sensor_initialized = false;

// 多次采样和滤波参数
#define ADC_IMMEDIATE_SAMPLES 10    // 每次采样10次
#define ADC_SAMPLE_DELAY_MS 10      // 采样间隔10ms  
#define VOLTAGE_FILTER_ALPHA 0.9f   // 滤波系数（新值权重90%）

// 温度校准参数
#define TEMP_REFERENCE_C 25.0f      // 参考温度 25°C
#define TEMP_COEFF_PPM_PER_C -200.0f // 温度系数 -200ppm/°C (ESP32典型值)
#define TEMP_UPDATE_INTERVAL_MS 5000 // 温度更新间隔 5秒

// Waveshare ESP32-S3-Touch-AMOLED-1.64: VBAT -- 200K -- ADC -- 100K -- GND
// Official: V_BAT = V_ADC * 3  (schematic divider)
#define VOLTAGE_DIVIDER_RATIO 3.0f

// 滤波和温度校准相关变量
static float filtered_voltage = 0.0f;
static bool filter_initialized = false;
static float current_temperature = TEMP_REFERENCE_C;
static uint32_t last_temp_update = 0;
// 多次采样取平均值函数
static int adc_get_average_raw(int sample_count)
{
    int adc_sum = 0;
    int valid_samples = 0;
    
    for(int i = 0; i < sample_count; i++) {
        int adc_raw;
        if(adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &adc_raw) == ESP_OK) {
            adc_sum += adc_raw;
            valid_samples++;
        }
        if(i < sample_count - 1) {  // 最后一次不需要延时
            vTaskDelay(pdMS_TO_TICKS(ADC_SAMPLE_DELAY_MS));
        }
    }
    
    return valid_samples > 0 ? (adc_sum / valid_samples) : -1;
}

// ADC校准初始化函数
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration Success");
    } else {
        ESP_LOGW(TAG, "ADC calibration Failed");
    }
    return calibrated;
}

// 温度传感器初始化函数
static bool temperature_sensor_init(void)
{
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t ret = temperature_sensor_install(&temp_config, &temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Temperature sensor install failed");
        return false;
    }
    
    ret = temperature_sensor_enable(temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Temperature sensor enable failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Temperature sensor initialized successfully");
    temp_sensor_initialized = true;
    return true;
}

// 获取当前温度
static float get_current_temperature(void)
{
    if (!temp_sensor_initialized) {
        return TEMP_REFERENCE_C; // 返回参考温度
    }
    
    float temperature;
    esp_err_t ret = temperature_sensor_get_celsius(temp_sensor, &temperature);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read temperature, using reference value");
        return TEMP_REFERENCE_C;
    }
    
    return temperature;
}

// 温度校准函数
static float apply_temperature_compensation(float voltage, float temperature)
{
    // 计算温度偏差
    float temp_delta = temperature - TEMP_REFERENCE_C;
    
    // 计算温度系数补偿 (ppm/°C)
    float temp_correction = 1.0f + (TEMP_COEFF_PPM_PER_C * temp_delta / 1000000.0f);
    
    // 应用温度补偿
    float compensated_voltage = voltage * temp_correction;
    
    ESP_LOGD(TAG, "Temp compensation: %.2f°C, delta=%.2f°C, correction=%.6f, voltage %.3fV->%.3fV", 
             temperature, temp_delta, temp_correction, voltage, compensated_voltage);
    
    return compensated_voltage;
}

// 更新温度读数（避免频繁读取）
static void update_temperature_if_needed(void)
{
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (current_time - last_temp_update >= TEMP_UPDATE_INTERVAL_MS) {
        current_temperature = get_current_temperature();
        last_temp_update = current_time;
        ESP_LOGD(TAG, "Temperature updated: %.2f°C", current_temperature);
    }
}

void adc_bsp_init(void)
{
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1, //ADC1
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten = ADC_ATTEN_DB_12,//ADC_ATTEN_DB_12,         //    1.1          ADC_ATTEN_DB_12:3.3
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config));
  
  // 初始化ADC校准
  do_calibration = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_12, &adc1_cali_handle);
  if (do_calibration) {
    ESP_LOGI(TAG, "ADC calibration enabled for battery voltage monitoring");
  } else {
    ESP_LOGW(TAG, "ADC calibration not available, using raw conversion");
  }
  
  // 官方电压公式不依赖温度补偿；保留传感器初始化供其它调试用途
  if (temperature_sensor_init()) {
    ESP_LOGI(TAG, "Temperature sensor available (not used for battery voltage)");
    current_temperature = get_current_temperature();
    last_temp_update = xTaskGetTickCount() * portTICK_PERIOD_MS;
  } else {
    ESP_LOGW(TAG, "Temperature sensor unavailable");
  }
}
void adc_get_value(float *value)
{
  // Waveshare official path: ADC1_CH3 (GPIO4), V_BAT = V_pin * 3
  int avg_adc_raw = adc_get_average_raw(ADC_IMMEDIATE_SAMPLES);
  if (avg_adc_raw < 0) {
    ESP_LOGE(TAG, "All ADC samples failed");
    *value = 0;
    return;
  }

  float pin_v;
  if (do_calibration && adc1_cali_handle) {
    int voltage_mv;
    esp_err_t cali_err = adc_cali_raw_to_voltage(adc1_cali_handle, avg_adc_raw, &voltage_mv);
    if (cali_err == ESP_OK) {
      pin_v = (float)voltage_mv / 1000.0f;
    } else {
      ESP_LOGW(TAG, "ADC calibration failed, using raw conversion");
      pin_v = (float)avg_adc_raw * 3.3f / 4096.0f;
    }
  } else {
    pin_v = (float)avg_adc_raw * 3.3f / 4096.0f;
  }

  *value = pin_v * VOLTAGE_DIVIDER_RATIO;
  ESP_LOGD(TAG, "ADC raw_avg=%d pin=%.3fV bat=%.3fV", avg_adc_raw, pin_v, *value);
}
// ADC校准反初始化函数
static void adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

// ADC反初始化函数
void adc_bsp_deinit(void)
{
    if (do_calibration && adc1_cali_handle) {
        adc_calibration_deinit(adc1_cali_handle);
        adc1_cali_handle = NULL;
        do_calibration = false;
    }
    if (adc1_handle) {
        ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
        adc1_handle = NULL;
    }
    // 重置滤波器状态
    filter_initialized = false;
    filtered_voltage = 0.0f;
    ESP_LOGI(TAG, "ADC deinitialized");
}

// 调试函数：获取原始和滤波后的电压值（用于对比测试）
void adc_get_raw_voltage(float *raw_voltage, float *filtered_voltage_out)
{
    int avg_adc_raw = adc_get_average_raw(ADC_IMMEDIATE_SAMPLES);
    if(avg_adc_raw < 0) {
        *raw_voltage = 0;
        *filtered_voltage_out = filtered_voltage;
        return;
    }
    
    // 获取当前未滤波的电压值
    if (do_calibration && adc1_cali_handle) {
        int voltage_mv;
        esp_err_t cali_err = adc_cali_raw_to_voltage(adc1_cali_handle, avg_adc_raw, &voltage_mv);
        if (cali_err == ESP_OK) {
            *raw_voltage = ((float)voltage_mv / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
        } else {
            *raw_voltage = ((float)avg_adc_raw * 3.3f / 4096.0f) * VOLTAGE_DIVIDER_RATIO;
        }
    } else {
        *raw_voltage = ((float)avg_adc_raw * 3.3f / 4096.0f) * VOLTAGE_DIVIDER_RATIO;
    }
    
    *filtered_voltage_out = filtered_voltage;
}

/*测试demo*/
void adc_example(void* parmeter)
{
  adc_bsp_init();
  float battery_voltage;
  for(;;)
  {
    adc_get_value(&battery_voltage);
    printf("Battery voltage: %.3f V\n", battery_voltage);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
