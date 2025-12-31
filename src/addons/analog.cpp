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
#define ADC_MAX_HALF (ADC_MAX * 0.5f) // Precomputed: 2047.5, used for normalization
#define ADC_PIN_OFFSET 26
#define ANALOG_CENTER 0.5f
#define CIRCULARITY_DATA_SIZE 48

/**
 * Convert protobuf CurvePoint array to AnalogCurvePoint array
 * This function explicitly copies data to avoid relying on memory layout compatibility
 * @param protobuf_points Source array of protobuf CurvePoint structures
 * @param count Number of points to convert (max 3)
 * @param output Array to store converted AnalogCurvePoint structures (must have space for at least count elements)
 */
static void convertCurvePoints(const CurvePoint* protobuf_points, int count, AnalogCurvePoint* output) {
    for (int i = 0; i < count && i < 3; i++) {
        output[i].x = protobuf_points[i].x;
        output[i].y = protobuf_points[i].y;
        // buttonMask always has a default value (0) set during preset loading, so no need to check has_buttonMask
        output[i].buttonMask = protobuf_points[i].buttonMask;
    }
}

bool AnalogInput::available() {
    return Storage::getInstance().getAddonOptions().analogOptions.enabled;
}

void AnalogInput::setup() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    
    // Initialize curve profile tracking from saved config (0 = custom, 1-4 = preset 1-4)
    // Default value is 0, so no need to check has_curve_profile
    usage_curve_profile_1 = analogOptions.curve_profile_1;
    usage_curve_profile_2 = analogOptions.curve_profile_2;
    
    // Check if curve is enabled (protobuf default is false)
    bool curveEnabled = analogOptions.joystick_curve_enabled;
    
    // Setup our ADC Pair of Sticks
    adc_pairs[0].x_pin = analogOptions.analogAdc1PinX;
    adc_pairs[0].y_pin = analogOptions.analogAdc1PinY;
    adc_pairs[0].analog_invert = analogOptions.analogAdc1Invert;
    adc_pairs[0].analog_dpad = analogOptions.analogAdc1Mode;
    adc_pairs[0].in_deadzone = analogOptions.inner_deadzone / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    // Clamp anti_deadzone to [0, 1] range (defensive: frontend validates 0-10, but clamp ensures safety)
    adc_pairs[0].anti_deadzone = std::clamp(analogOptions.anti_deadzone / 100.0f, 0.0f, 1.0f);
    adc_pairs[0].fixed_anti_deadzone = analogOptions.fixed_anti_deadzone;
    adc_pairs[0].joystick_center_x = analogOptions.joystick_center_x;
    adc_pairs[0].joystick_center_y = analogOptions.joystick_center_y;
    // Jitter filter (0 = disabled, protobuf default is 0)
    adc_pairs[0].jitter_filter = analogOptions.joystick_jitter_filter_1;
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
    adc_pairs[0].finetune_shape_force_circular = analogOptions.joystick_finetune_shape_force_circular_1;
    adc_pairs[0].finetune_shape_amplify = analogOptions.joystick_finetune_shape_amplify_1;
    // Initialize response curve points: build preprocessed array with start (0,0) + control points + end (1,1)
    // Note: Frontend saves control points sorted by x coordinate, so no sorting needed here
    // Frontend limits to max 3 points, protobuf also limits to max 3, so no need to check > 3
    // Relationship: curve_points_sorted_count = (joystick_curve_points_1_count > 0 && joystick_curve_enabled) ? (2 + joystick_curve_points_1_count) : 0
    //               (start point + control points + end point, or 0 if no curve or curve disabled)
    adc_pairs[0].curve_points_sorted_count = 0;
    adc_pairs[0].curve_segments_count = 0;
    adc_pairs[0].active_control_points_mask = 0;
    if (curveEnabled && analogOptions.joystick_curve_points_1_count > 0) {
        // Convert protobuf CurvePoint array to AnalogCurvePoint array
        AnalogCurvePoint converted_points[3];
        convertCurvePoints(analogOptions.joystick_curve_points_1, analogOptions.joystick_curve_points_1_count, converted_points);
        initializeCurveSegments(0, converted_points, analogOptions.joystick_curve_points_1_count);
    }
    adc_pairs[1].x_pin = analogOptions.analogAdc2PinX;
    adc_pairs[1].y_pin = analogOptions.analogAdc2PinY;
    adc_pairs[1].analog_invert = analogOptions.analogAdc2Invert;
    adc_pairs[1].analog_dpad = analogOptions.analogAdc2Mode;
    adc_pairs[1].in_deadzone = analogOptions.inner_deadzone2 / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    // Clamp anti_deadzone to [0, 1] range (defensive: frontend validates 0-10, but clamp ensures safety)
    adc_pairs[1].anti_deadzone = std::clamp(analogOptions.anti_deadzone2 / 100.0f, 0.0f, 1.0f);
    adc_pairs[1].fixed_anti_deadzone = analogOptions.fixed_anti_deadzone2;
    adc_pairs[1].joystick_center_x = analogOptions.joystick_center_x2;
    adc_pairs[1].joystick_center_y = analogOptions.joystick_center_y2;
    // Jitter filter (0 = disabled, protobuf default is 0)
    adc_pairs[1].jitter_filter = analogOptions.joystick_jitter_filter_2;
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
    adc_pairs[1].finetune_shape_force_circular = analogOptions.joystick_finetune_shape_force_circular_2;
    adc_pairs[1].finetune_shape_amplify = analogOptions.joystick_finetune_shape_amplify_2;
    // Initialize response curve points: build preprocessed array with start (0,0) + control points + end (1,1)
    // Note: Frontend saves control points sorted by x coordinate, so no sorting needed here
    // Frontend limits to max 3 points, protobuf also limits to max 3, so no need to check > 3
    // Relationship: curve_points_sorted_count = (joystick_curve_points_2_count > 0 && joystick_curve_enabled) ? (2 + joystick_curve_points_2_count) : 0
    //               (start point + control points + end point, or 0 if no curve or curve disabled)
    adc_pairs[1].curve_points_sorted_count = 0;
    adc_pairs[1].curve_segments_count = 0;
    adc_pairs[1].active_control_points_mask = 0;
    if (curveEnabled && analogOptions.joystick_curve_points_2_count > 0) {
        // Convert protobuf CurvePoint array to AnalogCurvePoint array
        AnalogCurvePoint converted_points[3];
        convertCurvePoints(analogOptions.joystick_curve_points_2, analogOptions.joystick_curve_points_2_count, converted_points);
        initializeCurveSegments(1, converted_points, analogOptions.joystick_curve_points_2_count);
    }
    
    // Apply finetune shape adjustments to range_data for both sticks
    applyFinetuneShapeAdjustments(0);
    applyFinetuneShapeAdjustments(1);
    
    // Initialize hardware-related variables (GPIO, calibration centers, jitter filter state)
    // Setup defaults and helpers
    for (int i = 0; i < ADC_COUNT; i++) {
        adc_pairs[i].x_pin_adc = adc_pairs[i].x_pin - ADC_PIN_OFFSET;
        adc_pairs[i].y_pin_adc = adc_pairs[i].y_pin - ADC_PIN_OFFSET;
        adc_pairs[i].x_value = ANALOG_CENTER;
        adc_pairs[i].y_value = ANALOG_CENTER;
        // Initialize jitter filter state (0 = no previous sample yet)
        adc_pairs[i].last_x_adc = 0;
        adc_pairs[i].last_y_adc = 0;
    }

    // Initialize center X/Y for each pair using manual calibration values
    // If no calibration data (value is 0), use ADC midpoint (2047.5) as default
    for (int i = 0; i < ADC_COUNT; i++) {
        if(isValidPin(adc_pairs[i].x_pin)) {
            adc_gpio_init(adc_pairs[i].x_pin);
            // Use stored manual calibration value, or ADC midpoint if not calibrated
            adc_pairs[i].x_center = (adc_pairs[i].joystick_center_x > 0) ? 
                                    adc_pairs[i].joystick_center_x : 
                                    static_cast<uint16_t>(ADC_MAX_HALF);
        }
        if(isValidPin(adc_pairs[i].y_pin)) {
            adc_gpio_init(adc_pairs[i].y_pin);
            // Use stored manual calibration value, or ADC midpoint if not calibrated
            adc_pairs[i].y_center = (adc_pairs[i].joystick_center_y > 0) ? 
                                    adc_pairs[i].joystick_center_y : 
                                    static_cast<uint16_t>(ADC_MAX_HALF);
        }
    }
}

