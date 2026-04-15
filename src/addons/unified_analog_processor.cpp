#include "addons/unified_analog_processor.h"

#include "addons/analog.h"
#include "addons/mcp3208_adc.h"
#include "addons/ads8332_adc.h"
#include "config.pb.h"
#include "drivermanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "storagemanager.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ADC_MAX ((1 << 12) - 1)
#define ADC_MAX_HALF (ADC_MAX * 0.5f)
#define ANALOG_CENTER 0.5f
#define CIRCULARITY_DATA_SIZE 48

namespace {
static void convertCurvePoints(const CurvePoint* protobuf_points, int count, UnifiedAnalogCurvePoint* output) {
    for (int i = 0; i < count && i < 3; i++) {
        output[i].x = protobuf_points[i].x;
        output[i].y = protobuf_points[i].y;
    }
}
} // namespace

bool UnifiedAnalogProcessorAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    return addonOptions.analogOptions.enabled ||
        addonOptions.mcp3208Options.enabled ||
        addonOptions.ads8332Options.enabled;
}

void UnifiedAnalogProcessorAddon::setup() {
    resolveSource();
    initializeFromOptions();
}

void UnifiedAnalogProcessorAddon::reinit() {
    resolveSource();
    initializeFromOptions();
}

void UnifiedAnalogProcessorAddon::resolveSource() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    if (addonOptions.ads8332Options.enabled) {
        source_ = StickSource::ADS8332;
        return;
    }
    if (addonOptions.mcp3208Options.enabled) {
        source_ = StickSource::MCP3208;
        return;
    }
    if (addonOptions.analogOptions.enabled) {
        source_ = StickSource::OnboardADC;
        return;
    }
    source_ = StickSource::None;
}

void UnifiedAnalogProcessorAddon::initializeFromOptions() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    const bool curveEnabled = analogOptions.joystick_curve_enabled;

    usage_curve_profile_1_ = analogOptions.curve_profile_1;
    usage_curve_profile_2_ = analogOptions.curve_profile_2;

    temp_curve_storage_[0].is_saved = false;
    temp_curve_storage_[1].is_saved = false;
    temp_curve_storage_[0].saved_points_count = 0;
    temp_curve_storage_[1].saved_points_count = 0;
    active_activation_preset_[0] = 0;
    active_activation_preset_[1] = 0;

    initializeStickFromOptions(0, analogOptions, curveEnabled);
    initializeStickFromOptions(1, analogOptions, curveEnabled);

    applyFinetuneShapeAdjustments(0);
    applyFinetuneShapeAdjustments(1);
}

void UnifiedAnalogProcessorAddon::initializeStickFromOptions(int stickNum, const AnalogOptions& options, bool curveEnabled) {
    const bool isFirst = (stickNum == 0);
    StickState& stick = sticks_[stickNum];

    stick.x_value = ANALOG_CENTER;
    stick.y_value = ANALOG_CENTER;
    stick.analog_invert = isFirst ? options.analogAdc1Invert : options.analogAdc2Invert;
    stick.analog_dpad = isFirst ? options.analogAdc1Mode : options.analogAdc2Mode;
    stick.in_deadzone = (isFirst ? options.inner_deadzone : options.inner_deadzone2) / 100.0f;
    stick.anti_deadzone = std::clamp((isFirst ? options.anti_deadzone : options.anti_deadzone2) / 100.0f, 0.0f, 1.0f);
    stick.fixed_anti_deadzone = isFirst ? options.fixed_anti_deadzone : options.fixed_anti_deadzone2;
    stick.jitter_filter = isFirst ? options.joystick_jitter_filter_1 : options.joystick_jitter_filter_2;
    stick.x_center = static_cast<uint16_t>((isFirst ? options.joystick_center_x : options.joystick_center_x2) > 0
        ? (isFirst ? options.joystick_center_x : options.joystick_center_x2)
        : static_cast<uint32_t>(ADC_MAX_HALF));
    stick.y_center = static_cast<uint16_t>((isFirst ? options.joystick_center_y : options.joystick_center_y2) > 0
        ? (isFirst ? options.joystick_center_y : options.joystick_center_y2)
        : static_cast<uint32_t>(ADC_MAX_HALF));
    stick.has_range_calibration = isFirst ? (options.joystick_range_data_1_count > 0) : (options.joystick_range_data_2_count > 0);
    stick.finetune_shape_force_circular = isFirst ? options.joystick_finetune_shape_force_circular_1 : options.joystick_finetune_shape_force_circular_2;
    stick.finetune_shape_amplify = isFirst ? options.joystick_finetune_shape_amplify_1 : options.joystick_finetune_shape_amplify_2;
    stick.last_x_adc = stick.x_center;
    stick.last_y_adc = stick.y_center;
    stick.curve_points_sorted_count = 0;
    stick.curve_segments_count = 0;

    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        const bool hasPoint = isFirst ? (i < options.joystick_range_data_1_count) : (i < options.joystick_range_data_2_count);
        const float point = isFirst ? options.joystick_range_data_1[i] : options.joystick_range_data_2[i];
        stick.range_data[i] = (hasPoint && point > 0.0f) ? point : 0.0f;
    }

    if (curveEnabled) {
        const int curveCount = isFirst ? options.joystick_curve_points_1_count : options.joystick_curve_points_2_count;
        if (curveCount > 0) {
            UnifiedAnalogCurvePoint converted[3];
            if (isFirst) {
                convertCurvePoints(options.joystick_curve_points_1, curveCount, converted);
            } else {
                convertCurvePoints(options.joystick_curve_points_2, curveCount, converted);
            }
            initializeCurveSegments(stickNum, converted, curveCount);
        }
    }
}

