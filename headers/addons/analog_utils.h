#ifndef _ANALOG_UTILS_H_
#define _ANALOG_UTILS_H_

#include "types.h"
#include "config.pb.h"

// ADC utility functions for joystick calibration
// These functions provide a unified interface for reading ADC values
// from joystick pins, used by both webconfig and miniLED calibration screens

/**
 * Read raw ADC values for a specific joystick stick
 * @param stickNum Stick number (0 for stick 1, 1 for stick 2)
 * @param x Output parameter for X-axis ADC value
 * @param y Output parameter for Y-axis ADC value
 * @return true if successful, false if analog input is not enabled or pins are invalid
 */
bool readJoystickADC(uint8_t stickNum, uint16_t& x, uint16_t& y);

/**
 * Calculate average center value from four calibration points
 * @param values Array of 8 values: [x1, y1, x2, y2, x3, y3, x4, y4]
 * @param avgX Output parameter for average X value
 * @param avgY Output parameter for average Y value
 */
void calculateCalibrationCenter(const uint16_t values[8], uint32_t& avgX, uint32_t& avgY);

/**
 * Save calibration values to storage
 * @param x1 Stick 1 X center value
 * @param y1 Stick 1 Y center value
 * @param x2 Stick 2 X center value
 * @param y2 Stick 2 Y center value
 */
void saveCalibrationValues(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);

#endif  // _ANALOG_UTILS_H_

