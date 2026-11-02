#include "addons/unified_analog_processor.h"

#include "addons/analog.h"
#include "addons/ads8332_adc.h"
#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "drivermanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "storagemanager.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ADC_MAX ((1 << 12) - 1)
#define ADC_MAX_HALF (ADC_MAX * 0.5f)
#define ANALOG_CENTER 0.5f
#define CIRCULARITY_DATA_SIZE 48

// Linear inner-deadzone mode: physical travel within the last kSnapMargin
// (2%) of a direction's range is compensated back by (dz + kSnapMargin),
// restoring the original outer gate shape; the curve endpoint stays
// reachable and the per-axis output clamp caps the overshoot.
static constexpr float kSnapMargin = 0.02f;

// Slope-space sector classification (defined below, avoids atan2).
static void classifySectorSlope(float nx, float ny, int& sectorOut, float& tOut);

namespace {
static void convertCurvePoints(const CurvePoint* protobuf_points, int count, UnifiedAnalogCurvePoint* output) {
    for (int i = 0; i < count && i < 6; i++) {
        output[i].x = protobuf_points[i].x;
        output[i].y = protobuf_points[i].y;
    }
}

// Bezier curve support (joystick_curve_mode == 1). The helpers below mirror the
// frontend JoystickCurveSettings.tsx implementations line by line (frontend uses
// double, firmware uses float — the precision difference is negligible).

// Linear default control points: the identity cubic y = x.
static constexpr float kBezierLinearC1 = 1.0f / 3.0f;
static constexpr float kBezierLinearC2 = 2.0f / 3.0f;

// Clamp one component to [0,1]; non-finite falls back to the linear-default
// component (matches frontend clampBezierControl).
static float clampBezierComponent(float v, float fallback) {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, 0.0f, 1.0f);
}

// True iff the quadratic bezier with control values (a, b, c) stays >= 0 on [0,1].
static bool quadBezierNonNegative(float a, float b, float c) {
    if (a < 0.0f || c < 0.0f) return false;
    const float d = a - 2.0f * b + c;
    if (d <= 0.0f) return true;              // concave: minimum at endpoint
    if (a <= b || c <= b) return true;       // vertex outside (0,1)
    return a * c >= b * b - 1e-9f;
}

static bool isBezierMonotone(float c1x, float c2x, float c1y, float c2y) {
    return quadBezierNonNegative(c1x, c2x - c1x, 1.0f - c2x) &&
           quadBezierNonNegative(c1y, c2y - c1y, 1.0f - c2y);
}

// Normalize raw control points exactly like frontend normalizeBezier: missing
// components, all-zero data, or a non-monotone curve all fall back to linear.
static void resolveBezierControl(const BezierControl& src, float& c1x, float& c1y, float& c2x, float& c2y) {
    if (!src.has_c1x || !src.has_c1y || !src.has_c2x || !src.has_c2y) {
        c1x = kBezierLinearC1;
        c1y = kBezierLinearC1;
        c2x = kBezierLinearC2;
        c2y = kBezierLinearC2;
        return;
    }
    c1x = clampBezierComponent(src.c1x, kBezierLinearC1);
    c1y = clampBezierComponent(src.c1y, kBezierLinearC1);
    c2x = clampBezierComponent(src.c2x, kBezierLinearC2);
    c2y = clampBezierComponent(src.c2y, kBezierLinearC2);
    // All-zero control points are degenerate (legacy flash data written before
    // the has_ flags existed) — treat as unset.
    if (c1x == 0.0f && c1y == 0.0f && c2x == 0.0f && c2y == 0.0f) {
        c1x = kBezierLinearC1;
        c1y = kBezierLinearC1;
        c2x = kBezierLinearC2;
        c2y = kBezierLinearC2;
        return;
    }
    if (!isBezierMonotone(c1x, c2x, c1y, c2y)) {
        c1x = kBezierLinearC1;
        c1y = kBezierLinearC1;
        c2x = kBezierLinearC2;
        c2y = kBezierLinearC2;
        return;
    }
}