uint16_t UnifiedAnalogProcessorAddon::quantizeRaw(int stickNum, uint16_t value, bool isXAxis, uint16_t adcMax) {
    uint32_t step = sticks_[stickNum].jitter_filter;
    uint16_t* last = isXAxis ? &sticks_[stickNum].last_x_adc : &sticks_[stickNum].last_y_adc;
    if (step > 0) {
        const uint32_t maxStep = static_cast<uint32_t>(adcMax) + 1u;
        if (step > maxStep) {
            step = maxStep;
        }
        const uint32_t half = step / 2u;
        const uint32_t rounded = (static_cast<uint32_t>(value) + half) / step;
        uint32_t quantized = rounded * step;
        if (quantized > adcMax) {
            quantized = adcMax;
        }
        value = static_cast<uint16_t>(quantized);
    }
    *last = value;
    return value;
}

void UnifiedAnalogProcessorAddon::process() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;
    if (!available()) {
        return;
    }

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    if (analogOptions.joystick_curve_enabled) {
        int stickNum = 1;
        bool foundPressed = false;
        int pressedPresetIdx = -1;
        for (int presetIdx = 0; presetIdx < 4 && presetIdx < analogOptions.joystick_curve_presets_count; presetIdx++) {
            const CurvePreset& preset = analogOptions.joystick_curve_presets[presetIdx];
            if (preset.activationButtonMask == 0) {
                continue;
            }
            bool isPressed = false;
            const uint32_t buttonMask = preset.activationButtonMask;
            const uint32_t dpadMask = buttonMask & (GAMEPAD_MASK_DU | GAMEPAD_MASK_DD | GAMEPAD_MASK_DL | GAMEPAD_MASK_DR);
            const uint32_t regularMask = buttonMask & ~(GAMEPAD_MASK_DU | GAMEPAD_MASK_DD | GAMEPAD_MASK_DL | GAMEPAD_MASK_DR);

            if (regularMask != 0 && (gamepad->state.buttons & regularMask) == regularMask) isPressed = true;
            if (dpadMask != 0) {
                if ((dpadMask & GAMEPAD_MASK_DU) && (gamepad->state.dpad & GAMEPAD_MASK_UP)) isPressed = true;
                if ((dpadMask & GAMEPAD_MASK_DD) && (gamepad->state.dpad & GAMEPAD_MASK_DOWN)) isPressed = true;
                if ((dpadMask & GAMEPAD_MASK_DL) && (gamepad->state.dpad & GAMEPAD_MASK_LEFT)) isPressed = true;
                if ((dpadMask & GAMEPAD_MASK_DR) && (gamepad->state.dpad & GAMEPAD_MASK_RIGHT)) isPressed = true;
            }

            if (isPressed) {
                foundPressed = true;
                pressedPresetIdx = presetIdx;
                break;
            }
        }

        if (foundPressed) {
            if (active_activation_preset_[stickNum] == 0) {
                saveCurrentCurveData(stickNum);
                if (pressedPresetIdx < analogOptions.joystick_curve_presets_count &&
                    analogOptions.joystick_curve_presets[pressedPresetIdx].points_count > 0) {
                    applyPresetCurve(stickNum, pressedPresetIdx);
                    active_activation_preset_[stickNum] = static_cast<uint8_t>(pressedPresetIdx + 1);
                }
            }
        } else if (active_activation_preset_[stickNum] != 0) {
            restoreCurveData(stickNum);
        }

        const uint32_t currentProfile1 = analogOptions.curve_profile_1;
        const uint32_t currentProfile2 = analogOptions.curve_profile_2;
        if (usage_curve_profile_1_ != currentProfile1 || usage_curve_profile_2_ != currentProfile2) {
            usage_curve_profile_1_ = currentProfile1;
            usage_curve_profile_2_ = currentProfile2;
            reinit();
        }
    }

    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    if (DriverManager::getInstance().getDriver() != nullptr) {
        joystickMax = DriverManager::getInstance().getDriver()->GetJoystickMidValue() * 2;
    }

    for (int i = 0; i < STICK_COUNT; i++) {
        uint16_t rawX = 0;
        uint16_t rawY = 0;
        uint16_t xCenter = static_cast<uint16_t>(ADC_MAX_HALF);
        uint16_t yCenter = static_cast<uint16_t>(ADC_MAX_HALF);
        uint16_t adcMax = ADC_MAX;
        bool xValid = false;
        bool yValid = false;

        bool hasSource = false;
        if (source_ == StickSource::ADS8332) {
            hasSource = ADS8332ADCAddon::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax);
        } else if (source_ == StickSource::MCP3208) {
            hasSource = MCP3208ADCAddon::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax);
        } else if (source_ == StickSource::OnboardADC) {
            hasSource = AnalogInput::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid);
            adcMax = ADC_MAX;
        }
        if (!hasSource) {
            continue;
        }

        rawX = quantizeRaw(i, rawX, true, adcMax);
        rawY = quantizeRaw(i, rawY, false, adcMax);
        // Center calibration remains owned by unified analog options.
        const uint16_t calibratedXCenter = sticks_[i].x_center;
        const uint16_t calibratedYCenter = sticks_[i].y_center;
        const float adcHalf = static_cast<float>(adcMax) * 0.5f;

        float cx = xValid ? static_cast<float>(rawX) - static_cast<float>(calibratedXCenter) : 0.0f;
        float cy = yValid ? static_cast<float>(rawY) - static_cast<float>(calibratedYCenter) : 0.0f;

        float scale = getInterpolatedScale(i, std::atan2(cy, cx));
        float sx = cx / scale;
        float sy = cy / scale;

        float nx = sx / adcHalf;
        float ny = sy / adcHalf;

        if (sticks_[i].analog_invert == InvertMode::INVERT_X || sticks_[i].analog_invert == InvertMode::INVERT_XY) nx = -nx;
        if (sticks_[i].analog_invert == InvertMode::INVERT_Y || sticks_[i].analog_invert == InvertMode::INVERT_XY) ny = -ny;

        float distSq = nx * nx + ny * ny;
        float deadzoneSq = sticks_[i].in_deadzone * sticks_[i].in_deadzone;
        if (distSq < deadzoneSq) {
            nx = 0.0f;
            ny = 0.0f;
        } else if (sticks_[i].anti_deadzone > 0.0f) {
            float dist = std::sqrt(distSq);
            float baseline = sticks_[i].anti_deadzone;
            if (sticks_[i].fixed_anti_deadzone) {
                if (dist > 0.0f && dist < baseline) {
                    float scaleFactor = baseline / dist;
                    nx *= scaleFactor;
                    ny *= scaleFactor;
                }
            } else if (dist > 0.0f) {
                float newDist = dist + baseline;
                float scaleFactor = newDist / dist;
                nx *= scaleFactor;
                ny *= scaleFactor;
            }
        }

        nx = std::clamp(nx, -1.0f, 1.0f);
        ny = std::clamp(ny, -1.0f, 1.0f);

        if (sticks_[i].curve_points_sorted_count > 0 || active_activation_preset_[i] != 0) {
            applyResponseCurveToCoordinates(nx, ny, i);
        }

        float xValue = nx * 0.5f + ANALOG_CENTER;
        float yValue = ny * 0.5f + ANALOG_CENTER;
        sticks_[i].x_value = xValue;
        sticks_[i].y_value = yValue;

        float clampedXf = std::clamp(xValue, 0.0f, 1.0f);
        float clampedYf = std::clamp(yValue, 0.0f, 1.0f);
        uint16_t clampedX = static_cast<uint16_t>(std::min(static_cast<uint32_t>(joystickMax * clampedXf), static_cast<uint32_t>(0xFFFF)));
        uint16_t clampedY = static_cast<uint16_t>(std::min(static_cast<uint32_t>(joystickMax * clampedYf), static_cast<uint32_t>(0xFFFF)));

        if (sticks_[i].analog_dpad == DpadMode::DPAD_MODE_LEFT_ANALOG) {
            gamepad->state.lx = clampedX;
            gamepad->state.ly = clampedY;
        } else if (sticks_[i].analog_dpad == DpadMode::DPAD_MODE_RIGHT_ANALOG) {
            gamepad->state.rx = clampedX;
            gamepad->state.ry = clampedY;
        }
    }
}

