#include "addons/analog.h"

#include "config.pb.h"
#include "hardware/adc.h"
#include "helper.h"
#include "storagemanager.h"

#define ADC_MAX ((1 << 12) - 1)
#define ADC_MAX_HALF (ADC_MAX * 0.5f)
#define ADC_PIN_OFFSET 26

AnalogInput* AnalogInput::s_instance_ = nullptr;

bool AnalogInput::available() {
    // 板载 ADC 摇杆输入源由前端「模拟摇杆」开关启用。
    return Storage::getInstance().getAddonOptions().analogOptions.enabled;
}

void AnalogInput::setup() {
    s_instance_ = this;
    refreshConfigFromStorage();
}

void AnalogInput::reinit() {
    refreshConfigFromStorage();
}

void AnalogInput::refreshConfigFromStorage() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    sticks_[0].x_pin = analogOptions.analogAdc1PinX;
    sticks_[0].y_pin = analogOptions.analogAdc1PinY;

    sticks_[1].x_pin = analogOptions.analogAdc2PinX;
    sticks_[1].y_pin = analogOptions.analogAdc2PinY;

    // 沿用网页「超采样」开关：开启时板载 ADC 每通道连续采样 64 次并输出 14bit
    oversampling_ = Storage::getInstance().getAddonOptions().mcp3208Options.oversampling;

    const uint16_t neutral = static_cast<uint16_t>(
        oversampling_ ? ANALOG_OVERSAMPLE_ADC_MAX * 0.5f : ADC_MAX_HALF);
    for (int i = 0; i < ADC_COUNT; i++) {
        sticks_[i].x_pin_adc = sticks_[i].x_pin - ADC_PIN_OFFSET;
        sticks_[i].y_pin_adc = sticks_[i].y_pin - ADC_PIN_OFFSET;
        sticks_[i].raw_x = neutral;
        sticks_[i].raw_y = neutral;
        sticks_[i].has_x = isValidPin(sticks_[i].x_pin);
        sticks_[i].has_y = isValidPin(sticks_[i].y_pin);

        if (sticks_[i].has_x) {
            adc_gpio_init(sticks_[i].x_pin);
        }
        if (sticks_[i].has_y) {
            adc_gpio_init(sticks_[i].y_pin);
        }
    }
}

uint16_t AnalogInput::readPinRaw(Pin_t pinAdc) {
    adc_select_input(pinAdc);
    if (!oversampling_) {
        return adc_read();
    }
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ANALOG_OVERSAMPLE_COUNT; i++) {
        sum += adc_read();
    }
    return static_cast<uint16_t>((sum + ANALOG_OVERSAMPLE_DIVISOR / 2) / ANALOG_OVERSAMPLE_DIVISOR);
}

void AnalogInput::preprocess() {
    for (int i = 0; i < ADC_COUNT; i++) {
        if (sticks_[i].has_x) {
            sticks_[i].raw_x = readPinRaw(sticks_[i].x_pin_adc);
        }
        if (sticks_[i].has_y) {
            sticks_[i].raw_y = readPinRaw(sticks_[i].y_pin_adc);
        }
    }
}

void AnalogInput::process() {
    // UnifiedAnalogProcessorAddon owns stick shaping and output mapping.
}

bool AnalogInput::getRawStickForProcessor(
    uint8_t stickNum,
    uint16_t& x,
    uint16_t& y,
    uint16_t& xCenter,
    uint16_t& yCenter,
    bool& xValid,
    bool& yValid,
    uint16_t& adcMax
) {
    if (s_instance_ == nullptr || stickNum >= ADC_COUNT) {
        return false;
    }

    const SamplerStick& stick = s_instance_->sticks_[stickNum];
    x = stick.raw_x;
    y = stick.raw_y;
    // 超采样开启时输出 14bit 量程，否则维持 12bit。
    const uint16_t max = s_instance_->oversampling_ ? ANALOG_OVERSAMPLE_ADC_MAX : ADC_MAX;
    xCenter = static_cast<uint16_t>(max * 0.5f);
    yCenter = static_cast<uint16_t>(max * 0.5f);
    xValid = stick.has_x;
    yValid = stick.has_y;
    adcMax = max;
    return true;
}
