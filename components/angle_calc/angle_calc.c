#include "angle_calc.h"
#include "qmi8658c.h"
#include "MadgwickAHRS.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
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

static float last_pitch = 0, last_roll = 0, last_yaw = 0;
static float last_gyro[3] = {0};
// 低通滤波参数（可调，0.0~1.0，越大响应越快，建议0.2~0.5）
static float pitch_lpf_alpha = 0.25f;
static float roll_lpf_alpha = 0.3f;   // roll滤波系数，可以设置得稍微快一些
static float filtered_pitch = 0;
static float filtered_roll = 0;

/* Relative heading: gyro projected onto gravity. No magnetometer, so this drifts.
 * Sign: +yaw = muzzle right (diamond right on the action target). Flip REL_YAW_SIGN if reversed. */
#define REL_YAW_SIGN            (-1.0f)
#define YAW_STILL_DPS           0.6f
#define YAW_STILL_CONFIRM       8       /* ~200 ms at 40 Hz before freeze */
#define YAW_BIAS_ALPHA          0.08f   /* ~0.3 s lock when still */
#define YAW_RATE_FILT_ALPHA     0.2f
#define YAW_RATE_DEADBAND       0.12f   /* dps; ignore residual noise when moving */
#define YAW_ACC_NORM_MIN        50.0f   /* mg; ignore free-fall / bad samples */
#define YAW_DT_MIN              0.001f
#define YAW_DT_MAX              0.200f

static float yaw_rate_bias = 0.0f;
static float last_yaw_rate_filt = 0.0f;
static int64_t last_yaw_us = 0;
static int yaw_bias_inited = 0;
static int yaw_still_count = 0;

static float wrap_deg180(float a)
{
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

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

void angle_calc_get_gyro(float *gx, float *gy, float *gz) {
    if (gx) *gx = last_gyro[0];
    if (gy) *gy = last_gyro[1];
    if (gz) *gz = last_gyro[2];
}

void angle_calc_capture_yaw_bias(void) {
    yaw_rate_bias = last_yaw_rate_filt;
    yaw_bias_inited = 1;
    yaw_still_count = 0;
}

void angle_calc_update(void) {
    float acc[3], gyro[3];
    qmi8658_read_xyz(acc, gyro);
    // 坐标系变换，确保pitch立起为0°、平放为90°
    transform_coordinates(acc, gyro);
    last_gyro[0] = gyro[0];
    last_gyro[1] = gyro[1];
    last_gyro[2] = gyro[2];
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

    /* Heading rate = body gyro dotted with the up vector (accelerometer). */
    float anorm = sqrtf(ax * ax + ay * ay + az * az);
    int64_t now_us = esp_timer_get_time();
    float dt_s = dt;
    if (last_yaw_us != 0) {
        dt_s = (float)(now_us - last_yaw_us) * 1e-6f;
        if (dt_s < YAW_DT_MIN) {
            dt_s = YAW_DT_MIN;
        } else if (dt_s > YAW_DT_MAX) {
            dt_s = YAW_DT_MAX;
        }
    }
    last_yaw_us = now_us;

    if (anorm >= YAW_ACC_NORM_MIN) {
        float yaw_rate = REL_YAW_SIGN * (gyro[0] * ax + gyro[1] * ay + gyro[2] * az) / anorm;
        last_yaw_rate_filt = YAW_RATE_FILT_ALPHA * yaw_rate
                           + (1.0f - YAW_RATE_FILT_ALPHA) * last_yaw_rate_filt;

        float gyro_l1 = fabsf(gyro[0]) + fabsf(gyro[1]) + fabsf(gyro[2]);
        if (gyro_l1 < YAW_STILL_DPS) {
            if (yaw_still_count < YAW_STILL_CONFIRM) {
                yaw_still_count++;
            }
            if (!yaw_bias_inited) {
                yaw_rate_bias = yaw_rate;
                last_yaw_rate_filt = yaw_rate;
                yaw_bias_inited = 1;
            } else {
                yaw_rate_bias = YAW_BIAS_ALPHA * yaw_rate + (1.0f - YAW_BIAS_ALPHA) * yaw_rate_bias;
            }
            /* Resting: learn bias, do not integrate leftover offset. */
            if (yaw_still_count >= YAW_STILL_CONFIRM) {
                return;
            }
        } else {
            yaw_still_count = 0;
        }

        float residual = yaw_rate - yaw_rate_bias;
        if (fabsf(residual) > YAW_RATE_DEADBAND) {
            last_yaw = wrap_deg180(last_yaw + residual * dt_s);
        }
    }
}

void angle_calc_get(float *pitch, float *roll, float *yaw) {
    if (pitch) *pitch = last_pitch;
    if (roll) *roll = last_roll;
    if (yaw) *yaw = last_yaw;
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