float UnifiedAnalogProcessorAddon::getInterpolatedScale(int stickNum, float angle) const {
    if (!sticks_[stickNum].has_range_calibration) {
        return 0.65f;
    }

    float normalizedAngle = (angle + static_cast<float>(M_PI)) / (2.0f * static_cast<float>(M_PI));
    float index = normalizedAngle * CIRCULARITY_DATA_SIZE;
    int i0 = static_cast<int>(index);
    if (i0 >= CIRCULARITY_DATA_SIZE) {
        i0 = CIRCULARITY_DATA_SIZE - 1;
    }
    int i1 = (i0 + 1 < CIRCULARITY_DATA_SIZE) ? (i0 + 1) : 0;
    float t = index - static_cast<float>(i0);
    float r0 = sticks_[stickNum].range_data[i0];
    float r1 = sticks_[stickNum].range_data[i1];
    return r0 * (1.0f - t) + r1 * t;
}

void UnifiedAnalogProcessorAddon::applyFinetuneShapeAdjustments(int stickNum) {
    if (!sticks_[stickNum].has_range_calibration) {
        return;
    }

    if (!sticks_[stickNum].finetune_shape_force_circular) {
        float minScale = sticks_[stickNum].range_data[0];
        for (int i = 1; i < CIRCULARITY_DATA_SIZE; i++) {
            if (sticks_[stickNum].range_data[i] < minScale) {
                minScale = sticks_[stickNum].range_data[i];
            }
        }
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            sticks_[stickNum].range_data[i] = minScale;
        }
    }

    const float amplifyFactor = 1.0f + sticks_[stickNum].finetune_shape_amplify / 100.0f;
    if (amplifyFactor > 0.0f) {
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            if (sticks_[stickNum].range_data[i] > 0.0f) {
                sticks_[stickNum].range_data[i] /= amplifyFactor;
            }
        }
    }
}