// Cubic bezier axis value with endpoints 0 and 1.
static float bezierEvalAxis(float c1, float c2, float t) {
    const float mt = 1.0f - t;
    return 3.0f * mt * mt * t * c1 + 3.0f * mt * t * t * c2 + t * t * t;
}

// Bisection solve x(t) = xTarget. x(t) is monotonic (enforced at init), so exactly
// one root exists and bisection always converges — unlike Newton, which stalls when
// x'(t) approaches 0 (possible now that C1 <= C2 is no longer enforced).
static float bezierFindT(float xTarget, float c1x, float c2x) {
    float lo = 0.0f, hi = 1.0f, t = 0.5f;
    for (int i = 0; i < 24; i++) {
        t = (lo + hi) * 0.5f;
        const float err = bezierEvalAxis(c1x, c2x, t) - xTarget;
        if (std::fabs(err) < 1e-6f) break;
        if (err > 0.0f) hi = t; else lo = t;
    }
    return t;
}
} // namespace

bool UnifiedAnalogProcessorAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    return addonOptions.analogOptions.enabled ||
        addonOptions.ads8332Options.enabled ||
        addonOptions.mcp3208Options.enabled;
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
    // Source priority only selects provider; coordinate semantics are unified:
    // stick0 => ANALOG_ADC_1_VRX/VRY, stick1 => ANALOG_ADC_2_VRX/VRY.
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
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;
    const bool curveEnabled = analogOptions.joystick_curve_enabled;

    usage_curve_profile_1_ = analogOptions.curve_profile_1;
    usage_curve_profile_2_ = analogOptions.curve_profile_2;

    temp_curve_storage_[0].is_saved = false;
    temp_curve_storage_[1].is_saved = false;
    temp_curve_storage_[0].saved_points_count = 0;
    temp_curve_storage_[1].saved_points_count = 0;
    temp_bezier_storage_[0].is_saved = false;
    temp_bezier_storage_[1].is_saved = false;
    active_activation_preset_[0] = 0;
    active_activation_preset_[1] = 0;

    initializeStickFromOptions(0, analogOptions, curveEnabled);
    initializeStickFromOptions(1, analogOptions, curveEnabled);

    // Scale is direction-independent; compute once at init instead of per-frame
    updateUniformScale(0);
    updateUniformScale(1);
}

