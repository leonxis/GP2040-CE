#ifndef _Analog_H
#define _Analog_H

#include "gpaddon.h"
#include "GamepadEnums.h"
#include "BoardConfig.h"
#include "enums.pb.h"
#include "types.h"

// Forward declaration
class Gamepad;

#ifndef ANALOG_INPUT_ENABLED
#define ANALOG_INPUT_ENABLED 0
#endif

#ifndef ANALOG_ADC_1_VRX
#define ANALOG_ADC_1_VRX    -1
#endif

#ifndef ANALOG_ADC_1_VRY
#define ANALOG_ADC_1_VRY    -1
#endif

#ifndef ANALOG_ADC_1_MODE
#define ANALOG_ADC_1_MODE DPAD_MODE_LEFT_ANALOG
#endif

#ifndef ANALOG_ADC_1_INVERT
#define ANALOG_ADC_1_INVERT INVERT_NONE
#endif

#ifndef ANALOG_ADC_2_VRX
#define ANALOG_ADC_2_VRX    -1
#endif

#ifndef ANALOG_ADC_2_VRY
#define ANALOG_ADC_2_VRY    -1
#endif

#ifndef ANALOG_ADC_2_MODE
#define ANALOG_ADC_2_MODE DPAD_MODE_RIGHT_ANALOG
#endif

#ifndef ANALOG_ADC_2_INVERT
#define ANALOG_ADC_2_INVERT INVERT_NONE
#endif

#ifndef DEFAULT_INNER_DEADZONE
#define DEFAULT_INNER_DEADZONE 5
#endif

#ifndef DEFAULT_INNER_DEADZONE2
#define DEFAULT_INNER_DEADZONE2 5
#endif

#ifndef DEFAULT_ANTI_DEADZONE
#define DEFAULT_ANTI_DEADZONE 0
#endif

#ifndef DEFAULT_ANTI_DEADZONE2
#define DEFAULT_ANTI_DEADZONE2 0
#endif

// Analog Module Name
#define AnalogName "Analog"

#define ADC_COUNT 2

// Curve point structure for response curve (avoid conflict with protobuf CurvePoint)
typedef struct {
    float x;
    float y;
    uint32_t buttonMask;  // Button mask to trigger when joystick reaches this control point
} AnalogCurvePoint;

typedef struct
{
    Pin_t x_pin;
    Pin_t y_pin;
    Pin_t x_pin_adc;
    Pin_t y_pin_adc;
    float x_value;
    float y_value;
    uint16_t x_center;
    uint16_t y_center;
    InvertMode analog_invert;
    DpadMode analog_dpad;
    float in_deadzone;
    // out_deadzone and forced_circularity removed - replaced by range calibration
    float anti_deadzone;
    bool fixed_anti_deadzone;  // true = fixed anti-deadzone, false = linear anti-deadzone
    uint32_t joystick_center_x;
    uint32_t joystick_center_y;
    // Jitter filter configuration (ADC units). 0 = disabled.
    uint32_t jitter_filter;
    // Last raw ADC readings for jitter filtering
    uint16_t last_x_adc;
    uint16_t last_y_adc;
    float range_data[48];  // Circularity data for 48 angular positions
    bool has_range_calibration;  // Flag to indicate if range calibration data exists
    // Finetune shape adjustment settings (independent from calibration data)
    bool finetune_shape_force_circular;
    float finetune_shape_amplify;
    // Preprocessed curve points array: start (0,0) + sorted control points + end (1,1)
    // Note: Frontend saves control points sorted by x coordinate
    struct {
        float x;
        float y;
        uint32_t buttonMask;  // Button mask to trigger when joystick reaches this control point
    } curve_points_sorted[5];  // max 5 points: (0,0) + 3 control + (1,1)
    uint8_t curve_points_sorted_count;  // Total number of points in sorted array (0-5, 0 means no curve)
    // Precomputed curve segment parameters for fast lookup: slope and intercept for each segment
    // For segment [p1x, p2x]: curvedMagnitude = intercept + magnitude * slope
    struct {
        float slope;      // Slope of the segment: (p2y - p1y) / (p2x - p1x)
        float intercept;  // Intercept: p1y - p1x * slope
        float x_start;    // Start x of segment (p1x)
        float x_end;      // End x of segment (p2x)
    } curve_segments[4];  // max 4 segments: (0,0)->p1, p1->p2, p2->p3, p3->(1,1)
    uint8_t curve_segments_count;  // Number of segments (0-4, 0 means no curve)
    float curve_extrapolate_slope;  // Slope for extrapolation when magnitude > 1.0
    // Track which control points are currently active (for button triggering)
    // Bitmask: bit 0 = control point 0, bit 1 = control point 1, bit 2 = control point 2
    uint8_t active_control_points_mask;  // 0 = no active points, bits set indicate active points
} adc_instance;

class AnalogInput : public GPAddon {
public:
    virtual bool available();
    virtual void setup();       // Analog Setup
    virtual void process();     // Analog Process
    virtual void preprocess() {}
    virtual void postprocess(bool sent) {}
    virtual void reinit();
    virtual std::string name() { return AnalogName; }
private:
    // Track current curve profile in use for both sticks (0 = custom, 1-4 = preset 1-4)
    uint32_t usage_curve_profile_1;
    uint32_t usage_curve_profile_2;
    float readPin(int stick_num, Pin_t pin, uint16_t center, bool isXAxis);
    float getInterpolatedScale(int stick_num, float angle);
    void applyFinetuneShapeAdjustments(int stick_num);
    void initializeCurveSegments(int stick_num, const AnalogCurvePoint* control_points, int control_points_count);
    void applyResponseCurveToCoordinates(float& normalizedX, float& normalizedY, int stick_num, Gamepad* gamepad);
    void forceReleaseActiveControlPoints(int stick_num, Gamepad* gamepad);
    adc_instance adc_pairs[ADC_COUNT];
};

#endif  // _Analog_H_