void UnifiedAnalogProcessorAddon::initializeCurveSegments(int stickNum, const UnifiedAnalogCurvePoint* controlPoints, int controlPointsCount) {
    sticks_[stickNum].curve_points_sorted_count = 0;
    sticks_[stickNum].curve_points_sorted[sticks_[stickNum].curve_points_sorted_count++] = {
        sticks_[stickNum].in_deadzone,
        sticks_[stickNum].anti_deadzone,
    };
    for (int i = 0; i < controlPointsCount; i++) {
        sticks_[stickNum].curve_points_sorted[sticks_[stickNum].curve_points_sorted_count++] = {
            controlPoints[i].x,
            controlPoints[i].y,
        };
    }
    sticks_[stickNum].curve_points_sorted[sticks_[stickNum].curve_points_sorted_count++] = {1.0f, 1.0f};

    sticks_[stickNum].curve_segments_count = 0;
    for (int i = 0; i < sticks_[stickNum].curve_points_sorted_count - 1; i++) {
        const float p1x = sticks_[stickNum].curve_points_sorted[i].x;
        const float p1y = sticks_[stickNum].curve_points_sorted[i].y;
        const float p2x = sticks_[stickNum].curve_points_sorted[i + 1].x;
        const float p2y = sticks_[stickNum].curve_points_sorted[i + 1].y;

        if (p2x == p1x) {
            sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].slope = 0.0f;
            sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].intercept = p1y;
        } else {
            float slope = (p2y - p1y) / (p2x - p1x);
            sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].slope = slope;
            sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].intercept = p1y - p1x * slope;
        }
        sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].x_start = p1x;
        sticks_[stickNum].curve_segments[sticks_[stickNum].curve_segments_count].x_end = p2x;
        sticks_[stickNum].curve_segments_count++;
    }
}

