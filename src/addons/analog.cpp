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
    // 固件侧强制禁用板载 ADC 模拟摇杆插件，不读取前端 AnalogInputEnabled。
    return false;
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

    for (int i = 0; i < ADC_COUNT; i++) {
        sticks_[i].x_pin_adc = sticks_[i].x_pin - ADC_PIN_OFFSET;
        sticks_[i].y_pin_adc = sticks_[i].y_pin - ADC_PIN_OFFSET;
        sticks_[i].raw_x = static_cast<uint16_t>(ADC_MAX_HALF);
        sticks_[i].raw_y = static_cast<uint16_t>(ADC_MAX_HALF);
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
    return adc_read();
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
    xCenter = static_cast<uint16_t>(ADC_MAX_HALF);
    yCenter = static_cast<uint16_t>(ADC_MAX_HALF);
    xValid = stick.has_x;
    yValid = stick.has_y;
    adcMax = ADC_MAX;
    return true;
}
