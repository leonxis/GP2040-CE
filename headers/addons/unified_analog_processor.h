#ifndef _Unified_Analog_Processor_H
#define _Unified_Analog_Processor_H

#include "gpaddon.h"
#include "config.pb.h"
#include "enums.pb.h"

#define UnifiedAnalogProcessorName "Unified Analog Processor"

typedef struct {
    float x;
    float y;
} UnifiedAnalogCurvePoint;

class UnifiedAnalogProcessorAddon : public GPAddon {
public:
    virtual bool available();
    virtual void setup();
    virtual void process();
    virtual void preprocess() {}
    virtual void postprocess(bool sent) {}
    virtual void reinit();
    virtual std::string name() { return UnifiedAnalogProcessorName; }

    // Latest calibration-normalized stick travel before deadzone/curve shaping.
    // nx/ny are in [-1, 1] along each axis; 1.0 corresponds to the calibrated
    // full travel of a direction (uniform_scale normalization), so the radial
    // magnitude maxes out at ~1.0 regardless of the ADC's electrical range.
    static bool getNormalizedStick(uint8_t stickNum, float& nx, float& ny);

private:
    static constexpr int STICK_COUNT = 2;
    static UnifiedAnalogProcessorAddon* s_instance_;
    enum class StickSource : uint8_t {
        None = 0,
        ADS8332,
        MCP3208,
        OnboardADC,
    };

    struct StickState {
        float x_value;
        float y_value;
        uint16_t x_center;
        uint16_t y_center;
        InvertMode analog_invert;
        DpadMode analog_dpad;
        float in_deadzone;
        float anti_deadzone;
        // Output radial cap fraction (0-0.3); 0 = disabled.
        float outer_deadzone;
        // Precomputed at init: (max(range_data)/uniform_scale)*(1-outer_deadzone).
        // Both are 0 when the stage is disabled or the stick is uncalibrated.
        float outer_radius_limit;
        float outer_radius_limit_sq;
        // Inner deadzone mode (repurposed field): false = linear (translate by
        // -dz with full-travel snap), true = fixed (pure mask, untouched outside).
        bool fixed_anti_deadzone;
        uint32_t jitter_filter;
        uint16_t last_x_adc;
        uint16_t last_y_adc;
        float range_data[48];
        bool has_range_calibration;
        float finetune_shape_amplify;
        float uniform_scale; // Precomputed direction-independent scale (set at init)
        // Per-sector snap need (linear deadzone mode): r1_sector - in_deadzone,
        // i.e. this direction's max reachable output. >= 1 means the direction
        // never needs snap; < 1 means snap triggers at r >= value + dz - margin.
        float snap_need[48];
        float min_snap_trigger; // min trigger over snap-enabled sectors (+inf if none)
        struct {
            float x;
            float y;
        } curve_points_sorted[8];
        uint8_t curve_points_sorted_count;
        struct {
            float slope;
            float intercept;
            float x_start;
            float x_end;
        } curve_segments[7];
        uint8_t curve_segments_count;
        // Bezier mode (joystick_curve_mode == 1): active whenever the curve toggle
        // is on in bezier mode — invalid data falls back to the linear default.
        bool bezier_active;
        float bezier_c1x;
        float bezier_c1y;
        float bezier_c2x;
        float bezier_c2y;
    };

    struct TempCurveStorage {
        bool is_saved;
        UnifiedAnalogCurvePoint saved_points[6];
        uint8_t saved_points_count;
    };

    struct TempBezierStorage {
        bool is_saved;
        float c1x;
        float c1y;
        float c2x;
        float c2y;
    };

    uint32_t usage_curve_profile_1_ = 0;
    uint32_t usage_curve_profile_2_ = 0;
    StickSource source_ = StickSource::None;
    TempCurveStorage temp_curve_storage_[STICK_COUNT];
    TempBezierStorage temp_bezier_storage_[STICK_COUNT];
    uint8_t active_activation_preset_[STICK_COUNT] = {0, 0};
    StickState sticks_[STICK_COUNT];
    float normalizedX_[STICK_COUNT] = {0.0f, 0.0f};
    float normalizedY_[STICK_COUNT] = {0.0f, 0.0f};
    bool normalizedValid_[STICK_COUNT] = {false, false};

    void initializeFromOptions();
    void resolveSource();
    void initializeStickFromOptions(int stickNum, const AnalogOptions& options, bool curveEnabled);
    uint16_t quantizeRaw(int stickNum, uint16_t value, bool isXAxis, uint16_t adcMax);
    void applyInnerDeadzone(int stickNum, float& nx, float& ny);
    void updateUniformScale(int stickNum);
    void initializeCurveSegments(int stickNum, const UnifiedAnalogCurvePoint* controlPoints, int controlPointsCount);
    void applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stickNum);
    void saveCurrentCurveData(int stickNum);
    void restoreCurveData(int stickNum);
    void applyPresetCurve(int stickNum, int presetIndex);
    void initializeBezierCurve(int stickNum, const BezierControl& bezier);
    void applyBezierResponseCurve(float& normalizedX, float& normalizedY, int stickNum);
    void saveCurrentBezierData(int stickNum);
    void restoreBezierData(int stickNum);
    void applyBezierPresetCurve(int stickNum, int presetIndex);
};

#endif
