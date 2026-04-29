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

private:
    static constexpr int STICK_COUNT = 2;
    enum class StickSource : uint8_t {
        None = 0,
        ADS8332,
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
        bool fixed_anti_deadzone;
        uint32_t jitter_filter;
        uint16_t last_x_adc;
        uint16_t last_y_adc;
        float range_data[48];
        bool has_range_calibration;
        bool finetune_shape_force_circular;
        float finetune_shape_amplify;
        struct {
            float x;
            float y;
        } curve_points_sorted[5];
        uint8_t curve_points_sorted_count;
        struct {
            float slope;
            float intercept;
            float x_start;
            float x_end;
        } curve_segments[4];
        uint8_t curve_segments_count;
    };

    struct TempCurveStorage {
        bool is_saved;
        UnifiedAnalogCurvePoint saved_points[3];
        uint8_t saved_points_count;
    };

    uint32_t usage_curve_profile_1_ = 0;
    uint32_t usage_curve_profile_2_ = 0;
    StickSource source_ = StickSource::None;
    TempCurveStorage temp_curve_storage_[STICK_COUNT];
    uint8_t active_activation_preset_[STICK_COUNT] = {0, 0};
    StickState sticks_[STICK_COUNT];

    void initializeFromOptions();
    void resolveSource();
    void initializeStickFromOptions(int stickNum, const AnalogOptions& options, bool curveEnabled);
    uint16_t quantizeRaw(int stickNum, uint16_t value, bool isXAxis, uint16_t adcMax);
    float getInterpolatedScale(int stickNum, float angle) const;
    void applyFinetuneShapeAdjustments(int stickNum);
    void initializeCurveSegments(int stickNum, const UnifiedAnalogCurvePoint* controlPoints, int controlPointsCount);
    void applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stickNum);
    void saveCurrentCurveData(int stickNum);
    void restoreCurveData(int stickNum);
    void applyPresetCurve(int stickNum, int presetIndex);
};

#endif