void UnifiedAnalogProcessorAddon::initializeStickFromOptions(int stickNum, const AnalogOptions& options, bool curveEnabled) {
    const bool isFirst = (stickNum == 0);
    StickState& stick = sticks_[stickNum];

    stick.x_value = ANALOG_CENTER;
    stick.y_value = ANALOG_CENTER;
    stick.analog_invert = isFirst ? options.analogAdc1Invert : options.analogAdc2Invert;
    stick.analog_dpad = isFirst ? options.analogAdc1Mode : options.analogAdc2Mode;
    stick.in_deadzone = analogDeadzoneNormFromRaw(isFirst ? options.inner_deadzone : options.inner_deadzone2);
    stick.anti_deadzone = std::clamp(
        analogDeadzoneNormFromRaw(isFirst ? options.anti_deadzone : options.anti_deadzone2),
        0.0f,
        1.0f);
    stick.fixed_anti_deadzone = isFirst ? options.fixed_anti_deadzone : options.fixed_anti_deadzone2;
    stick.jitter_filter = isFirst ? options.joystick_jitter_filter_1 : options.joystick_jitter_filter_2;
    stick.x_center = static_cast<uint16_t>((isFirst ? options.joystick_center_x : options.joystick_center_x2) > 0
        ? (isFirst ? options.joystick_center_x : options.joystick_center_x2)
        : 0u);
    stick.y_center = static_cast<uint16_t>((isFirst ? options.joystick_center_y : options.joystick_center_y2) > 0
        ? (isFirst ? options.joystick_center_y : options.joystick_center_y2)
        : 0u);
    stick.finetune_shape_amplify = isFirst ? options.joystick_finetune_shape_amplify_1 : options.joystick_finetune_shape_amplify_2;
    stick.last_x_adc = stick.x_center;
    stick.last_y_adc = stick.y_center;
    stick.curve_points_sorted_count = 0;
    stick.curve_segments_count = 0;

    // Calibrated means at least one valid (positive) range point; a stored
    // count with only non-positive samples is treated as no calibration.
    stick.has_range_calibration = false;
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        const bool hasPoint = isFirst ? (i < options.joystick_range_data_1_count) : (i < options.joystick_range_data_2_count);
        const float point = isFirst ? options.joystick_range_data_1[i] : options.joystick_range_data_2[i];
        stick.range_data[i] = (hasPoint && point > 0.0f) ? point : 0.0f;
        if (stick.range_data[i] > 0.0f) {
            stick.has_range_calibration = true;
        }
    }

    if (curveEnabled) {
        const int curveCount = isFirst ? options.joystick_curve_points_1_count : options.joystick_curve_points_2_count;
        if (curveCount > 0) {
            UnifiedAnalogCurvePoint converted[6];
            if (isFirst) {
                convertCurvePoints(options.joystick_curve_points_1, curveCount, converted);
            } else {
                convertCurvePoints(options.joystick_curve_points_2, curveCount, converted);
            }
            initializeCurveSegments(stickNum, converted, curveCount);
        }
    }

    // Bezier mode: the same "enable joystick curve" toggle gates both modes; with
    // the toggle on and mode == 1 the bezier path always activates, falling back
    // to the linear default on missing/invalid data (initializeBezierCurve never
    // fails, so a fallback still counts as active).
    stick.bezier_active = false;
    if (curveEnabled && options.joystick_curve_mode == 1) {
        const bool hasBezier = isFirst ? options.has_joystick_bezier_1 : options.has_joystick_bezier_2;
        if (hasBezier) {
            initializeBezierCurve(stickNum, isFirst ? options.joystick_bezier_1 : options.joystick_bezier_2);
        } else {
            // Never configured: all component has_ flags false -> linear default.
            const BezierControl emptyBezier = {};
            initializeBezierCurve(stickNum, emptyBezier);
        }
    }
}

