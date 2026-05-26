#include "addons/linear_trigger.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "hardware/adc.h"

#define ADC_MAX ((1 << 12) - 1)  // 4095
#define TRIGGER_OUT_MAX 255

// 真实硬件：扳机松开=高 ADC，扳机压下=低 ADC。不做反转，直接使用真实 ADC。
// highAdc=松开侧阈值，lowAdc=按到底侧阈值；raw >= highAdc -> 0，raw <= lowAdc -> 255，中间线性
static uint8_t adcToTrigger(uint16_t raw, int32_t highAdc, int32_t lowAdc) {
    if ((int32_t)raw >= highAdc)
        return 0;
    if ((int32_t)raw <= lowAdc)
        return TRIGGER_OUT_MAX;
    if (highAdc <= lowAdc)
        return 0;
    uint32_t range = (uint32_t)(highAdc - lowAdc);
    return (uint8_t)((uint32_t)(highAdc - (int32_t)raw) * TRIGGER_OUT_MAX / range);
}

bool LinearTriggerAddon::available() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    return opts.enabled;
}

// 真实 ADC：releasedRaw=松开时高电压，maxRaw=按到底时低电压，故 releasedRaw > maxRaw
// 输出：outHighAdc=松开侧阈值（raw>=此值输出0），outLowAdc=按到底侧阈值（raw<=此值输出255）
static void computeThresholds(int32_t releasedRaw, int32_t maxRaw,
        uint32_t deadzonePercent, uint32_t travelPercent,
        int32_t& outHighAdc, int32_t& outLowAdc) {
    const int32_t ADC_MAX_I = (int32_t)ADC_MAX;
    int32_t r = releasedRaw < 0 ? 0 : releasedRaw;
    int32_t m = maxRaw > ADC_MAX_I ? ADC_MAX_I : (maxRaw < 0 ? 0 : maxRaw);
    if (r <= m) {
        outHighAdc = r;
        outLowAdc = r - 1;
        return;
    }
    int32_t range = r - m;
    float highF = (float)r - (float)range * ((float)deadzonePercent / 100.f);
    float lowF = (float)r - (float)range * ((float)travelPercent / 100.f);
    outHighAdc = (int32_t)(highF + 0.5f);
    outLowAdc = (int32_t)(lowF + 0.5f);
    if (outHighAdc > r) outHighAdc = r;
    if (outHighAdc < m) outHighAdc = m;
    if (outLowAdc < m) outLowAdc = m;
    if (outLowAdc > r) outLowAdc = r;
}

void LinearTriggerAddon::reloadThresholds() {
    const LinearTriggerOptions& opts = Storage::getInstance().getAddonOptions().linearTriggerOptions;
    // 真实 ADC 与存储一致：releasedRaw=松开时高电压，maxRaw=按到底时低电压
    const int32_t releasedL = opts.leftTriggerReleasedRaw < 0 ? 0 : opts.leftTriggerReleasedRaw;
    const int32_t maxL = opts.leftTriggerMaxRaw > (int32_t)ADC_MAX ? (int32_t)ADC_MAX : opts.leftTriggerMaxRaw;
    const int32_t releasedR = opts.rightTriggerReleasedRaw < 0 ? 0 : opts.rightTriggerReleasedRaw;
    const int32_t maxR = opts.rightTriggerMaxRaw > (int32_t)ADC_MAX ? (int32_t)ADC_MAX : opts.rightTriggerMaxRaw;
    computeThresholds(releasedL, maxL, opts.leftTriggerDeadzone, opts.leftTriggerTravel, minAdcL, maxAdcL);
    computeThresholds(releasedR, maxR, opts.rightTriggerDeadzone, opts.rightTriggerTravel, minAdcR, maxAdcR);
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
    if (gamepad == nullptr) {
        return;
    }

    adc_select_input(LINEAR_L2_PIN - 26);  // GPIO29 = ADC channel 3 = L2
    uint16_t rawL2 = adc_read();  // 真实 ADC：松开=高，压下=低
    adc_select_input(LINEAR_R2_PIN - 26);  // GPIO28 = ADC channel 2 = R2
    uint16_t rawR2 = adc_read();
    const uint8_t hwLt = adcToTrigger(rawL2, minAdcL, maxAdcL);
    const uint8_t hwRt = adcToTrigger(rawR2, minAdcR, maxAdcR);

    // 硬件 ADC 为 lt/rt 基准；背键/触摸板/FN 等映射可能已置 L2/R2 位，不得在此处清除。
    if (gamepad->state.buttons & GAMEPAD_MASK_L2) {
        gamepad->state.lt = (hwLt > gamepad->state.lt) ? hwLt : gamepad->state.lt;
    } else {
        gamepad->state.lt = hwLt;
    }
    if (gamepad->state.buttons & GAMEPAD_MASK_R2) {
        gamepad->state.rt = (hwRt > gamepad->state.rt) ? hwRt : gamepad->state.rt;
    } else {
        gamepad->state.rt = hwRt;
    }

    // 硬件模拟量 → 数字扳机位；仅在无映射/虚拟输入时清除，避免抹掉背键等映射
    if (hwLt > 0) {
        gamepad->state.buttons |= GAMEPAD_MASK_L2;
    } else if (gamepad->state.lt == 0) {
        gamepad->state.buttons &= ~GAMEPAD_MASK_L2;
    }
    if (hwRt > 0) {
        gamepad->state.buttons |= GAMEPAD_MASK_R2;
    } else if (gamepad->state.rt == 0) {
        gamepad->state.buttons &= ~GAMEPAD_MASK_R2;
    }

    // 映射/热键等仅置 L2/R2 位时，补齐模拟量（须在 USB 驱动读取前完成）
    if ((gamepad->state.buttons & GAMEPAD_MASK_L2) && gamepad->state.lt == 0) {
        gamepad->state.lt = GAMEPAD_TRIGGER_MAX;
    }
    if ((gamepad->state.buttons & GAMEPAD_MASK_R2) && gamepad->state.rt == 0) {
        gamepad->state.rt = GAMEPAD_TRIGGER_MAX;
    }
}
