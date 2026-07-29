#ifndef BALLISTIC_MATH_H
#define BALLISTIC_MATH_H

#include <math.h>

/**
 * @file ballistic_math.h
 * @brief Mathematical utility functions for ballistic calculations
 * 
 * This header provides mathematical utility functions including
 * vector operations, unit conversions, and interpolation functions
 * used throughout the ballistic calculation system.
 */

/**
 * @brief Calculate vector magnitude
 * @param x X component
 * @param y Y component
 * @param z Z component
 * @return Vector magnitude
 */
double vector_magnitude(double x, double y, double z);

/**
 * @brief Calculate distance between two points
 * @param x1 X coordinate of point 1
 * @param y1 Y coordinate of point 1
 * @param x2 X coordinate of point 2
 * @param y2 Y coordinate of point 2
 * @return Distance between points
 */
double distance_2d(double x1, double y1, double x2, double y2);

/**
 * @brief Convert degrees to radians
 * @param degrees Angle in degrees
 * @return Angle in radians
 */
double deg_to_rad(double degrees);

/**
 * @brief Convert radians to degrees
 * @param radians Angle in radians
 * @return Angle in degrees
 */
double rad_to_deg(double radians);

/**
 * @brief Convert feet per second to meters per second
 * @param fps Velocity in feet per second
 * @return Velocity in meters per second
 */
double fps_to_mps(double fps);

/**
 * @brief Convert meters per second to feet per second
 * @param mps Velocity in meters per second
 * @return Velocity in feet per second
 */
double mps_to_fps(double mps);

/**
 * @brief Convert yards to meters
 * @param yards Distance in yards
 * @return Distance in meters
 */
double yards_to_meters(double yards);

/**
 * @brief Convert meters to yards
 * @param meters Distance in meters
 * @return Distance in yards
 */
double meters_to_yards(double meters);

/**
 * @brief Linear interpolation between two values
 * @param x0 First x value
 * @param y0 First y value
 * @param x1 Second x value
 * @param y1 Second y value
 * @param x Target x value
 * @return Interpolated y value
 */
double linear_interpolate(double x0, double y0, double x1, double y1, double x);

/**
 * @brief Table lookup with linear interpolation
 * @param table_x Array of x values (must be sorted)
 * @param table_y Array of corresponding y values
 * @param table_size Size of the tables
 * @param x Target x value
 * @return Interpolated y value
 */
double table_lookup(const double* table_x, const double* table_y, int table_size, double x);

/**
 * @brief Calculate atmospheric density ratio
 * @param altitude Altitude in meters
 * @param temperature Temperature in Celsius
 * @param pressure Pressure in hPa
 * @param humidity Relative humidity (0-1)
 * @return Density ratio relative to standard conditions
 */
double atmospheric_density_ratio(double altitude, double temperature, double pressure, double humidity);

/**
 * @brief Calculate speed of sound in air
 * @param temperature Temperature in Celsius
 * @return Speed of sound in m/s
 */
double speed_of_sound(double temperature);

/**
 * @brief Constrain value to range
 * @param value Input value
 * @param min_val Minimum value
 * @param max_val Maximum value
 * @return Constrained value
 */
double constrain(double value, double min_val, double max_val);

#endif // BALLISTIC_MATH_H