uint16_t __not_in_flash_func(UnifiedAnalogProcessorAddon::quantizeRaw)(int stickNum, uint16_t value, bool isXAxis, uint16_t adcMax) {
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

void __not_in_flash_func(UnifiedAnalogProcessorAddon::applyInnerDeadzone)(int stickNum, float& nx, float& ny) {
    const StickState& stick = sticks_[stickNum];
    const float distSq = nx * nx + ny * ny;
    const float deadzoneSq = stick.in_deadzone * stick.in_deadzone;
    if (distSq < deadzoneSq) {
        // Both modes mask inputs inside the inner deadzone to zero.
        nx = 0.0f;
        ny = 0.0f;
        return;
    }
    // Fixed mode (fixed_anti_deadzone == true): pure mask, no processing
    // outside the deadzone; output equals the raw stick input.
    if (stick.fixed_anti_deadzone) {
        return;
    }

    // Linear mode (fixed_anti_deadzone == false): pure radial translation
    // by -in_deadzone (no rescale, curve proportion is preserved).
    // Snap uses tiered filtering on the per-sector snap_need table:
    // cheap global gate, slope-space direction classification (no
    // atan2), neighbor need check, neighbor travel gate, and only
    // then interpolation for the precise trigger.
    const float r = std::sqrt(distSq);
    // r >= in_deadzone > 0 in this branch; the division is safe.
    float target = r - stick.in_deadzone;
    if (r >= stick.min_snap_trigger) {
        int sector;
        float t;
        // snap_need is indexed in the raw ADC frame (range calibration is
        // captured pre-inversion); map the direction back to that frame.
        const bool invX = (stick.analog_invert == InvertMode::INVERT_X) ||
                          (stick.analog_invert == InvertMode::INVERT_XY);
        const bool invY = (stick.analog_invert == InvertMode::INVERT_Y) ||
                          (stick.analog_invert == InvertMode::INVERT_XY);
        classifySectorSlope(invX ? -nx : nx, invY ? -ny : ny, sector, t);
        const int next = (sector + 1 < CIRCULARITY_DATA_SIZE) ? (sector + 1) : 0;
        const float v0 = stick.snap_need[sector];
        const float v1 = stick.snap_need[next];
        if (v0 < 1.0f || v1 < 1.0f) {
            const float inf = std::numeric_limits<float>::infinity();
            const float t0 = (v0 < 1.0f) ? (v0 + stick.in_deadzone - kSnapMargin) : inf;
            const float t1 = (v1 < 1.0f) ? (v1 + stick.in_deadzone - kSnapMargin) : inf;
            if (r >= std::fmin(t0, t1)) {
                // Avoid inf arithmetic: a disabled neighbor just
                // yields the enabled one's trigger.
                const float trigger = (t0 == inf) ? t1
                                    : (t1 == inf) ? t0
                                    : (t0 + (t1 - t0) * t);
                if (r >= trigger) {
                    // Compensate back (dz + margin) instead of snapping to
                    // the unit circle: the trigger fires margin early at
                    // r1 - margin, where the translated output lags the
                    // no-deadzone travel by exactly dz + margin. Adding it
                    // back (target = (r - dz) + (dz + margin) = r + margin)
                    // restores the original outer gate shape and makes the
                    // output at the trigger equal the no-deadzone full-travel
                    // value r1 (>= 1), so full scale stays reachable and the
                    // per-axis clamp at the end caps the small overshoot.
                    target = r + kSnapMargin;
                }
            }
        }
    }
    const float s = target / r;
    nx *= s;
    ny *= s;
}

void __not_in_flash_func(UnifiedAnalogProcessorAddon::process)() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    if (analogOptions.joystick_curve_enabled) {
        int stickNum = 1;
        // In bezier mode the same hot-swap mechanism polls the shared bezier
        // preset list instead of the polyline one (no new options either way).
        // bezier_active == curveEnabled && mode == 1 (init always resolves to a
        // valid curve, falling back to linear).
        const bool useBezier = sticks_[stickNum].bezier_active;
        const int presetCount = useBezier ? analogOptions.joystick_bezier_presets_count
                                          : analogOptions.joystick_curve_presets_count;
        bool foundPressed = false;
        int pressedPresetIdx = -1;
        for (int presetIdx = 0; presetIdx < presetCount; presetIdx++) {
            const uint32_t buttonMask = useBezier
                ? analogOptions.joystick_bezier_presets[presetIdx].activationButtonMask
                : analogOptions.joystick_curve_presets[presetIdx].activationButtonMask;
            if (buttonMask == 0) {
                continue;
            }
            bool isPressed = false;
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
                if (useBezier) {
                    saveCurrentBezierData(stickNum);
                    if (pressedPresetIdx < analogOptions.joystick_bezier_presets_count &&
                        analogOptions.joystick_bezier_presets[pressedPresetIdx].has_bezier) {
                        applyBezierPresetCurve(stickNum, pressedPresetIdx);
                        active_activation_preset_[stickNum] = static_cast<uint8_t>(pressedPresetIdx + 1);
                    }
                } else {
                    saveCurrentCurveData(stickNum);
                    if (pressedPresetIdx < analogOptions.joystick_curve_presets_count &&
                        analogOptions.joystick_curve_presets[pressedPresetIdx].points_count > 0) {
                        applyPresetCurve(stickNum, pressedPresetIdx);
                        active_activation_preset_[stickNum] = static_cast<uint8_t>(pressedPresetIdx + 1);
                    }
                }
            }
        } else if (active_activation_preset_[stickNum] != 0) {
            if (useBezier) {
                restoreBezierData(stickNum);
            } else {
                restoreCurveData(stickNum);
            }
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
        // The sampler contract guarantees unified stick semantics across sources.
        if (source_ == StickSource::ADS8332) {
            hasSource = ADS8332ADCAddon::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax);
        } else if (source_ == StickSource::MCP3208) {
            hasSource = MCP3208ADCAddon::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax);
        } else if (source_ == StickSource::OnboardADC) {
            hasSource = AnalogInput::getRawStickForProcessor(i, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax);
        }
        if (!hasSource) {
            continue;
        }

        rawX = quantizeRaw(i, rawX, true, adcMax);
        rawY = quantizeRaw(i, rawY, false, adcMax);
        // If center is uncalibrated (0), use source-native center (12-bit/16-bit aware).
        const uint16_t calibratedXCenter = (sticks_[i].x_center > 0u) ? sticks_[i].x_center : xCenter;
        const uint16_t calibratedYCenter = (sticks_[i].y_center > 0u) ? sticks_[i].y_center : yCenter;
        const float adcHalf = static_cast<float>(adcMax) * 0.5f;

        float cx = xValid ? static_cast<float>(rawX) - static_cast<float>(calibratedXCenter) : 0.0f;
        float cy = yValid ? static_cast<float>(rawY) - static_cast<float>(calibratedYCenter) : 0.0f;

        const float scale = sticks_[i].uniform_scale; // Precomputed at init (direction-independent)
        float sx = cx / scale;
        float sy = cy / scale;

        float nx = sx / adcHalf;
        float ny = sy / adcHalf;

        if (sticks_[i].analog_invert == InvertMode::INVERT_X || sticks_[i].analog_invert == InvertMode::INVERT_XY) nx = -nx;
        if (sticks_[i].analog_invert == InvertMode::INVERT_Y || sticks_[i].analog_invert == InvertMode::INVERT_XY) ny = -ny;

        // Inner deadzone stage admission: only meaningful in calibrated
        // normalized space with a non-zero deadzone (dz == 0 = feature off).
        // Both modes mask radially; linear mode additionally translates by
        // -dz with per-direction full-travel snap.
        if (sticks_[i].has_range_calibration && sticks_[i].in_deadzone > 0.0f) {
            applyInnerDeadzone(i, nx, ny);
        }

        // Capture pre-curve magnitude and direction for the post-curve
        // anti-deadzone offset (dist == 0 means the input was masked).
        float dist = std::sqrt(nx * nx + ny * ny);
        float ux = 0.0f;
        float uy = 0.0f;
        if (dist > 0.0f) {
            ux = nx / dist;
            uy = ny / dist;
        }

        if (sticks_[i].bezier_active) {
            applyBezierResponseCurve(nx, ny, i);
        } else if (sticks_[i].curve_points_sorted_count > 0 || active_activation_preset_[i] != 0) {
            applyResponseCurveToCoordinates(nx, ny, i);
        }

        // Anti-deadzone: fixed amount added after the curve along the original
        // direction (vertical translation of the output; az is never scaled and
        // masked/centered sticks are not offset).
        if (sticks_[i].anti_deadzone > 0.0f && dist > 0.0f) {
            nx += ux * sticks_[i].anti_deadzone;
            ny += uy * sticks_[i].anti_deadzone;
        }

        nx = std::clamp(nx, -1.0f, 1.0f);
        ny = std::clamp(ny, -1.0f, 1.0f);

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