void AnalogInput::reinit() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    // Release old control point buttons before resetting data
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    forceReleaseActiveControlPoints(0, gamepad); // Release stick 1 virtual buttons
    forceReleaseActiveControlPoints(1, gamepad); // Release stick 2 virtual buttons
    // Reinitialize curve segments for both sticks (only data reinitialization, no hardware changes)
    // This is called when curve preset changes via hotkey, which only affects curve data
    adc_pairs[0].curve_points_sorted_count = 0;
    adc_pairs[0].curve_segments_count = 0;
    adc_pairs[0].active_control_points_mask = 0;  // Reset active control points mask
    if (analogOptions.joystick_curve_points_1_count > 0) {
        AnalogCurvePoint converted_points[3];
        convertCurvePoints(analogOptions.joystick_curve_points_1, analogOptions.joystick_curve_points_1_count, converted_points);
        initializeCurveSegments(0, converted_points, analogOptions.joystick_curve_points_1_count);
    }
    
    adc_pairs[1].curve_points_sorted_count = 0;
    adc_pairs[1].curve_segments_count = 0;
    adc_pairs[1].active_control_points_mask = 0;  // Reset active control points mask
    if (analogOptions.joystick_curve_points_2_count > 0) {
        AnalogCurvePoint converted_points[3];
        convertCurvePoints(analogOptions.joystick_curve_points_2, analogOptions.joystick_curve_points_2_count, converted_points);
        initializeCurveSegments(1, converted_points, analogOptions.joystick_curve_points_2_count);
    }

    // Note: Hardware initialization (GPIO, calibration, jitter filter) is NOT performed here
    // because curve preset switching only changes curve data, not hardware configuration.
    // Hardware initialization is only done in setup() when the addon is first initialized,
    // or when profile changes (handled by gp2040.cpp which may reload configuration).
}

