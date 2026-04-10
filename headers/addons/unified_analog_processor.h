#ifndef _Unified_Analog_Processor_H
#define _Unified_Analog_Processor_H

#include "gpaddon.h"
#include "config.pb.h"
#include "enums.pb.h"

#define UnifiedAnalogProcessorName "Unified Analog Processor"

typedef struct {
    float x;
    float y;
    uint32_t buttonMask;
} UnifiedAnalogCurvePoint;

class Gamepad;

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
        float range_data[48];
        bool has_range_calibration;
        bool finetune_shape_force_circular;
        float finetune_shape_amplify;
        struct {
            float x;
            float y;
            uint32_t buttonMask;
        } curve_points_sorted[5];
        uint8_t curve_points_sorted_count;
        struct {
            float slope;
            float intercept;
            float x_start;
            float x_end;
        } curve_segments[4];
        uint8_t curve_segments_count;
        uint8_t active_control_points_mask;
    };

    struct TempCurveStorage {
        bool is_saved;
        UnifiedAnalogCurvePoint saved_points[3];
        uint8_t saved_points_count;
    };

    uint32_t usage_curve_profile_1_ = 0;
    uint32_t usage_curve_profile_2_ = 0;
    TempCurveStorage temp_curve_storage_[STICK_COUNT];
    uint8_t active_activation_preset_[STICK_COUNT] = {0, 0};
    StickState sticks_[STICK_COUNT];

    void initializeFromOptions();
    void initializeStickFromOptions(int stickNum, const AnalogOptions& options, bool curveEnabled);
    float getInterpolatedScale(int stickNum, float angle) const;
    void applyFinetuneShapeAdjustments(int stickNum);
    void initializeCurveSegments(int stickNum, const UnifiedAnalogCurvePoint* controlPoints, int controlPointsCount);
    void applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stickNum, Gamepad* gamepad);
    void forceReleaseActiveControlPoints(int stickNum, Gamepad* gamepad);
    void saveCurrentCurveData(int stickNum);
    void restoreCurveData(int stickNum);
    void applyPresetCurve(int stickNum, int presetIndex);
};

#endif