void UnifiedAnalogProcessorAddon::updateUniformScale(int stickNum) {
    StickState& stick = sticks_[stickNum];
    if (!stick.has_range_calibration) {
        stick.uniform_scale = 0.6f;
        // Inner deadzone processing is skipped entirely without calibration;
        // keep snap disabled so no stale table is ever consulted.
        stick.min_snap_trigger = std::numeric_limits<float>::infinity();
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            stick.snap_need[i] = std::numeric_limits<float>::infinity();
        }
        return;
    }

    // Uniform scale: minimum of the calibration points, ignoring empty slots
    // (a non-positive sample would otherwise zero the scale and divide by 0).
    float minScale = std::numeric_limits<float>::infinity();
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (stick.range_data[i] > 0.0f && stick.range_data[i] < minScale) {
            minScale = stick.range_data[i];
        }
    }

    // Apply finetune amplify factor (matches legacy range_data /= amplifyFactor behavior)
    const float amplifyFactor = 1.0f + stick.finetune_shape_amplify / 100.0f;
    if (amplifyFactor > 0.0f) {
        minScale /= amplifyFactor;
    }

    stick.uniform_scale = minScale;

    // Deadzone feature is off when in_deadzone is zero: process() early-exits
    // before any deadzone/snap work, so keep the snap table fully disabled.
    if (stick.in_deadzone <= 0.0f) {
        stick.min_snap_trigger = std::numeric_limits<float>::infinity();
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            stick.snap_need[i] = std::numeric_limits<float>::infinity();
        }
        return;
    }

    // Per-sector snap need: r1 (this direction's physical max travel in
    // normalized units) minus in_deadzone = max reachable output. A sector
    // needs snap only when this value is below 1.
    float minTrigger = std::numeric_limits<float>::infinity();
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        // Empty slots have no physical travel: never snap in their direction.
        if (stick.range_data[i] <= 0.0f) {
            stick.snap_need[i] = std::numeric_limits<float>::infinity();
            continue;
        }
        const float r1 = stick.range_data[i] / stick.uniform_scale;
        const float need = r1 - stick.in_deadzone;
        stick.snap_need[i] = need;
        if (need < 1.0f) {
            const float trigger = need + stick.in_deadzone - kSnapMargin;
            if (trigger < minTrigger) {
                minTrigger = trigger;
            }
        }
    }
    stick.min_snap_trigger = minTrigger;
}

