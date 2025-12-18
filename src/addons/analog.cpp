#include "addons/analog.h"
#include "config.pb.h"
#include "enums.pb.h"
#include "hardware/adc.h"
#include "helper.h"
#include "storagemanager.h"
#include "drivermanager.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ADC_MAX ((1 << 12) - 1) // 4095
#define ADC_PIN_OFFSET 26
#define ANALOG_MAX 1.0f
#define ANALOG_CENTER 0.5f
#define ANALOG_MINIMUM 0.0f
#define CIRCULARITY_DATA_SIZE 48

bool AnalogInput::available() {
    return Storage::getInstance().getAddonOptions().analogOptions.enabled;
}

void AnalogInput::setup() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    
    // Setup our ADC Pair of Sticks
    adc_pairs[0].x_pin = analogOptions.analogAdc1PinX;
    adc_pairs[0].y_pin = analogOptions.analogAdc1PinY;
    adc_pairs[0].analog_invert = analogOptions.analogAdc1Invert;
    adc_pairs[0].analog_dpad = analogOptions.analogAdc1Mode;
    adc_pairs[0].in_deadzone = analogOptions.inner_deadzone / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    adc_pairs[0].anti_deadzone = analogOptions.anti_deadzone / 100.0f;
    adc_pairs[0].joystick_center_x = analogOptions.joystick_center_x;
    adc_pairs[0].joystick_center_y = analogOptions.joystick_center_y;
    // Jitter filter (0 = disabled)
    adc_pairs[0].jitter_filter = analogOptions.has_joystick_jitter_filter_1 ? analogOptions.joystick_jitter_filter_1 : 0;
    // Initialize range calibration data (48 angular positions)
    adc_pairs[0].has_range_calibration = (analogOptions.joystick_range_data_1_count > 0);
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (i < analogOptions.joystick_range_data_1_count && analogOptions.joystick_range_data_1[i] > 0.0f) {
            adc_pairs[0].range_data[i] = analogOptions.joystick_range_data_1[i];
        } else {
            adc_pairs[0].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
    // Initialize finetune shape adjustment settings
    adc_pairs[0].finetune_shape_force_circular = analogOptions.has_joystick_finetune_shape_force_circular_1 ? 
        analogOptions.joystick_finetune_shape_force_circular_1 : false;
    adc_pairs[0].finetune_shape_amplify = analogOptions.has_joystick_finetune_shape_amplify_1 ? 
        analogOptions.joystick_finetune_shape_amplify_1 : 0.0f;
    adc_pairs[1].x_pin = analogOptions.analogAdc2PinX;
    adc_pairs[1].y_pin = analogOptions.analogAdc2PinY;
    adc_pairs[1].analog_invert = analogOptions.analogAdc2Invert;
    adc_pairs[1].analog_dpad = analogOptions.analogAdc2Mode;
    adc_pairs[1].in_deadzone = analogOptions.inner_deadzone2 / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    adc_pairs[1].anti_deadzone = analogOptions.anti_deadzone2 / 100.0f;
    adc_pairs[1].joystick_center_x = analogOptions.joystick_center_x2;
    adc_pairs[1].joystick_center_y = analogOptions.joystick_center_y2;
    // Jitter filter (0 = disabled)
    adc_pairs[1].jitter_filter = analogOptions.has_joystick_jitter_filter_2 ? analogOptions.joystick_jitter_filter_2 : 0;
    // Initialize range calibration data (48 angular positions)
    adc_pairs[1].has_range_calibration = (analogOptions.joystick_range_data_2_count > 0);
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (i < analogOptions.joystick_range_data_2_count && analogOptions.joystick_range_data_2[i] > 0.0f) {
            adc_pairs[1].range_data[i] = analogOptions.joystick_range_data_2[i];
        } else {
            adc_pairs[1].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
    // Initialize finetune shape adjustment settings
    adc_pairs[1].finetune_shape_force_circular = analogOptions.has_joystick_finetune_shape_force_circular_2 ? 
        analogOptions.joystick_finetune_shape_force_circular_2 : false;
    adc_pairs[1].finetune_shape_amplify = analogOptions.has_joystick_finetune_shape_amplify_2 ? 
        analogOptions.joystick_finetune_shape_amplify_2 : 0.0f;
    
    // Apply finetune shape adjustments to range_data for both sticks
    applyFinetuneShapeAdjustments(0);
    applyFinetuneShapeAdjustments(1);

    // Setup defaults and helpers
    for (int i = 0; i < ADC_COUNT; i++) {
        adc_pairs[i].x_pin_adc = adc_pairs[i].x_pin - ADC_PIN_OFFSET;
        adc_pairs[i].y_pin_adc = adc_pairs[i].y_pin - ADC_PIN_OFFSET;
        adc_pairs[i].x_value = ANALOG_CENTER;
        adc_pairs[i].y_value = ANALOG_CENTER;
        // Initialize jitter filter state (0 = no previous sample yet)
        adc_pairs[i].last_x_adc = 0;
        adc_pairs[i].last_y_adc = 0;
        // Note: has_range_calibration is set during range data initialization above
    }

    // Initialize center X/Y for each pair using manual calibration values
    for (int i = 0; i < ADC_COUNT; i++) {
        if(isValidPin(adc_pairs[i].x_pin)) {
            adc_gpio_init(adc_pairs[i].x_pin);
            // Always use stored manual calibration value
                adc_pairs[i].x_center = adc_pairs[i].joystick_center_x;
        }
        if(isValidPin(adc_pairs[i].y_pin)) {
            adc_gpio_init(adc_pairs[i].y_pin);
            // Always use stored manual calibration value
                adc_pairs[i].y_center = adc_pairs[i].joystick_center_y;
        }
    }
}

void AnalogInput::process() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    
    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    if ( DriverManager::getInstance().getDriver() != nullptr ) {
        uint32_t joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
        joystickMax = joystickMid * 2;
    }

    for(int i = 0; i < ADC_COUNT; i++) {
        // Step 1: Read raw ADC values and transform to center-relative coordinates
        // Combines: ADC read + center offset + move to center coordinate system
        // Result: centered coordinates where (0,0) is stick center
        float cx = 0.0f, cy = 0.0f;
        if (isValidPin(adc_pairs[i].x_pin)) {
            cx = readPin(i, adc_pairs[i].x_pin_adc, adc_pairs[i].x_center, true) - (float)adc_pairs[i].x_center;
        }
        if (isValidPin(adc_pairs[i].y_pin)) {
            cy = readPin(i, adc_pairs[i].y_pin_adc, adc_pairs[i].y_center, false) - (float)adc_pairs[i].y_center;
        }

        // Step 2: Range calibration scaling (radial scaling)
        // scale is always > 0: 1.0 when uncalibrated, calibrated value when calibrated
        float scale = getInterpolatedScale(i, std::atan2(cy, cx));
        float sx = cx / scale;
        float sy = cy / scale;

        // Step 3: Normalize to [0.0, 1.0] range and apply inversion
        float x_value = sx / ADC_MAX + ANALOG_CENTER;
        float y_value = sy / ADC_MAX + ANALOG_CENTER;

        if (adc_pairs[i].analog_invert == InvertMode::INVERT_X || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            x_value = ANALOG_MAX - x_value;
        }
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_Y || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            y_value = ANALOG_MAX - y_value;
        }

        // Step 4: Apply deadzone and anti-deadzone (optimized: defer sqrt)
        float mx = x_value - ANALOG_CENTER;
        float my = y_value - ANALOG_CENTER;
        float dist_sq = mx * mx + my * my;
        float deadzone_sq = adc_pairs[i].in_deadzone * adc_pairs[i].in_deadzone;
        
        if (dist_sq < deadzone_sq) {
            // Inside deadzone: no sqrt needed
            x_value = ANALOG_CENTER;
            y_value = ANALOG_CENTER;
        } else if (adc_pairs[i].anti_deadzone > 0.0f) {
            // Only compute sqrt when anti-deadzone is enabled
            float dist = std::sqrt(dist_sq);
            float normalized = std::min(dist / ANALOG_CENTER, 1.0f);
            float baseline = std::clamp(adc_pairs[i].anti_deadzone, 0.0f, 1.0f);
            if (normalized < baseline) {
                float scale_factor = (baseline * ANALOG_CENTER) / dist;
                x_value = mx * scale_factor + ANALOG_CENTER;
                y_value = my * scale_factor + ANALOG_CENTER;
            }
        }

        // Step 5: Square trimming (DS4-style) - clamp to [-1, 1] then back to [0, 1]
        float nx = (x_value - ANALOG_CENTER) * 2.0f;
        float ny = (y_value - ANALOG_CENTER) * 2.0f;
        float tx, ty;
        trimToSquare(nx, ny, tx, ty);
        x_value = tx * 0.5f + ANALOG_CENTER;
        y_value = ty * 0.5f + ANALOG_CENTER;

        // Store values
        adc_pairs[i].x_value = x_value;
        adc_pairs[i].y_value = y_value;

        // Convert to gamepad protocol format
        uint16_t clampedX = (uint16_t)std::min((uint32_t)(joystickMax * std::min(x_value, 1.0f)), (uint32_t)0xFFFF);
        uint16_t clampedY = (uint16_t)std::min((uint32_t)(joystickMax * std::min(y_value, 1.0f)), (uint32_t)0xFFFF);

        if (adc_pairs[i].analog_dpad == DpadMode::DPAD_MODE_LEFT_ANALOG) {
            gamepad->state.lx = clampedX;
            gamepad->state.ly = clampedY;
        } else if (adc_pairs[i].analog_dpad == DpadMode::DPAD_MODE_RIGHT_ANALOG) {
            gamepad->state.rx = clampedX;
            gamepad->state.ry = clampedY;
        }
    }
}