void AnalogInput::process() {
    // Check if curve profile has changed (only identifier for hotkey preset switching)
    // Reinitialize curve data only when curve profile changes, not when curve enabled state changes
    // The reinit() function will check curve enabled state to decide whether to initialize curve segments
    
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    
    // Only check curve profile changes when curve is enabled
    // If curve is disabled, no need to check profile changes as reinit() won't initialize curve segments anyway
    if (analogOptions.joystick_curve_enabled) {
        // Get current curve profile values from config (0 = custom, 1-4 = preset 1-4)
        // Default value is 0, so no need to check has_curve_profile
        uint32_t current_profile_1 = analogOptions.curve_profile_1;
        uint32_t current_profile_2 = analogOptions.curve_profile_2;
        
        // Check if curve profile has changed for either stick (hotkey preset switching)
        // Reinitialize only when curve profile changes
        // reinit() will check curve enabled state to decide whether to initialize curve segments
        if (usage_curve_profile_1 != current_profile_1 || usage_curve_profile_2 != current_profile_2) {
            // Update tracking variables
            usage_curve_profile_1 = current_profile_1;
            usage_curve_profile_2 = current_profile_2;
            reinit();
        }
    }
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

        // Step 3: Normalize to [-1, 1] range (unified coordinate system) and apply inversion
        // Work in [-1, 1] coordinate system throughout to avoid precision loss from repeated conversions
        float nx = sx / ADC_MAX_HALF;  // Normalize to [-1, 1] range
        float ny = sy / ADC_MAX_HALF;

        if (adc_pairs[i].analog_invert == InvertMode::INVERT_X || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            nx = -nx;
        }
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_Y || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            ny = -ny;
        }

        // Step 4: Apply deadzone and anti-deadzone (in [-1, 1] coordinate system)
        float dist_sq = nx * nx + ny * ny;
        float deadzone_sq = adc_pairs[i].in_deadzone * adc_pairs[i].in_deadzone;
        
        if (dist_sq < deadzone_sq) {
            // Inside deadzone: set to center
            nx = 0.0f;
            ny = 0.0f;
            dist_sq = 0.0f;  // Update dist_sq after setting to center
        } else if (adc_pairs[i].anti_deadzone > 0.0f) {
            // Only compute sqrt when anti-deadzone is enabled
            // anti_deadzone is already clamped to [0, 1] during setup, no need to clamp again
            float dist = std::sqrt(dist_sq);
            float baseline = adc_pairs[i].anti_deadzone;
            
            if (adc_pairs[i].fixed_anti_deadzone) {
                // Fixed anti-deadzone mode: scale distance to baseline (fixed output)
                // Only applies when dist < baseline to provide a fixed minimum output
                // When dist >= baseline, no anti-deadzone is applied (normal output)
                if (dist > 0.0f && dist < baseline) {
                    float scale_factor = baseline / dist;
                    nx *= scale_factor;
                    ny *= scale_factor;
                }
            } else {
                // Linear anti-deadzone mode: add baseline to distance across the entire range
                // This maintains linear feel by adding a constant offset to all movements
                // Unlike fixed mode, this applies regardless of distance magnitude
                // Step 5 will clamp coordinates to [-1, 1] if they exceed the square boundary
                if (dist > 0.0f) {
                    float new_dist = dist + baseline;
                    float scale_factor = new_dist / dist;
                    nx *= scale_factor;
                    ny *= scale_factor;
                }
            }
        }

        // Step 5: Square trimming (DS4-style) - clamp to [-1, 1] square boundary
        nx = std::clamp(nx, -1.0f, 1.0f);
        ny = std::clamp(ny, -1.0f, 1.0f);

        // Step 6: Apply response curve if configured (control point button triggers are handled inside)
        if (adc_pairs[i].curve_points_sorted_count > 0) {
            applyResponseCurveToCoordinates(nx, ny, i, gamepad);
        }

        // Final conversion: Convert from [-1, 1] to [0, 1] range for storage and output
        float x_value = nx * 0.5f + ANALOG_CENTER;
        float y_value = ny * 0.5f + ANALOG_CENTER;

        // Store values
        adc_pairs[i].x_value = x_value;
        adc_pairs[i].y_value = y_value;

        // Convert to gamepad protocol format
        // Clamp x_value/y_value to [0, 1] then scale to [0, joystickMax], then clamp to uint16_t range
        float clamped_x = std::clamp(x_value, 0.0f, 1.0f);
        float clamped_y = std::clamp(y_value, 0.0f, 1.0f);
        uint16_t clampedX = (uint16_t)std::min((uint32_t)(joystickMax * clamped_x), (uint32_t)0xFFFF);
        uint16_t clampedY = (uint16_t)std::min((uint32_t)(joystickMax * clamped_y), (uint32_t)0xFFFF);

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
        // If last_adc is still 0 (first read), diff ≈ adc_value, likely >= threshold, pass through and update last_adc
        uint32_t diff = (adc_value > *last_adc) ? (adc_value - *last_adc) : (*last_adc - adc_value);
        if (diff < threshold) {
            // Difference is smaller than jitter filter threshold, return last ADC value
            return static_cast<float>(*last_adc);
        }
        // Difference is greater than or equal to threshold, accept new value and update last_adc
        *last_adc = adc_value;
    } else {
        // Threshold is 0: no jitter filtering, but still update last_adc for potential future use
        *last_adc = adc_value;
    }

    // Return (possibly jitter-filtered) ADC value
    return static_cast<float>(adc_value);
}