// Slope-space sector classification: locates the calibration sector of a
// direction vector without atan2 (one division + a few comparisons).
// Sectors match the legacy convention floor(((angle+PI)/(2*PI)) * 48).
// Outputs sector index [0,48) and interpolation parameter t in [0,1].
static void classifySectorSlope(float nx, float ny, int& sectorOut, float& tOut) {
    const float ax = std::fabs(nx);
    const float ay = std::fabs(ny);
    // Octant layout: [0,45):24, [45,90):30, [90,135):36, [135,180):42,
    //                [-180,-135):0, [-135,-90):6, [-90,-45):12, [-45,0):18
    int oct;
    if (ny >= 0.0f) {
        if (nx >= 0.0f) oct = (ay > ax) ? 1 : 0;
        else            oct = (ay > ax) ? 2 : 3;
    } else {
        if (nx < 0.0f)  oct = (ay > ax) ? 5 : 4;
        else            oct = (ay > ax) ? 6 : 7;
    }

    // m = tan(angle within octant) in [0,1]; max is non-zero because r > 0
    const float m = (ax >= ay) ? (ay / ax) : (ax / ay);

    // Sector-within-octant boundaries: tan(0), tan(7.5), ..., tan(45)
    static const float kTan[7] = {
        0.0f, 0.13165250f, 0.26794919f, 0.41421356f, 0.57735027f, 0.76732699f, 1.0f
    };
    int sm;
    if (m < kTan[3]) {
        if (m < kTan[1]) sm = 0;
        else             sm = (m < kTan[2]) ? 1 : 2;
    } else {
        if (m < kTan[4]) sm = 3;
        else             sm = (m < kTan[5]) ? 4 : 5;
    }
    float t = (m - kTan[sm]) / (kTan[sm + 1] - kTan[sm]);

    // Odd octants run counter to slope growth (angle decreases as m grows)
    const int s = (oct & 1) ? (5 - sm) : sm;
    if (oct & 1) {
        t = 1.0f - t;
    }

    static const int kOctBase[8] = {24, 30, 36, 42, 0, 6, 12, 18};
    sectorOut = kOctBase[oct] + s;
    tOut = t;
}