float AnalogInput::readPin(int stick_num, Pin_t pin_adc, uint16_t /* center */, bool isXAxis) {
    adc_select_input(pin_adc);
    uint16_t adc_value = adc_read();

    // Jitter filtering in ADC domain:
    // Compare current ADC value with last value for this axis.
    // If absolute difference is smaller than jitter_filter, keep last value.
    uint32_t threshold = adc_pairs[stick_num].jitter_filter;
    uint16_t* last_adc = isXAxis ? &adc_pairs[stick_num].last_x_adc : &adc_pairs[stick_num].last_y_adc;

    if (threshold > 0) {
        // If last_adc still 0,第一次 diff≈adc_value，大概率>=threshold，直接通过并更新 last_adc
        uint32_t diff = (adc_value > *last_adc) ? (adc_value - *last_adc) : (*last_adc - adc_value);
        if (diff < threshold) {
            // 差值小于抖动过滤值，输出上次 ADC 值
            return static_cast<float>(*last_adc);
        }
        // 差值大于等于阈值，接受新值并更新 last_adc
        *last_adc = adc_value;
    } else {
        // 阈值为 0：不做防抖，但仍然更新 last_adc，方便以后启用时使用
        *last_adc = adc_value;
    }

    // 返回（可能已防抖后的）ADC 值
    return static_cast<float>(adc_value);
}