/**
 * Get interpolated scale for a given angle using range calibration data
 * Note: range_data has already been adjusted by applyFinetuneShapeAdjustments() during initialization
 * @param stick_num Stick number (0 or 1)
 * @param angle Angle in radians (-PI to PI)
 * @return Scale value (ratio of actual outer radius to standard radius), or 0.65 if no calibration data (default scaling)
 */
float AnalogInput::getInterpolatedScale(int stick_num, float angle) {
    // Check if we have calibration data (use flag set during setup to avoid checking all 48 indices)
    if (!adc_pairs[stick_num].has_range_calibration) {
        return 0.65f;  // No calibration data: use default scaling (0.65)
    }
    
    // Convert angle from [-PI, PI] to [0, 2*PI] then to [0, CIRCULARITY_DATA_SIZE]
    float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);  // 0.0 to 1.0
    float index = normalizedAngle * CIRCULARITY_DATA_SIZE;
    
    // Get the two adjacent indices for interpolation
    // Optimize: use conditional instead of modulo (saves ~15-30 CPU cycles per call)
    // i0 is in [0, CIRCULARITY_DATA_SIZE-1], i1 wraps around to 0 if i0 == CIRCULARITY_DATA_SIZE-1
    // Optimize: for positive numbers, (int) cast is equivalent to floor but faster (no function call)
    int i0 = (int)index;
    if (i0 >= CIRCULARITY_DATA_SIZE) {
        i0 = CIRCULARITY_DATA_SIZE - 1;  // Clamp to valid range (shouldn't happen, but defensive)
    }
    int i1 = (i0 + 1 < CIRCULARITY_DATA_SIZE) ? (i0 + 1) : 0;  // Wrap around using conditional (1-2 cycles vs 15-30 for modulo)
    float t = index - (float)i0;  // Fractional part (0.0 to 1.0) - faster than index - floor(index)
    
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
 * Initialize curve points array and compute segment parameters (slope and intercept) for fast lookup
 * Builds preprocessed array with start (0,0) + control points + end (1,1), then computes segment parameters
 * @param stick_num Stick number (0 or 1)
 * @param control_points Array of control points (already sorted by x coordinate from frontend)
 * @param control_points_count Number of control points (0-3)
 */
