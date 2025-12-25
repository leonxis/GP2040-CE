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
#define ANALOG_MAX 1.0f
#define ANALOG_CENTER 0.5f
#define ANALOG_MINIMUM 0.0f
#define CIRCULARITY_DATA_SIZE 48

bool AnalogInput::available() {
    return Storage::getInstance().getAddonOptions().analogOptions.enabled;
}

void AnalogInput::setup() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    
    // Check if curve is enabled (default to enabled if not set)
    bool curveEnabled = analogOptions.has_joystick_curve_enabled ? analogOptions.joystick_curve_enabled : true;
    
    // Setup our ADC Pair of Sticks
    adc_pairs[0].x_pin = analogOptions.analogAdc1PinX;
    adc_pairs[0].y_pin = analogOptions.analogAdc1PinY;
    adc_pairs[0].analog_invert = analogOptions.analogAdc1Invert;
    adc_pairs[0].analog_dpad = analogOptions.analogAdc1Mode;
    adc_pairs[0].in_deadzone = analogOptions.inner_deadzone / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    // Clamp anti_deadzone to [0, 1] range (defensive: frontend validates 0-100, but clamp ensures safety)
    adc_pairs[0].anti_deadzone = std::clamp(analogOptions.anti_deadzone / 100.0f, 0.0f, 1.0f);
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
    // Initialize response curve points: build preprocessed array with start (0,0) + control points + end (1,1)
    // Note: Frontend saves control points sorted by x coordinate, so no sorting needed here
    // Frontend limits to max 3 points, protobuf also limits to max 3, so no need to check > 3
    // Relationship: curve_points_sorted_count = (joystick_curve_points_1_count > 0 && joystick_curve_enabled) ? (2 + joystick_curve_points_1_count) : 0
    //               (start point + control points + end point, or 0 if no curve or curve disabled)
    adc_pairs[0].curve_points_sorted_count = 0;
    adc_pairs[0].curve_segments_count = 0;
    if (curveEnabled && analogOptions.joystick_curve_points_1_count > 0) {
        initializeCurveSegments(0, reinterpret_cast<const AnalogCurvePoint*>(analogOptions.joystick_curve_points_1), analogOptions.joystick_curve_points_1_count);
    }
    adc_pairs[1].x_pin = analogOptions.analogAdc2PinX;
    adc_pairs[1].y_pin = analogOptions.analogAdc2PinY;
    adc_pairs[1].analog_invert = analogOptions.analogAdc2Invert;
    adc_pairs[1].analog_dpad = analogOptions.analogAdc2Mode;
    adc_pairs[1].in_deadzone = analogOptions.inner_deadzone2 / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    // Clamp anti_deadzone to [0, 1] range (defensive: frontend validates 0-100, but clamp ensures safety)
    adc_pairs[1].anti_deadzone = std::clamp(analogOptions.anti_deadzone2 / 100.0f, 0.0f, 1.0f);
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
    // Initialize response curve points: build preprocessed array with start (0,0) + control points + end (1,1)
    // Note: Frontend saves control points sorted by x coordinate, so no sorting needed here
    // Frontend limits to max 3 points, protobuf also limits to max 3, so no need to check > 3
    // Relationship: curve_points_sorted_count = (joystick_curve_points_2_count > 0 && joystick_curve_enabled) ? (2 + joystick_curve_points_2_count) : 0
    //               (start point + control points + end point, or 0 if no curve or curve disabled)
    adc_pairs[1].curve_points_sorted_count = 0;
    adc_pairs[1].curve_segments_count = 0;
    if (curveEnabled && analogOptions.joystick_curve_points_2_count > 0) {
        initializeCurveSegments(1, reinterpret_cast<const AnalogCurvePoint*>(analogOptions.joystick_curve_points_2), analogOptions.joystick_curve_points_2_count);
    }
    
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
        float dist = 0.0f;  // Will be computed only if needed
        float scale_factor = 0.0f;  // Store scale_factor if anti-deadzone is applied (for Step 6)
        
        if (dist_sq < deadzone_sq) {
            // Inside deadzone: set to center
            nx = 0.0f;
            ny = 0.0f;
            dist_sq = 0.0f;  // Update dist_sq after setting to center
        } else if (adc_pairs[i].anti_deadzone > 0.0f) {
            // Only compute sqrt when anti-deadzone is enabled
            // anti_deadzone is already clamped to [0, 1] during setup, no need to clamp again
            dist = std::sqrt(dist_sq);
            float baseline = adc_pairs[i].anti_deadzone;
            // Check dist > 0 to avoid division by zero when at center point with no deadzone
            if (dist > 0.0f && dist < baseline) {
                scale_factor = baseline / dist;
                nx = nx * scale_factor;
                ny = ny * scale_factor;
                // Note: dist and dist_sq update is deferred to Step 6 (curve application)
                // to avoid unnecessary computation when curve is not enabled
                // dist_sq remains as dist_sq_old (before scaling) for now
                // scale_factor is stored in outer scope variable, will be used in Step 6 if curve enabled
            }
        }

        // Step 5: Square trimming (DS4-style) - clamp to [-1, 1] square boundary
        // Check if square trimming changes the coordinates (if so, we need to recalculate magnitude)
        float nx_before = nx;
        float ny_before = ny;
        nx = std::clamp(nx, -1.0f, 1.0f);
        ny = std::clamp(ny, -1.0f, 1.0f);
        bool coords_changed = (nx != nx_before) || (ny != ny_before);

        // Step 6: Apply response curve if configured
        if (adc_pairs[i].curve_points_sorted_count > 0) {
            float magnitude_sq;
            float magnitude = -1.0f;
            
            if (coords_changed) {
                // Square trimming changed coordinates: recalculate from nx/ny
                magnitude_sq = nx * nx + ny * ny;
            } else {
                // Square trimming didn't change coordinates: reuse dist/dist_sq
                if (adc_pairs[i].anti_deadzone > 0.0f) {
                    // Anti-deadzone enabled: dist and dist_sq are computed
                    // Update dist/dist_sq if anti-deadzone was applied (scale_factor > 0)
                    if (scale_factor > 0.0f) {
                        magnitude_sq = dist_sq * scale_factor * scale_factor;
                        magnitude = dist * scale_factor;  // Reuse precomputed sqrt
                    } else {
                        // Anti-deadzone enabled but not applied (dist >= baseline)
                        magnitude_sq = dist_sq;
                        magnitude = dist;  // Reuse precomputed sqrt
                    }
                } else {
                    // Anti-deadzone not enabled: dist = 0, only dist_sq exists
                    magnitude_sq = dist_sq;
                    // magnitude remains -1.0f, will be computed in applyResponseCurveToCoordinates
                }
            }
            
            applyResponseCurveToCoordinates(nx, ny, i, magnitude_sq, magnitude);
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
    
    // Add start point (0, 0)
    adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {0.0f, 0.0f};
    
    // Add control points (already sorted by x from frontend)
    for (int i = 0; i < control_points_count; i++) {
        adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {
            control_points[i].x,
            control_points[i].y
        };
    }
    
    // Add end point (1, 1)
    adc_pairs[stick_num].curve_points_sorted[adc_pairs[stick_num].curve_points_sorted_count++] = {1.0f, 1.0f};
    
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
        adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].x_start_sq = p1x * p1x;
        adc_pairs[stick_num].curve_segments[adc_pairs[stick_num].curve_segments_count].x_end_sq = p2x * p2x;
        adc_pairs[stick_num].curve_segments_count++;
    }
    
    // Precompute extrapolation slope for magnitude > 1.0 (using last segment)
    int lastIdx = adc_pairs[stick_num].curve_points_sorted_count - 2;
    const float p1x = adc_pairs[stick_num].curve_points_sorted[lastIdx].x;
    const float p1y = adc_pairs[stick_num].curve_points_sorted[lastIdx].y;
    const float p2x = 1.0f;  // Last point is always (1.0, 1.0)
    const float p2y = 1.0f;
    adc_pairs[stick_num].curve_extrapolate_slope = (p2x == p1x) ? 1.0f : (p2y - p1y) / (p2x - p1x);
}

