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
    adc_pairs[0].ema_option = analogOptions.analog_smoothing;
    adc_pairs[0].ema_smoothing = analogOptions.smoothing_factor / 100.0f;
    // 动态防抖参数：默认值 delta_max=1.5%, alpha_max=95%
    adc_pairs[0].smoothing_delta_max = (analogOptions.smoothing_delta_max > 0.0f) ? 
        (analogOptions.smoothing_delta_max / 100.0f) : 0.015f;  // 默认1.5% = 0.015
    adc_pairs[0].smoothing_alpha_max = (analogOptions.smoothing_alpha_max > 0.0f) ? 
        (analogOptions.smoothing_alpha_max / 100.0f) : 0.95f;   // 默认95% = 0.95
    adc_pairs[0].error_rate = analogOptions.analog_error / 1000.0f;
    adc_pairs[0].in_deadzone = analogOptions.inner_deadzone / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    adc_pairs[0].anti_deadzone = analogOptions.anti_deadzone / 100.0f;
    adc_pairs[0].joystick_center_x = analogOptions.joystick_center_x;
    adc_pairs[0].joystick_center_y = analogOptions.joystick_center_y;
    // Initialize range calibration data (48 angular positions)
    adc_pairs[0].has_range_calibration = (analogOptions.joystick_range_data_1_count > 0);
    for (int i = 0; i < 48; i++) {
        if (i < analogOptions.joystick_range_data_1_count && analogOptions.joystick_range_data_1[i] > 0.0f) {
            adc_pairs[0].range_data[i] = analogOptions.joystick_range_data_1[i];
        } else {
            adc_pairs[0].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
    // Initialize finetune shape adjustment percentages (default 100% = no adjustment)
    adc_pairs[0].finetune_shape_x_top_percent = analogOptions.has_joystick_finetune_shape_x_top_percent_1 ? 
        analogOptions.joystick_finetune_shape_x_top_percent_1 : 100.0f;
    adc_pairs[0].finetune_shape_x_bottom_percent = analogOptions.has_joystick_finetune_shape_x_bottom_percent_1 ? 
        analogOptions.joystick_finetune_shape_x_bottom_percent_1 : 100.0f;
    adc_pairs[0].finetune_shape_y_left_percent = analogOptions.has_joystick_finetune_shape_y_left_percent_1 ? 
        analogOptions.joystick_finetune_shape_y_left_percent_1 : 100.0f;
    adc_pairs[0].finetune_shape_y_right_percent = analogOptions.has_joystick_finetune_shape_y_right_percent_1 ? 
        analogOptions.joystick_finetune_shape_y_right_percent_1 : 100.0f;
    adc_pairs[0].finetune_shape_force_circular = analogOptions.has_joystick_finetune_shape_force_circular_1 ? 
        analogOptions.joystick_finetune_shape_force_circular_1 : false;
    adc_pairs[0].finetune_shape_amplify = analogOptions.has_joystick_finetune_shape_amplify_1 ? 
        analogOptions.joystick_finetune_shape_amplify_1 : 0.0f;
    adc_pairs[1].x_pin = analogOptions.analogAdc2PinX;
    adc_pairs[1].y_pin = analogOptions.analogAdc2PinY;
    adc_pairs[1].analog_invert = analogOptions.analogAdc2Invert;
    adc_pairs[1].analog_dpad = analogOptions.analogAdc2Mode;
    adc_pairs[1].ema_option = analogOptions.analog_smoothing2;
    adc_pairs[1].ema_smoothing = analogOptions.smoothing_factor2 / 100.0f;
    // 动态防抖参数：默认值 delta_max=1.5%, alpha_max=95%
    adc_pairs[1].smoothing_delta_max = (analogOptions.smoothing_delta_max2 > 0.0f) ? 
        (analogOptions.smoothing_delta_max2 / 100.0f) : 0.015f;  // 默认1.5% = 0.015
    adc_pairs[1].smoothing_alpha_max = (analogOptions.smoothing_alpha_max2 > 0.0f) ? 
        (analogOptions.smoothing_alpha_max2 / 100.0f) : 0.95f;   // 默认95% = 0.95
    adc_pairs[1].error_rate = analogOptions.analog_error2 / 1000.0f;
    adc_pairs[1].in_deadzone = analogOptions.inner_deadzone2 / 100.0f;
    // Outer deadzone and forced_circularity removed - replaced by range calibration
    adc_pairs[1].anti_deadzone = analogOptions.anti_deadzone2 / 100.0f;
    adc_pairs[1].joystick_center_x = analogOptions.joystick_center_x2;
    adc_pairs[1].joystick_center_y = analogOptions.joystick_center_y2;
    // Initialize range calibration data (48 angular positions)
    adc_pairs[1].has_range_calibration = (analogOptions.joystick_range_data_2_count > 0);
    for (int i = 0; i < 48; i++) {
        if (i < analogOptions.joystick_range_data_2_count && analogOptions.joystick_range_data_2[i] > 0.0f) {
            adc_pairs[1].range_data[i] = analogOptions.joystick_range_data_2[i];
        } else {
            adc_pairs[1].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
    // Initialize finetune shape adjustment percentages (default 100% = no adjustment)
    adc_pairs[1].finetune_shape_x_top_percent = analogOptions.has_joystick_finetune_shape_x_top_percent_2 ? 
        analogOptions.joystick_finetune_shape_x_top_percent_2 : 100.0f;
    adc_pairs[1].finetune_shape_x_bottom_percent = analogOptions.has_joystick_finetune_shape_x_bottom_percent_2 ? 
        analogOptions.joystick_finetune_shape_x_bottom_percent_2 : 100.0f;
    adc_pairs[1].finetune_shape_y_left_percent = analogOptions.has_joystick_finetune_shape_y_left_percent_2 ? 
        analogOptions.joystick_finetune_shape_y_left_percent_2 : 100.0f;
    adc_pairs[1].finetune_shape_y_right_percent = analogOptions.has_joystick_finetune_shape_y_right_percent_2 ? 
        analogOptions.joystick_finetune_shape_y_right_percent_2 : 100.0f;
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
        // Note: has_range_calibration is set during range data initialization above
        adc_pairs[i].x_ema = 0.0f;
        adc_pairs[i].y_ema = 0.0f;
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
    
    uint32_t joystickMid = GAMEPAD_JOYSTICK_MID;
    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    if ( DriverManager::getInstance().getDriver() != nullptr ) {
        joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
        joystickMax = joystickMid * 2; // 0x8000 mid must be 0x10000 max, but we reduce by 1 if we're maxed out
    }

    const float ADC_CENTER = ADC_MAX / 2.0f;  // 2047.5

    for(int i = 0; i < ADC_COUNT; i++) {
        // Step 1: Read raw ADC values (0-4095)
        float adc_x = 0.0f;
        float adc_y = 0.0f;
        
        if (isValidPin(adc_pairs[i].x_pin)) {
            adc_x = readPin(i, adc_pairs[i].x_pin_adc, adc_pairs[i].x_center);
        }
        if (isValidPin(adc_pairs[i].y_pin)) {
            adc_y = readPin(i, adc_pairs[i].y_pin_adc, adc_pairs[i].y_center);
        }

        // Step 2: Coordinate translation (offset transformation)
        // Calculate center offset values
        float dX_value = (float)adc_pairs[i].x_center - ADC_CENTER;
        float dY_value = (float)adc_pairs[i].y_center - ADC_CENTER;
        
        // Apply offset transformation
        float offset_x = adc_x - dX_value;  // adc_offset coordinate system
        float offset_y = adc_y - dY_value;  // adc_offset coordinate system

        // Step 3: Move to adc_offset_center coordinate system
        float offset_center_x = offset_x - ADC_CENTER;
        float offset_center_y = offset_y - ADC_CENTER;

        // Step 4: Range calibration scaling (radial scaling)
        float current_distance = std::sqrt(offset_center_x * offset_center_x + offset_center_y * offset_center_y);
        float angle = std::atan2(offset_center_y, offset_center_x);
        float scale = getInterpolatedScale(i, angle);
        
        float scaled_center_x = 0.0f;
        float scaled_center_y = 0.0f;
        
        if (scale > 0.0f && current_distance > 0.0f) {
            // Apply radial scaling
            scaled_center_x = offset_center_x / scale;
            scaled_center_y = offset_center_y / scale;
        } else {
            // No calibration data, use raw data
            scaled_center_x = offset_center_x;
            scaled_center_y = offset_center_y;
        }

        // Step 5: Normalize to [0.0, 1.0] range
        float x_value = scaled_center_x / ADC_MAX + ANALOG_CENTER;
        float y_value = scaled_center_y / ADC_MAX + ANALOG_CENTER;

        // Apply inversion if needed
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_X || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            x_value = ANALOG_MAX - x_value;
        }
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_Y || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            y_value = ANALOG_MAX - y_value;
        }

        // Apply EMA smoothing if enabled
        if (adc_pairs[i].ema_option) {
            x_value = emaCalculation(i, x_value, adc_pairs[i].x_ema);
            y_value = emaCalculation(i, y_value, adc_pairs[i].y_ema);
            adc_pairs[i].x_ema = x_value;
            adc_pairs[i].y_ema = y_value;
        }

        // Apply inner deadzone
        float x_magnitude = x_value - ANALOG_CENTER;
        float y_magnitude = y_value - ANALOG_CENTER;
        float distance = std::sqrt(x_magnitude * x_magnitude + y_magnitude * y_magnitude);
        
        if (distance < adc_pairs[i].in_deadzone) {
            x_value = ANALOG_CENTER;
            y_value = ANALOG_CENTER;
        } else {
            // Apply anti-deadzone if needed
            if (adc_pairs[i].anti_deadzone > 0.0f && distance > 0.0f) {
                float normalized = std::min(distance / ANALOG_CENTER, 1.0f);
                float baseline = std::clamp(adc_pairs[i].anti_deadzone, 0.0f, 1.0f);
                if (normalized < baseline) {
                    float targetLength = baseline * ANALOG_CENTER;
                    float scale_factor = targetLength / distance;
                    x_magnitude *= scale_factor;
                    y_magnitude *= scale_factor;
                    x_value = x_magnitude + ANALOG_CENTER;
                    y_value = y_magnitude + ANALOG_CENTER;
                }
            }
        }

        // Apply square trimming to prevent output values > 1.0 (DS4-style)
        // Convert from [0.0, 1.0] range (center 0.5) to [-1, 1] range (center 0) for trimming
        float x_normalized = (x_value - ANALOG_CENTER) * 2.0f;  // [0.0, 1.0] -> [-1, 1]
        float y_normalized = (y_value - ANALOG_CENTER) * 2.0f;  // [0.0, 1.0] -> [-1, 1]
        
        // Trim to square [-1, 1] boundary
        float trimmedX, trimmedY;
        trimToSquare(x_normalized, y_normalized, trimmedX, trimmedY);
        
        // Convert back to [0.0, 1.0] range
        // Note: Since trimmedX/Y are guaranteed to be in [-1, 1] by trimToSquare,
        // the converted values are guaranteed to be in [0.0, 1.0], no clamp needed
        x_value = trimmedX * 0.5f + ANALOG_CENTER;
        y_value = trimmedY * 0.5f + ANALOG_CENTER;

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

float AnalogInput::readPin(int stick_num, Pin_t pin_adc, uint16_t center) {
    adc_select_input(pin_adc);
    uint16_t adc_value = adc_read();
    // Return raw ADC value (0-4095) as float
    // Coordinate transformation will be done in process() function
    return (float)adc_value;
}

float AnalogInput::emaCalculation(int stick_num, float ema_value, float ema_previous) {
    float alpha_base = adc_pairs[stick_num].ema_smoothing;        // 基准 α（来自 WebConfig）
    float delta_max = adc_pairs[stick_num].smoothing_delta_max;   // 快速移动阈值（可配置）
    float alpha_max = adc_pairs[stick_num].smoothing_alpha_max;   // 最大 α（可配置）
    
    // 计算当前变化幅度（速度估计）
    float delta = std::abs(ema_value - ema_previous);
    
    // 防止除零错误：如果 delta_max 为 0 或非常小，使用默认值或直接使用基准 alpha
    if (delta_max <= 0.0001f) {  // 使用一个很小的阈值来避免除零
        // 如果 delta_max 无效，直接使用基准 alpha（不进行动态调整）
        float alpha_dynamic = std::clamp(alpha_base, 0.01f, 0.99f);
        return (alpha_dynamic * ema_value) + ((1.0f - alpha_dynamic) * ema_previous);
    }
    
    // 计算速度因子（0-1之间）
    float speed_factor = std::fmin(delta / delta_max, 1.0f);
    
    // 动态计算 α（立即响应，不进行平滑）
    float alpha_dynamic = alpha_base + (alpha_max - alpha_base) * speed_factor;
    
    // 边界保护
    alpha_dynamic = std::clamp(alpha_dynamic, 0.01f, 0.99f);
    
    // 计算 EMA（对摇杆值进行平滑，而不是对alpha）
    return (alpha_dynamic * ema_value) + ((1.0f - alpha_dynamic) * ema_previous);
}

/**
 * Get interpolated scale for a given angle using range calibration data
 * Note: range_data has already been adjusted by applyFinetuneShapeAdjustments() during initialization
 * @param stick_num Stick number (0 or 1)
 * @param angle Angle in radians (-PI to PI)
 * @return Scale value (ratio of actual outer radius to standard radius), or 0.0 if no calibration data
 */
float AnalogInput::getInterpolatedScale(int stick_num, float angle) {
    const int CIRCULARITY_DATA_SIZE = 48;
    
    // Check if we have calibration data (use flag set during setup to avoid checking all 48 indices)
    if (!adc_pairs[stick_num].has_range_calibration) {
        return 0.0f;  // No calibration data
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
 * Modifies range_data array based on force circular and percentage/amplify settings
 * @param stick_num Stick number (0 or 1)
 */
void AnalogInput::applyFinetuneShapeAdjustments(int stick_num) {
    if (!adc_pairs[stick_num].has_range_calibration) {
        return;  // No calibration data to adjust
    }
    
    const int CIRCULARITY_DATA_SIZE = 48;
    // Index mapping: angle = (index * 2π / 48) - π
    // 0° (right): index = 24
    // 90° (top): index = 36  
    // 180° (left): index = 0
    // 270° (bottom): index = 12
    const int cardinalIndices[4] = {24, 36, 0, 12};  // Right, Top, Left, Bottom
    
    if (adc_pairs[stick_num].finetune_shape_force_circular) {
        // Case 2: Force circular enabled - apply amplify factor to all indices
        // All scaling ratios = calibration_value / (1 + amplify%)
        float amplifyFactor = 1.0f + adc_pairs[stick_num].finetune_shape_amplify / 100.0f;
        if (amplifyFactor > 0.0f) {
            for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
                adc_pairs[stick_num].range_data[i] /= amplifyFactor;
            }
        }
    } else {
        // Case 1: Force circular disabled - apply percentage adjustments to cardinal indices only
        // Cardinal indices: calibration_value / (percent / 100)
        // Other indices: keep original calibration values (interpolation will be done by getInterpolatedScale)
        const float cardinalPercentFactors[4] = {
            adc_pairs[stick_num].finetune_shape_y_right_percent / 100.0f,    // Right (0°, index 24)
            adc_pairs[stick_num].finetune_shape_x_top_percent / 100.0f,      // Top (90°, index 36)
            adc_pairs[stick_num].finetune_shape_y_left_percent / 100.0f,      // Left (180°, index 0)
            adc_pairs[stick_num].finetune_shape_x_bottom_percent / 100.0f     // Bottom (270°, index 12)
        };
        
        // Apply percentage adjustments to cardinal indices (divide by percentage factor, same as amplify)
        for (int i = 0; i < 4; i++) {
            int cardIndex = cardinalIndices[i];
            if (cardinalPercentFactors[i] > 0.0f) {
                adc_pairs[stick_num].range_data[cardIndex] /= cardinalPercentFactors[i];
            }
        }
        // Non-cardinal indices keep their original calibration values
        // Interpolation between indices will be handled by getInterpolatedScale() at runtime
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