/**
 * Get interpolated scale for a given angle using range calibration data
 * Note: range_data has already been adjusted by applyFinetuneShapeAdjustments() during initialization
 * @param stick_num Stick number (0 or 1)
 * @param angle Angle in radians (-PI to PI)
 * @return Scale value (ratio of actual outer radius to standard radius), or 1.0 if no calibration data (1:1 native output)
 */
float AnalogInput::getInterpolatedScale(int stick_num, float angle) {
    // Check if we have calibration data (use flag set during setup to avoid checking all 48 indices)
    if (!adc_pairs[stick_num].has_range_calibration) {
        return 1.0f;  // No calibration data: use 1:1 scaling (native output)
    }
    
    // Convert angle from [-PI, PI] to [0, 2*PI] then to [0, CIRCULARITY_DATA_SIZE]
    float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);  // 0.0 to 1.0
    float index = normalizedAngle * CIRCULARITY_DATA_SIZE;
    
    // Get the two adjacent indices for interpolation
    int i0 = ((int)std::floor(index)) % CIRCULARITY_DATA_SIZE;
    int i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
    float t = index - std::floor(index);  // Fractional part (0.0 to 1.0)
    
    // Linear interpolation of adjusted calibration data
    // Note: range_data has already been adjusted by applyFinetuneShapeAdjustments() during initialization
    // and square trimming has been applied, so we can directly interpolate
    float r0 = adc_pairs[stick_num].range_data[i0];
    float r1 = adc_pairs[stick_num].range_data[i1];
    return r0 * (1.0f - t) + r1 * t;
}

/**
 * Apply finetune shape adjustments to range_data during initialization
 * Modifies range_data array based on force circular and amplify settings
 * @param stick_num Stick number (0 or 1)
 */
void AnalogInput::applyFinetuneShapeAdjustments(int stick_num) {
    if (!adc_pairs[stick_num].has_range_calibration) {
        return;  // No calibration data to adjust
    }
    
    if (!adc_pairs[stick_num].finetune_shape_force_circular) {
        // When force circular is disabled, set all scaling ratios to the minimum value
        // Find the minimum scaling ratio
        float minScale = adc_pairs[stick_num].range_data[0];
        for (int i = 1; i < CIRCULARITY_DATA_SIZE; i++) {
            if (adc_pairs[stick_num].range_data[i] < minScale) {
                minScale = adc_pairs[stick_num].range_data[i];
            }
        }
        
        // Set all scaling ratios to the minimum value
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            adc_pairs[stick_num].range_data[i] = minScale;
        }
    }
    // Note: When force_circular is true, scaling ratios remain unchanged at this point
    
    // Apply amplify factor to all scaling ratios (regardless of force_circular setting)
    float amplifyFactor = 1.0f + adc_pairs[stick_num].finetune_shape_amplify / 100.0f;
    if (amplifyFactor > 0.0f) {
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            if (adc_pairs[stick_num].range_data[i] > 0.0f) {
                adc_pairs[stick_num].range_data[i] /= amplifyFactor;
            }
        }
    }
    
    // Note: Square trimming is applied in process() function to the final output coordinates,
    // not to the scale ratios in range_data
}

/**
 * Trim cartesian coordinates to square [-1, 1] boundary (DS4-style square trimming)
 * @param x Input X coordinate
 * @param y Input Y coordinate
 * @param outX Output trimmed X coordinate
 * @param outY Output trimmed Y coordinate
 */
void AnalogInput::trimToSquare(float x, float y, float& outX, float& outY) {
    // Trim to -1,-1 to 1,1 square
    outX = std::max(-1.0f, std::min(1.0f, x));
    outY = std::max(-1.0f, std::min(1.0f, y));
            }

