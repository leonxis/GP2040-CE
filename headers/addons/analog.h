#ifndef _Analog_H
#define _Analog_H

#include "gpaddon.h"
#include "enums.pb.h"
#include "types.h"

#include <algorithm>
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
 * Inner/anti deadzone persistence (uint32): canonical range 0–200 = tenths of a percent (55 → 5.5%).
 * Percent = tenths/10, normalized stick factor = tenths/1000.
 * Migrates: raw ≤20 → legacy whole percent ×10; 200–400 → previous 200+tenths encoding.
 */
inline uint32_t analogDeadzoneMigrateToTenths(uint32_t raw)
{
    if (raw <= 20u) {
        return raw * 10u;
    }
    if (raw >= 200u && raw <= 400u) {
        return raw - 200u;
    }
    return std::min(200u, raw);
}

inline float analogDeadzonePercentFromRaw(uint32_t raw)
{
    const uint32_t t = analogDeadzoneMigrateToTenths(raw);
    return std::min(20.0f, static_cast<float>(t) / 10.0f);
}

inline float analogDeadzoneNormFromRaw(uint32_t raw)
{
    const uint32_t t = analogDeadzoneMigrateToTenths(raw);
    return static_cast<float>(t) / 1000.0f;
}

#endif  // _Analog_H_
