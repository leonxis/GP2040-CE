#ifndef _Analog_H
#define _Analog_H

#include "gpaddon.h"
#include "enums.pb.h"
#include "types.h"

#include <cmath>





// Analog Module Name
#define AnalogName "Analog"

#define ADC_COUNT 2

class AnalogInput : public GPAddon {
public:
    virtual bool available();
    virtual void setup();       // Analog Setup
    virtual void process();     // No-op: processing moved to unified addon
    virtual void preprocess();
    virtual void postprocess(bool sent) {}
    virtual void reinit();
    virtual std::string name() { return AnalogName; }

    static bool getRawStickForProcessor(
        uint8_t stickNum,
        uint16_t& x,
        uint16_t& y,
        uint16_t& xCenter,
        uint16_t& yCenter,
        bool& xValid,
        bool& yValid,
        uint16_t& adcMax
    );

private:
    struct SamplerStick {
        Pin_t x_pin;
        Pin_t y_pin;
        Pin_t x_pin_adc;
        Pin_t y_pin_adc;
        uint16_t raw_x;
        uint16_t raw_y;
        bool has_x;
        bool has_y;
    } sticks_[ADC_COUNT];

    static AnalogInput* s_instance_;

    void refreshConfigFromStorage();
    uint16_t readPinRaw(Pin_t pinAdc);
};

/**
 * Inner/anti deadzone raw (uint32): legacy 0–20 = whole %; ≥200 = 200 + tenths (0.1% steps, 20.0% max → 400).
 */
inline float analogDeadzonePercentFromRaw(uint32_t raw)
{
    if (raw <= 20) {
        return static_cast<float>(raw);
    }
    if (raw >= 200) {
        return static_cast<float>(raw - 200) / 10.0f;
    }
    return static_cast<float>(raw);
}

inline float analogDeadzoneNormFromRaw(uint32_t raw)
{
    return analogDeadzonePercentFromRaw(raw) / 100.0f;
}

#endif  // _Analog_H_