void AnalogInput::initializeCurveSegments(int stick_num, const AnalogCurvePoint* control_points, int control_points_count) {
    // Build preprocessed array: start (0,0) + control points + end (1,1)
    adc_pairs[stick_num].curve_points_sorted_count = 0;
    
    // Add start point (in_deadzone, anti_deadzone)
    adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {
        adc_pairs[stick_num].in_deadzone, 
        adc_pairs[stick_num].anti_deadzone,
        0  // Start point has no button mask
    };
    
    // Add control points (already sorted by x from frontend)
    for (int i = 0; i < control_points_count; i++) {
        adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {
            control_points[i].x,
            control_points[i].y,
            control_points[i].buttonMask
        };
    }
    
    // Add end point (1, 1)
    adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {
        1.0f, 
        1.0f,
        0  // End point has no button mask
    };
    
    // Precompute curve segment parameters for fast lookup
    adc_pairs[stick_num].curve_segments_count = 0;
    for (int i = 0; i < adc_pairs[stick_num].curve_points_sorted_count - 1; i++) {
        const float p1x = adc_pairs[stick_num].curve_points_sorted[i].x;
        const float p1y = adc_pairs[stick_num].curve_points_sorted[i].y;
        const float p2x = adc_pairs[stick_num].curve_points_sorted[i + 1].x;
        const float p2y = adc_pairs[stick_num].curve_points_sorted[i + 1].y;
        
        if (p2x == p1x) {
            // Vertical segment: slope is undefined, use 0 and set intercept to p1y
            adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].slope = 0.0f;
            adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].intercept = p1y;
        } else {
            float slope = (p2y - p1y) / (p2x - p1x);
            adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].slope = slope;
            adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].intercept = p1y - p1x * slope;
        }
        adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].x_start = p1x;
        adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].x_end = p2x;
        adc_pairs[stick_num].curve_segments_count++;
    }
}