void UnifiedAnalogProcessorAddon::initializeCurveSegments(int stickNum, const UnifiedAnalogCurvePoint* controlPoints, int controlPointsCount) {
    sticks_[stickNum].curve_points_sorted_count = 0;
    sticks_[stickNum].curve_points_sorted[sticks_[stickNum].curve_points_sorted_count++] = {
        0.0f,
        0.0f,
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

void __not_in_flash_func(UnifiedAnalogProcessorAddon::applyResponseCurveToCoordinates)(float& normalizedX, float& normalizedY, int stickNum) {
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
            if (temp_curve_storage_[stickNum].saved_points_count < 6) {
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

    UnifiedAnalogCurvePoint converted[6];
    for (int i = 0; i < preset.points_count && i < 6; i++) {
        converted[i].x = preset.points[i].x;
        converted[i].y = preset.points[i].y;
    }
    initializeCurveSegments(stickNum, converted, preset.points_count);
}

void UnifiedAnalogProcessorAddon::initializeBezierCurve(int stickNum, const BezierControl& bezier) {
    StickState& stick = sticks_[stickNum];
    resolveBezierControl(bezier, stick.bezier_c1x, stick.bezier_c1y, stick.bezier_c2x, stick.bezier_c2y);
    // Always active in bezier mode: invalid data falls back to the linear default.
    stick.bezier_active = true;
}

void __not_in_flash_func(UnifiedAnalogProcessorAddon::applyBezierResponseCurve)(float& normalizedX, float& normalizedY, int stickNum) {
    if (normalizedX == 0.0f && normalizedY == 0.0f) {
        return;
    }

    const float dist = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);
    // Match the polyline curve boundary behavior: beyond the unit circle the
    // input is left unscaled and the per-axis clamp at the end of process()
    // caps the overshoot.
    if (dist > 1.0f) {
        return;
    }

    const float t = bezierFindT(dist, sticks_[stickNum].bezier_c1x, sticks_[stickNum].bezier_c2x);
    const float y = bezierEvalAxis(sticks_[stickNum].bezier_c1y, sticks_[stickNum].bezier_c2y, t);
    const float scale = y / dist;
    normalizedX *= scale;
    normalizedY *= scale;
}

void UnifiedAnalogProcessorAddon::saveCurrentBezierData(int stickNum) {
    if (stickNum < 0 || stickNum >= STICK_COUNT) return;

    temp_bezier_storage_[stickNum].c1x = sticks_[stickNum].bezier_c1x;
    temp_bezier_storage_[stickNum].c1y = sticks_[stickNum].bezier_c1y;
    temp_bezier_storage_[stickNum].c2x = sticks_[stickNum].bezier_c2x;
    temp_bezier_storage_[stickNum].c2y = sticks_[stickNum].bezier_c2y;
    temp_bezier_storage_[stickNum].is_saved = true;
}

void UnifiedAnalogProcessorAddon::restoreBezierData(int stickNum) {
    if (stickNum < 0 || stickNum >= STICK_COUNT || !temp_bezier_storage_[stickNum].is_saved) return;

    sticks_[stickNum].bezier_c1x = temp_bezier_storage_[stickNum].c1x;
    sticks_[stickNum].bezier_c1y = temp_bezier_storage_[stickNum].c1y;
    sticks_[stickNum].bezier_c2x = temp_bezier_storage_[stickNum].c2x;
    sticks_[stickNum].bezier_c2y = temp_bezier_storage_[stickNum].c2y;

    temp_bezier_storage_[stickNum].is_saved = false;
    active_activation_preset_[stickNum] = 0;
}

void UnifiedAnalogProcessorAddon::applyBezierPresetCurve(int stickNum, int presetIndex) {
    if (stickNum < 0 || stickNum >= STICK_COUNT || presetIndex < 0 || presetIndex >= 4) return;

    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    if (presetIndex >= analogOptions.joystick_bezier_presets_count ||
        !analogOptions.joystick_bezier_presets[presetIndex].has_bezier) {
        return;
    }

    const BezierPreset& preset = analogOptions.joystick_bezier_presets[presetIndex];
    // Pure in-memory temporary switch: preset data is normalized exactly like at
    // init (invalid data falls back to linear), never written to flash.
    resolveBezierControl(preset.bezier,
                         sticks_[stickNum].bezier_c1x,
                         sticks_[stickNum].bezier_c1y,
                         sticks_[stickNum].bezier_c2x,
                         sticks_[stickNum].bezier_c2y);
}
