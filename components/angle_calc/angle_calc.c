#include "angle_calc.h"
#include "qmi8658c.h"
#include "MadgwickAHRS.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <math.h>

// NVS相关常量
#define NVS_NAMESPACE "angle_calib"
#define NVS_PITCH_OFFSET_KEY "pitch_offset"
#define NVS_ROLL_OFFSET_KEY "roll_offset"

static const char* TAG = "angle_calc";

extern float q0, q1, q2, q3;

static float dt = 0.01f;

void angle_calc_init(float sample_period, float beta) {
    dt = sample_period;
    MadgwickSetSamplePeriod(dt);
    MadgwickSetBeta(beta);
    qmi8658_init();
    // 初始化时加载校准数据
    angle_calc_load_calibration();
}

static float last_pitch = 0, last_roll = 0;
// 低通滤波参数（可调，0.0~1.0，越大响应越快，建议0.2~0.5）
static float pitch_lpf_alpha = 0.25f;
static float roll_lpf_alpha = 0.3f;   // roll滤波系数，可以设置得稍微快一些
static float filtered_pitch = 0;
static float filtered_roll = 0;

// 校准偏移值
static float pitch_offset = 0.0f;
static float roll_offset = 0.0f;

void angle_calc_set_pitch_lpf_alpha(float alpha) {
    if(alpha < 0.01f) alpha = 0.01f;
    if(alpha > 1.0f) alpha = 1.0f;
    pitch_lpf_alpha = alpha;
}

void angle_calc_set_roll_lpf_alpha(float alpha) {
    if(alpha < 0.01f) alpha = 0.01f;
    if(alpha > 1.0f) alpha = 1.0f;
    roll_lpf_alpha = alpha;
}

// 坐标系变换：绕X轴旋转90度，使pitch定义为立起0°、平放90°
static void transform_coordinates(float acc[3], float gyro[3]) {
    float ax = acc[0], ay = acc[1], az = acc[2];
    float gx = gyro[0], gy = gyro[1], gz = gyro[2];
    // 加速度转换
    acc[0] = ax;     // X不变
    acc[1] = -az;    // 原Z变为新Y（反向）
    acc[2] = ay;     // 原Y变为新Z
    // 陀螺仪转换
    gyro[0] = gx;    // X不变
    gyro[1] = -gz;   // 原Z角速度变为Y分量
    gyro[2] = gy;    // 原Y角速度变为Z分量
}

void angle_calc_update(void) {
    float acc[3], gyro[3];
    qmi8658_read_xyz(acc, gyro);
    // 坐标系变换，确保pitch立起为0°、平放为90°
    transform_coordinates(acc, gyro);
    float ax = acc[0];
    float ay = acc[1];
    float az = acc[2];
    // pitch: 立起为0，平放为90，前倾为正，后仰为负
    float pitch = atan2f(-ay, sqrtf(ax * ax + az * az)) * 180.0f / 3.1415926f;
    // roll: 左右倾斜有正负，平放和立起都为0
    float roll = atan2f(az, ax) * 180.0f / 3.1415926f;
    
    // 低通滤波处理
    static int first = 1;
    if(first) { 
        filtered_pitch = pitch; 
        filtered_roll = roll;
        first = 0; 
    } else { 
        filtered_pitch = pitch_lpf_alpha * pitch + (1.0f - pitch_lpf_alpha) * filtered_pitch;
        filtered_roll = roll_lpf_alpha * roll + (1.0f - roll_lpf_alpha) * filtered_roll;
    }
    
    // 应用校准偏移
    last_pitch = filtered_pitch - pitch_offset;
    last_roll = filtered_roll - roll_offset;
}

void angle_calc_get(float *pitch, float *roll, float *yaw) {
    if (pitch) *pitch = last_pitch;
    if (roll) *roll = last_roll;
    if (yaw) *yaw = 0.0f; // 静态加速度计无法得出yaw
}

// 水平校准功能：将当前角度设为水平基准
void angle_calc_calibrate_level(void) {
    // 使用当前滤波后的角度作为偏移基准（不含之前的偏移）
    pitch_offset = filtered_pitch;
    roll_offset = filtered_roll;
    
    ESP_LOGI(TAG, "Level calibration set: pitch_offset=%.2f°, roll_offset=%.2f°", 
             pitch_offset, roll_offset);
    
    // 自动保存到NVS
    angle_calc_save_calibration();
}

// 重置校准偏移
void angle_calc_reset_calibration(void) {
    pitch_offset = 0.0f;
    roll_offset = 0.0f;
    
    ESP_LOGI(TAG, "Calibration reset to zero");
    
    // 自动保存到NVS
    angle_calc_save_calibration();
}

// 从NVS加载校准数据
void angle_calc_load_calibration(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // 尝试打开NVS命名空间
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(err));
        pitch_offset = 0.0f;
        roll_offset = 0.0f;
        return;
    }
    
    // 读取pitch偏移
    size_t required_size = sizeof(float);
    err = nvs_get_blob(nvs_handle, NVS_PITCH_OFFSET_KEY, &pitch_offset, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved pitch offset found, using default: 0.0°");
        pitch_offset = 0.0f;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error reading pitch offset from NVS: %s", esp_err_to_name(err));
        pitch_offset = 0.0f;
    }
    
    // 读取roll偏移
    required_size = sizeof(float);
    err = nvs_get_blob(nvs_handle, NVS_ROLL_OFFSET_KEY, &roll_offset, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved roll offset found, using default: 0.0°");
        roll_offset = 0.0f;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error reading roll offset from NVS: %s", esp_err_to_name(err));
        roll_offset = 0.0f;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Loaded calibration: pitch_offset=%.2f°, roll_offset=%.2f°", 
             pitch_offset, roll_offset);
}

// 保存校准数据到NVS
void angle_calc_save_calibration(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // 尝试打开NVS命名空间
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s' for writing: %s", 
                NVS_NAMESPACE, esp_err_to_name(err));
        return;
    }
    
    // 保存pitch偏移
    err = nvs_set_blob(nvs_handle, NVS_PITCH_OFFSET_KEY, &pitch_offset, sizeof(float));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save pitch offset to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    // 保存roll偏移
    err = nvs_set_blob(nvs_handle, NVS_ROLL_OFFSET_KEY, &roll_offset, sizeof(float));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save roll offset to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    // 提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit calibration to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Successfully saved calibration to NVS: pitch_offset=%.2f°, roll_offset=%.2f°", 
             pitch_offset, roll_offset);
}