/**
 * Applies response curve to normalized coordinates based on distance from center
 * Calculates magnitude internally and applies curve scaling to both X and Y coordinates
 * Uses preprocessed sorted curve points array built during initialization
 * @param normalizedX Input/output X coordinate in [-1, 1] range
 * @param normalizedY Input/output Y coordinate in [-1, 1] range
 * @param stick_num Stick number (0 or 1)
 * @param magnitude_sq Precomputed magnitude squared (must be >= 0)
 * @param magnitude Optional precomputed magnitude (if < 0, will be calculated from magnitude_sq)
 */
void AnalogInput::applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stick_num, float magnitude_sq, float magnitude) {
    if (magnitude_sq <= 0.0f) {
        // At center point: no scaling needed (coordinates already 0)
        return;
    }
    
    // Note: Caller ensures curve_segments_count > 0, so no need to check here
    float curvedMagnitude;
    
    // Use squared values for segment lookup to avoid sqrt (since x >= 0, x_sq comparison is equivalent)
    // Find the segment containing the input value using squared comparison
    int segmentIdx = -1;
    for (int i = 0; i < adc_pairs[stick_num].curve_segments_count; i++) {
        if (magnitude_sq >= adc_pairs[stick_num].curve_segments[i].x_start_sq && 
            magnitude_sq <= adc_pairs[stick_num].curve_segments[i].x_end_sq) {
            segmentIdx = i;
            break;
        }
    }
    
    // Calculate magnitude only if not provided (optimization: reuse precomputed sqrt when available)
    // This saves ~20-30 CPU cycles when dist was already computed in Step 4 (anti-deadzone)
    if (magnitude < 0.0f) {
        magnitude = std::sqrt(magnitude_sq);
    }
    
    if (segmentIdx >= 0) {
        // Magnitude within curve definition range [0, 1]: use precomputed segment parameters
        // Use precomputed slope and intercept: curvedMagnitude = intercept + magnitude * slope
        curvedMagnitude = adc_pairs[stick_num].curve_segments[segmentIdx].intercept + 
                          magnitude * adc_pairs[stick_num].curve_segments[segmentIdx].slope;
    } else {
        // Magnitude > 1.0: extrapolate using precomputed slope
        // Extrapolate: curvedMagnitude = 1.0 + slope * (magnitude - 1.0)
        curvedMagnitude = 1.0f + adc_pairs[stick_num].curve_extrapolate_slope * (magnitude - 1.0f);
    }
    
    // Calculate scale factor and apply to both coordinates (preserving direction)
    float scale = curvedMagnitude / magnitude;
    normalizedX = normalizedX * scale;
    normalizedY = normalizedY * scale;
}

