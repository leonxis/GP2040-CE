#include "addons/linear_trigger.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/adc.h"

#define ADC_MAX ((1 << 12) - 1)  // 4095
#define TRIGGER_OUT_MAX 255

// 每帧只做整数线性映射：raw 在 [minAdc, maxAdc] -> [0, 255]，阈值在 setup 中已预计算
static uint8_t adcToTrigger(uint16_t raw, int32_t minAdc, int32_t maxAdc) {
    if ((int32_t)raw <= minAdc)
        return 0;
    if ((int32_t)raw >= maxAdc)
        return TRIGGER_OUT_MAX;
    if (maxAdc <= minAdc)
        return 0;
    uint32_t outRange = (uint32_t)(maxAdc - minAdc);
    return (uint8_t)((uint32_t)((int32_t)raw - minAdc) * TRIGGER_OUT_MAX / outRange);
}

bool LinearTriggerAddon::available() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    return opts.enabled;
}

static void computeThresholds(int32_t releasedRaw, int32_t maxRaw,
        uint32_t deadzonePercent, uint32_t travelPercent,
        int32_t& outMinAdc, int32_t& outMaxAdc) {
    const int32_t ADC_MAX_I = (int32_t)ADC_MAX;
    int32_t r = releasedRaw < 0 ? 0 : releasedRaw;
    int32_t m = maxRaw > ADC_MAX_I ? ADC_MAX_I : maxRaw;
    if (r >= m) {
        outMinAdc = r;
        outMaxAdc = r + 1;  // 退化时 raw<=r->0, raw>r->255
        return;
    }
    int32_t range = m - r;
    float minF = (float)r + (float)range * ((float)deadzonePercent / 100.f);
    float maxF = (float)r + (float)range * ((float)travelPercent / 100.f);
    outMinAdc = (int32_t)(minF + 0.5f);
    outMaxAdc = (int32_t)(maxF + 0.5f);
    if (outMinAdc < r) outMinAdc = r;
    if (outMinAdc > m) outMinAdc = m;
    if (outMaxAdc < r) outMaxAdc = r;
    if (outMaxAdc > m) outMaxAdc = m;
}

void LinearTriggerAddon::reloadThresholds() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    computeThresholds(opts.leftTriggerReleasedRaw, opts.leftTriggerMaxRaw,
            opts.leftTriggerDeadzone, opts.leftTriggerTravel, minAdcL, maxAdcL);
    computeThresholds(opts.rightTriggerReleasedRaw, opts.rightTriggerMaxRaw,
            opts.rightTriggerDeadzone, opts.rightTriggerTravel, minAdcR, maxAdcR);
}

void LinearTriggerAddon::setup() {
    reloadThresholds();
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    gamepad->hasAnalogTriggers = true;
    adc_gpio_init(LINEAR_L2_PIN);
    adc_gpio_init(LINEAR_R2_PIN);
}

void LinearTriggerAddon::preprocess() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    adc_select_input(LINEAR_L2_PIN - 26);  // GPIO29 = ADC channel 3 = L2
    uint16_t rawL2 = adc_read();
    adc_select_input(LINEAR_R2_PIN - 26);  // GPIO28 = ADC channel 2 = R2
    uint16_t rawR2 = adc_read();
    gamepad->state.lt = adcToTrigger(rawL2, minAdcL, maxAdcL);
    if (gamepad->state.lt > 0)
        gamepad->state.buttons |= GAMEPAD_MASK_L2;
    else
        gamepad->state.buttons &= ~GAMEPAD_MASK_L2;
    gamepad->state.rt = adcToTrigger(rawR2, minAdcR, maxAdcR);
    if (gamepad->state.rt > 0)
        gamepad->state.buttons |= GAMEPAD_MASK_R2;
    else
        gamepad->state.buttons &= ~GAMEPAD_MASK_R2;
}
