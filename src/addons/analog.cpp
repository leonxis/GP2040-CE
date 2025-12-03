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
    adc_pairs[0].out_deadzone = analogOptions.outer_deadzone / 100.0f;
    adc_pairs[0].anti_deadzone = analogOptions.anti_deadzone / 100.0f;
    adc_pairs[0].forced_circularity = analogOptions.forced_circularity;
    adc_pairs[0].joystick_center_x = analogOptions.joystick_center_x;
    adc_pairs[0].joystick_center_y = analogOptions.joystick_center_y;
    // Initialize range calibration data (48 angular positions)
    for (int i = 0; i < 48; i++) {
        if (i < analogOptions.joystick_range_data_1_count && analogOptions.joystick_range_data_1[i] > 0.0f) {
            adc_pairs[0].range_data[i] = analogOptions.joystick_range_data_1[i];
        } else {
            adc_pairs[0].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
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
    adc_pairs[1].out_deadzone = analogOptions.outer_deadzone2 / 100.0f;
    adc_pairs[1].anti_deadzone = analogOptions.anti_deadzone2 / 100.0f;
    adc_pairs[1].forced_circularity = analogOptions.forced_circularity2;
    adc_pairs[1].joystick_center_x = analogOptions.joystick_center_x2;
    adc_pairs[1].joystick_center_y = analogOptions.joystick_center_y2;
    // Initialize range calibration data (48 angular positions)
    for (int i = 0; i < 48; i++) {
        if (i < analogOptions.joystick_range_data_2_count && analogOptions.joystick_range_data_2[i] > 0.0f) {
            adc_pairs[1].range_data[i] = analogOptions.joystick_range_data_2[i];
        } else {
            adc_pairs[1].range_data[i] = 0.0f;  // Default: no calibration data
        }
    }
    

    // Setup defaults and helpers
    for (int i = 0; i < ADC_COUNT; i++) {
        adc_pairs[i].x_pin_adc = adc_pairs[i].x_pin - ADC_PIN_OFFSET;
        adc_pairs[i].y_pin_adc = adc_pairs[i].y_pin - ADC_PIN_OFFSET;
        adc_pairs[i].x_value = ANALOG_CENTER;
        adc_pairs[i].y_value = ANALOG_CENTER;
        adc_pairs[i].xy_magnitude = 0.0f;
        adc_pairs[i].x_magnitude = 0.0f;
        adc_pairs[i].y_magnitude = 0.0f;
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

    for(int i = 0; i < ADC_COUNT; i++) {
        // Read X-Axis
        if (isValidPin(adc_pairs[i].x_pin)) {
            adc_pairs[i].x_value = readPin(i, adc_pairs[i].x_pin_adc, adc_pairs[i].x_center);
            if (adc_pairs[i].analog_invert == InvertMode::INVERT_X || 
                adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
                adc_pairs[i].x_value = ANALOG_MAX - adc_pairs[i].x_value;
            }
            if (adc_pairs[i].ema_option) {
                adc_pairs[i].x_value = emaCalculation(i, adc_pairs[i].x_value, adc_pairs[i].x_ema);
                adc_pairs[i].x_ema = adc_pairs[i].x_value;
            }
        }
        // Read Y-Axis
        if (isValidPin(adc_pairs[i].y_pin)) {
            adc_pairs[i].y_value = readPin(i, adc_pairs[i].y_pin_adc, adc_pairs[i].y_center);
            if (adc_pairs[i].analog_invert == InvertMode::INVERT_Y || 
                adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
                adc_pairs[i].y_value = ANALOG_MAX - adc_pairs[i].y_value;
            }
            if (adc_pairs[i].ema_option) {
                adc_pairs[i].y_value = emaCalculation(i, adc_pairs[i].y_value, adc_pairs[i].y_ema);
                adc_pairs[i].y_ema = adc_pairs[i].y_value;
            }
        }
        // Look for dead-zones and circularity
        adc_pairs[i].xy_magnitude = magnitudeCalculation(i, adc_pairs[i]);
        if (adc_pairs[i].xy_magnitude < adc_pairs[i].in_deadzone) {
            adc_pairs[i].x_value = ANALOG_CENTER;
            adc_pairs[i].y_value = ANALOG_CENTER;
        } else {
            radialDeadzone(i, adc_pairs[i]);
        }

        // If MID is 0x8000, clamp our max to 0xFFFF incase we are at 0x10000. 0x7FFF will max at 0xFFFE
        uint16_t clampedX = (uint16_t)std::min((uint32_t)(joystickMax * std::min(adc_pairs[i].x_value, 1.0f)), (uint32_t)0xFFFF);
        uint16_t clampedY = (uint16_t)std::min((uint32_t)(joystickMax * std::min(adc_pairs[i].y_value, 1.0f)), (uint32_t)0xFFFF);

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
    // Apply calibration if manual calibration has been performed (center value is not 0)
    if (center != 0) {
        if (adc_value > center) {
            adc_value = map(adc_value, center, ADC_MAX, ADC_MAX / 2, ADC_MAX);
        } else if (adc_value == center) {
            adc_value = ADC_MAX / 2;
        } else {
            adc_value = map(adc_value, 0, center, 0, ADC_MAX / 2);
        }
    }
    return ((float)adc_value) / ADC_MAX;
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

uint16_t AnalogInput::map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float AnalogInput::magnitudeCalculation(int stick_num, adc_instance & adc_inst) {
    adc_inst.x_magnitude = adc_inst.x_value - ANALOG_CENTER;
    adc_inst.y_magnitude = adc_inst.y_value - ANALOG_CENTER;
    return adc_pairs[stick_num].error_rate * std::sqrt((adc_inst.x_magnitude * adc_inst.x_magnitude) + (adc_inst.y_magnitude * adc_inst.y_magnitude));
}

/**
 * Interpolate maximum radius for a given angle using range calibration data
 * @param stick_num Stick number (0 or 1)
 * @param angle Angle in radians (-PI to PI)
 * @return Maximum radius (0.0 to 1.0), or 0.0 if no calibration data
 */
float AnalogInput::getInterpolatedMaxRadius(int stick_num, float angle) {
    const float* range_data = adc_pairs[stick_num].range_data;
    const int CIRCULARITY_DATA_SIZE = 48;
    
    // Check if we have any calibration data
    bool hasData = false;
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (range_data[i] > 0.0f) {
            hasData = true;
            break;
        }
    }
    if (!hasData) {
        return 0.0f;  // No calibration data, use default behavior
    }
    
    // Convert angle from [-PI, PI] to [0, 2*PI] then to [0, CIRCULARITY_DATA_SIZE]
    float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);  // 0.0 to 1.0
    float index = normalizedAngle * CIRCULARITY_DATA_SIZE;
    
    // Get the two adjacent indices for interpolation
    int i0 = ((int)std::floor(index)) % CIRCULARITY_DATA_SIZE;
    int i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
    float t = index - std::floor(index);  // Fractional part (0.0 to 1.0)
    
    // Linear interpolation
    float r0 = range_data[i0] > 0.0f ? range_data[i0] : 0.0f;
    float r1 = range_data[i1] > 0.0f ? range_data[i1] : 0.0f;
    
    return r0 * (1.0f - t) + r1 * t;
}

void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // Calculate current distance from center
    float current_distance = std::sqrt((adc_inst.x_magnitude * adc_inst.x_magnitude) + (adc_inst.y_magnitude * adc_inst.y_magnitude));
    
    // Check if range calibration data is available and apply angle-based limit
    float max_radius = getInterpolatedMaxRadius(stick_num, std::atan2(adc_inst.y_magnitude, adc_inst.x_magnitude));
    if (max_radius > 0.0f && current_distance > max_radius) {
        // Scale down to fit within calibrated range
        float scale = max_radius / current_distance;
        adc_inst.x_magnitude *= scale;
        adc_inst.y_magnitude *= scale;
        current_distance = max_radius;
    }
    
    // Apply standard deadzone scaling
    float scaling_factor = (current_distance - adc_pairs[stick_num].in_deadzone) / (adc_pairs[stick_num].out_deadzone - adc_pairs[stick_num].in_deadzone);
    if (adc_pairs[stick_num].forced_circularity == true) {
        scaling_factor = std::fmin(scaling_factor, ANALOG_CENTER);
    }
    adc_inst.x_value = ((adc_inst.x_magnitude / (current_distance > 0.0f ? current_distance : 1.0f)) * scaling_factor) + ANALOG_CENTER;
    adc_inst.y_value = ((adc_inst.y_magnitude / (current_distance > 0.0f ? current_distance : 1.0f)) * scaling_factor) + ANALOG_CENTER;
    adc_inst.x_value = std::clamp(adc_inst.x_value, ANALOG_MINIMUM, ANALOG_MAX);
    adc_inst.y_value = std::clamp(adc_inst.y_value, ANALOG_MINIMUM, ANALOG_MAX);
    if (adc_pairs[stick_num].anti_deadzone > 0.0f) {
        float x_component = adc_inst.x_value - ANALOG_CENTER;
        float y_component = adc_inst.y_value - ANALOG_CENTER;
        float length = std::sqrt((x_component * x_component) + (y_component * y_component));
        if (length > 0.0f) {
            float normalized = std::min(length / ANALOG_CENTER, 1.0f);
            float baseline = std::clamp(adc_pairs[stick_num].anti_deadzone, 0.0f, 1.0f);
            if (normalized < baseline) {
                float targetLength = baseline * ANALOG_CENTER;
                float scale = targetLength / length;
                x_component *= scale;
                y_component *= scale;
                adc_inst.x_value = std::clamp(x_component + ANALOG_CENTER, ANALOG_MINIMUM, ANALOG_MAX);
                adc_inst.y_value = std::clamp(y_component + ANALOG_CENTER, ANALOG_MINIMUM, ANALOG_MAX);
            }
        }
    }
}
