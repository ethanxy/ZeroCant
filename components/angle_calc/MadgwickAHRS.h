// MadgwickAHRS.h - Minimal C header for Madgwick filter
// https://x-io.co.uk/open-source-imu-and-ahrs-algorithms/
#ifndef MADGWICK_AHRS_H
#define MADGWICK_AHRS_H

void MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az);
void MadgwickGetEuler(float *pitch, float *roll, float *yaw);
void MadgwickSetSamplePeriod(float dt);
void MadgwickSetBeta(float beta);

#endif // MADGWICK_AHRS_H