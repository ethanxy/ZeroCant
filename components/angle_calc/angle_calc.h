#ifndef ANGLE_CALC_H
#define ANGLE_CALC_H

#ifdef __cplusplus
extern "C" {
#endif

void angle_calc_init(float sample_period, float beta);
void angle_calc_update(void); // 自动读取qmi8658c，更新姿态
void angle_calc_get(float *pitch, float *roll, float *yaw);
// 设置pitch低通滤波系数（0.01~1.0，越大响应越快，默认0.25）
void angle_calc_set_pitch_lpf_alpha(float alpha);
// 设置roll低通滤波系数（0.01~1.0，越大响应越快，默认0.3）
void angle_calc_set_roll_lpf_alpha(float alpha);

// 水平校准功能
void angle_calc_calibrate_level(void);      // 将当前角度设为水平基准
void angle_calc_reset_calibration(void);    // 重置校准偏移
void angle_calc_load_calibration(void);     // 从NVS加载校准数据
void angle_calc_save_calibration(void);     // 保存校准数据到NVS

#ifdef __cplusplus
}
#endif

#endif // ANGLE_CALC_H
