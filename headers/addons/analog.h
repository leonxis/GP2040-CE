#ifndef _Analog_H
#define _Analog_H

#include "gpaddon.h"
#include "GamepadEnums.h"
#include "BoardConfig.h"
#include "enums.pb.h"
#include "types.h"

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
} adc_instance;

class AnalogInput : public GPAddon {
public:
    virtual bool available();
    virtual void setup();       // Analog Setup
    virtual void process();     // Analog Process
    virtual void preprocess() {}
    virtual void postprocess(bool sent) {}
    virtual void reinit() {}
    virtual std::string name() { return AnalogName; }
private:
    float readPin(int stick_num, Pin_t pin, uint16_t center, bool isXAxis);
    float getInterpolatedScale(int stick_num, float angle);
    void applyFinetuneShapeAdjustments(int stick_num);
    void trimToSquare(float x, float y, float& outX, float& outY);
    adc_instance adc_pairs[ADC_COUNT];
};

#endif  // _Analog_H_
