#include "addons/linear_trigger.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/adc.h"

#define ADC_MAX ((1 << 12) - 1)  // 4095
#define TRIGGER_OUT_MAX 255

static uint8_t adcToTrigger(uint16_t raw, uint32_t deadzonePercent, uint32_t travelPercent) {
    float rawNorm = (float)raw / (float)ADC_MAX;
    float deadzone = (float)deadzonePercent / 100.f;
    float travel = (float)travelPercent / 100.f;
    if (rawNorm <= deadzone)
        return 0;
    float effective = (rawNorm - deadzone) / (travel - deadzone);
    if (effective > 1.f) effective = 1.f;
    return (uint8_t)(effective * (float)TRIGGER_OUT_MAX);
}

bool LinearTriggerAddon::available() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    return opts.enabled;
}

void LinearTriggerAddon::setup() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    deadzoneL = opts.leftTriggerDeadzone;
    deadzoneR = opts.rightTriggerDeadzone;
    travelL = opts.leftTriggerTravel;
    travelR = opts.rightTriggerTravel;
    adc_gpio_init(LINEAR_L2_PIN);
    adc_gpio_init(LINEAR_R2_PIN);
}

void LinearTriggerAddon::preprocess() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    adc_select_input(LINEAR_L2_PIN - 26);  // channel 2
    uint16_t rawL2 = adc_read();
    adc_select_input(LINEAR_R2_PIN - 26);  // channel 3
    uint16_t rawR2 = adc_read();
    gamepad->hasAnalogTriggers = true;
    gamepad->state.lt = adcToTrigger(rawL2, deadzoneL, travelL);
    if (gamepad->state.lt > 0)
        gamepad->state.buttons |= GAMEPAD_MASK_L2;
    else
        gamepad->state.buttons &= ~GAMEPAD_MASK_L2;
    gamepad->state.rt = adcToTrigger(rawR2, deadzoneR, travelR);
    if (gamepad->state.rt > 0)
        gamepad->state.buttons |= GAMEPAD_MASK_R2;
    else
        gamepad->state.buttons &= ~GAMEPAD_MASK_R2;
}
