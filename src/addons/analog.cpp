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
    sticks_[0].joystick_center_x = analogOptions.joystick_center_x;
    sticks_[0].joystick_center_y = analogOptions.joystick_center_y;
    sticks_[0].jitter_filter = analogOptions.joystick_jitter_filter_1;

    sticks_[1].x_pin = analogOptions.analogAdc2PinX;
    sticks_[1].y_pin = analogOptions.analogAdc2PinY;
    sticks_[1].joystick_center_x = analogOptions.joystick_center_x2;
    sticks_[1].joystick_center_y = analogOptions.joystick_center_y2;
    sticks_[1].jitter_filter = analogOptions.joystick_jitter_filter_2;

    for (int i = 0; i < ADC_COUNT; i++) {
        sticks_[i].x_pin_adc = sticks_[i].x_pin - ADC_PIN_OFFSET;
        sticks_[i].y_pin_adc = sticks_[i].y_pin - ADC_PIN_OFFSET;
        sticks_[i].x_center = (sticks_[i].joystick_center_x > 0)
            ? static_cast<uint16_t>(sticks_[i].joystick_center_x)
            : static_cast<uint16_t>(ADC_MAX_HALF);
        sticks_[i].y_center = (sticks_[i].joystick_center_y > 0)
            ? static_cast<uint16_t>(sticks_[i].joystick_center_y)
            : static_cast<uint16_t>(ADC_MAX_HALF);
        sticks_[i].last_x_adc = sticks_[i].x_center;
        sticks_[i].last_y_adc = sticks_[i].y_center;
        sticks_[i].raw_x = sticks_[i].x_center;
        sticks_[i].raw_y = sticks_[i].y_center;
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

uint16_t AnalogInput::readPinQuantized(int stickNum, Pin_t pinAdc, bool isXAxis) {
    adc_select_input(pinAdc);
    uint16_t adcValue = adc_read();

    uint32_t step = sticks_[stickNum].jitter_filter;
    uint16_t* last = isXAxis ? &sticks_[stickNum].last_x_adc : &sticks_[stickNum].last_y_adc;

    if (step > 0) {
        constexpr uint32_t kMaxQuantStep = static_cast<uint32_t>(ADC_MAX) + 1u;
        if (step > kMaxQuantStep) {
            step = kMaxQuantStep;
        }
        const uint32_t half = step / 2u;
        const uint32_t rounded = (static_cast<uint32_t>(adcValue) + half) / step;
        uint32_t q = rounded * step;
        if (q > ADC_MAX) {
            q = ADC_MAX;
        }
        adcValue = static_cast<uint16_t>(q);
    }

    *last = adcValue;
    return adcValue;
}

void AnalogInput::preprocess() {
    for (int i = 0; i < ADC_COUNT; i++) {
        if (sticks_[i].has_x) {
            sticks_[i].raw_x = readPinQuantized(i, sticks_[i].x_pin_adc, true);
        }
        if (sticks_[i].has_y) {
            sticks_[i].raw_y = readPinQuantized(i, sticks_[i].y_pin_adc, false);
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
    bool& yValid
) {
    if (s_instance_ == nullptr || stickNum >= ADC_COUNT) {
        return false;
    }

    const SamplerStick& stick = s_instance_->sticks_[stickNum];
    x = stick.raw_x;
    y = stick.raw_y;
    xCenter = stick.x_center;
    yCenter = stick.y_center;
    xValid = stick.has_x;
    yValid = stick.has_y;
    return true;
}