/**
 * Applies response curve to normalized coordinates based on distance from center
 * Calculates magnitude internally and applies curve scaling to both X and Y coordinates
 * Uses preprocessed sorted curve points array built during initialization
 * Also handles control point button triggers (only when curve is enabled)
 * @param normalizedX Input/output X coordinate in [-1, 1] range
 * @param normalizedY Input/output Y coordinate in [-1, 1] range
 * @param stick_num Stick number (0 or 1)
 * @param gamepad Gamepad instance for button state updates
 */
 void AnalogInput::applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stick_num, Gamepad* gamepad) {
    // 1. Center deadzone handling
    if (normalizedX == 0.0f && normalizedY == 0.0f) {
        forceReleaseActiveControlPoints(stick_num, gamepad);
        return;
    }
    
    float clampdist_sq = normalizedX * normalizedX + normalizedY * normalizedY;
    float clampdist = std::sqrt(clampdist_sq);

    // ================== Button Logic (Optimized) ==================
    
    // Determine current segment index
    int segmentIdx = -1;
    if (clampdist > 1.0f) {
        segmentIdx = adc_pairs[stick_num].curve_segments_count; 
    } else {
        for (int i = 0; i < adc_pairs[stick_num].curve_segments_count; i++) {
            if (clampdist >= adc_pairs[stick_num].curve_segments[i].x_start && 
                clampdist < adc_pairs[stick_num].curve_segments[i].x_end) {
                segmentIdx = i;
                break;
            }
        }
        if (segmentIdx == -1) segmentIdx = adc_pairs[stick_num].curve_segments_count - 1;
    }

    if (gamepad != nullptr) {
        uint8_t old_active_points_mask = adc_pairs[stick_num].active_control_points_mask;
        uint8_t new_active_points_mask = 0;
        
        uint32_t buttons_active_now = 0;    // Combined button mask for all active points this frame
        uint32_t buttons_active_prev = 0;   // Combined button mask for all active points previous frame

        // Iterate through all control points (single loop completes all calculations)
        for (int j = 1; j < adc_pairs[stick_num].curve_points_sorted_count - 1; j++) {
            uint32_t btn = adc_pairs[stick_num].curve_points_sorted[j].buttonMask;
            uint8_t point_bit = 1U << (j - 1);
            bool is_active_now = (segmentIdx >= j);
            bool was_active_prev = ((old_active_points_mask & point_bit) != 0);

            // 1. Calculate current frame point status
            if (is_active_now) {
                new_active_points_mask |= point_bit;
                if (btn != 0) buttons_active_now |= btn;
            }

            // 2. Calculate previous frame point button status
            if (was_active_prev && btn != 0) {
                buttons_active_prev |= btn;
            }
        }

        // Update button state:
        // A. Press buttons that should be active (bitwise OR automatically handles duplicates:
        //    if multiple points trigger the same button, OR operation only keeps one 1)
        //    Note: |= 0 doesn't change state, so no conditional check needed
        gamepad->state.buttons |= buttons_active_now;

        // B. Release buttons: logic is (previous frame had) AND (this frame doesn't have)
        //    This perfectly resolves conflicts: if point1 and point2 both use the same button,
        //    and point2 deactivates but point1 is still active, (~buttons_active_now) protects
        //    this bit from being cleared
        //    Note: &= ~0 doesn't change state, so no conditional check needed
        gamepad->state.buttons &= ~(buttons_active_prev & ~buttons_active_now);

        // Save new control points mask
        adc_pairs[stick_num].active_control_points_mask = new_active_points_mask;
    }

    // ================== Coordinate Curve Logic ==================

    if (clampdist > 1.0f) {
        return; 
    }

    float curvedMagnitude;
    int validSegIdx = std::min(segmentIdx, (int)adc_pairs[stick_num].curve_segments_count - 1);
    
    if (validSegIdx >= 0) {
        curvedMagnitude = adc_pairs[stick_num].curve_segments[validSegIdx].intercept + 
                          clampdist * adc_pairs[stick_num].curve_segments[validSegIdx].slope;
    } else {
        curvedMagnitude = clampdist;
    }
    
    float scale = curvedMagnitude / clampdist;
    normalizedX = normalizedX * scale;
    normalizedY = normalizedY * scale;
}

// 在 AnalogInput 类中添加此私有辅助函数
void AnalogInput::forceReleaseActiveControlPoints(int stick_num, Gamepad* gamepad) {
    if (adc_pairs[stick_num].active_control_points_mask == 0) return;
    
    // Iterate through all possible control points (indices 1 to count-2)
    for (int j = 1; j < adc_pairs[stick_num].curve_points_sorted_count - 1; j++) {
        uint8_t control_point_index = j - 1;
        uint8_t bit_mask = 1U << control_point_index;
        
        // Check if this bit is marked as active
        if ((adc_pairs[stick_num].active_control_points_mask & bit_mask) != 0) {
            uint32_t button_mask = adc_pairs[stick_num].curve_points_sorted[j].buttonMask;
            if (button_mask != 0 && gamepad != nullptr) {
                gamepad->state.buttons &= ~button_mask; // Clear button
            }
        }
    }
    adc_pairs[stick_num].active_control_points_mask = 0;
}

