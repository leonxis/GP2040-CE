#ifndef _Analog_H
#define _Analog_H

#include "gpaddon.h"
#include "enums.pb.h"
#include "types.h"

#include <algorithm>





// Analog Module Name
#define AnalogName "Analog"

#define ADC_COUNT 2

// 网页「超采样」开启时，板载 ADC 每通道连续转换次数
#define ANALOG_OVERSAMPLE_COUNT 64
// 64 次 12bit 采样求和后每 4 次平均对应 1 个 14bit LSB
#define ANALOG_OVERSAMPLE_DIVISOR (ANALOG_OVERSAMPLE_COUNT / 4)
// 超采样输出的 14bit 量程（与 12bit 的 4095 对应）
#define ANALOG_OVERSAMPLE_ADC_MAX ((1 << 14) - 1)

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
    bool oversampling_;

    void refreshConfigFromStorage();
    uint16_t readPinRaw(Pin_t pinAdc);
};

/**
 * Inner/anti deadzone (uint32): 0–800 = tenths of a percent (55 → 5.5%).
 * Percent = clamp(raw)/10, normalized factor = clamp(raw)/1000.
 */
inline float analogDeadzonePercentFromRaw(uint32_t raw)
{
    const uint32_t t = std::min<uint32_t>(raw, 800u);
    return std::min(80.0f, static_cast<float>(t) / 10.0f);
}

inline float analogDeadzoneNormFromRaw(uint32_t raw)
{
    const uint32_t t = std::min<uint32_t>(raw, 800u);
    return static_cast<float>(t) / 1000.0f;
}

#endif  // _Analog_H_