void UnifiedAnalogProcessorAddon::applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stickNum) {
    if (normalizedX == 0.0f && normalizedY == 0.0f) {
        return;
    }

    float clampdistSq = normalizedX * normalizedX + normalizedY * normalizedY;
    float clampdist = std::sqrt(clampdistSq);

    int segmentIdx = -1;
    if (clampdist > 1.0f) {
        segmentIdx = sticks_[stickNum].curve_segments_count;
    } else {
        for (int i = 0; i < sticks_[stickNum].curve_segments_count; i++) {
            if (clampdist >= sticks_[stickNum].curve_segments[i].x_start &&
                clampdist < sticks_[stickNum].curve_segments[i].x_end) {
                segmentIdx = i;
                break;
            }
        }
        if (segmentIdx == -1) segmentIdx = sticks_[stickNum].curve_segments_count - 1;
    }

    if (clampdist > 1.0f) {
        return;
    }

    float curvedMagnitude;
    int validSegIdx = std::min(segmentIdx, static_cast<int>(sticks_[stickNum].curve_segments_count) - 1);
    if (validSegIdx >= 0) {
        curvedMagnitude = sticks_[stickNum].curve_segments[validSegIdx].intercept +
            clampdist * sticks_[stickNum].curve_segments[validSegIdx].slope;
    } else {
        curvedMagnitude = clampdist;
    }

    float scale = curvedMagnitude / clampdist;
    normalizedX *= scale;
    normalizedY *= scale;
}

void UnifiedAnalogProcessorAddon::saveCurrentCurveData(int stickNum) {
    if (stickNum < 0 || stickNum >= STICK_COUNT) return;

    temp_curve_storage_[stickNum].saved_points_count = 0;
    if (sticks_[stickNum].curve_points_sorted_count > 2) {
        for (int i = 1; i < sticks_[stickNum].curve_points_sorted_count - 1; i++) {
            if (temp_curve_storage_[stickNum].saved_points_count < 3) {
                temp_curve_storage_[stickNum].saved_points[temp_curve_storage_[stickNum].saved_points_count] = {
                    sticks_[stickNum].curve_points_sorted[i].x,
                    sticks_[stickNum].curve_points_sorted[i].y,
                };
                temp_curve_storage_[stickNum].saved_points_count++;
            }
        }
    }
    temp_curve_storage_[stickNum].is_saved = true;
}

void UnifiedAnalogProcessorAddon::restoreCurveData(int stickNum) {
    if (stickNum < 0 || stickNum >= STICK_COUNT || !temp_curve_storage_[stickNum].is_saved) return;

    if (temp_curve_storage_[stickNum].saved_points_count > 0) {
        initializeCurveSegments(stickNum, temp_curve_storage_[stickNum].saved_points, temp_curve_storage_[stickNum].saved_points_count);
    } else {
        sticks_[stickNum].curve_points_sorted_count = 0;
        sticks_[stickNum].curve_segments_count = 0;
    }

    temp_curve_storage_[stickNum].is_saved = false;
    active_activation_preset_[stickNum] = 0;
}

void UnifiedAnalogProcessorAddon::applyPresetCurve(int stickNum, int presetIndex) {
    if (stickNum < 0 || stickNum >= STICK_COUNT || presetIndex < 0 || presetIndex >= 4) return;

    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    if (presetIndex >= analogOptions.joystick_curve_presets_count ||
        analogOptions.joystick_curve_presets[presetIndex].points_count == 0) {
        return;
    }

    const CurvePreset& preset = analogOptions.joystick_curve_presets[presetIndex];

    UnifiedAnalogCurvePoint converted[3];
    for (int i = 0; i < preset.points_count && i < 3; i++) {
        converted[i].x = preset.points[i].x;
        converted[i].y = preset.points[i].y;
    }
    initializeCurveSegments(stickNum, converted, preset.points_count);
}
