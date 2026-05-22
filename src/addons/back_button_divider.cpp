#include "addons/back_button_divider.h"

#include "hml_back_mapping_preset.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

// RP2040: ADC0 = GPIO26, ADC1 = GPIO27
static constexpr uint16_t ADC_MAX_12BIT = (1u << 12) - 1u; // 4095

// 电压阈值（以 3.3V 参考电压近似换算 ADC 计数）
// >2.9V -> 无输出
// 2.9~2.1V -> 背键2
// 2.1~1.52V -> 背键1
// <1.52V -> 1+2 同时按下
static constexpr float VREF = 3.3f;
static constexpr float V_HIGH = 2.9f;
static constexpr float V_MID = 2.1f;
static constexpr float V_LOW = 1.52f;

static constexpr uint16_t THRESH_HIGH = static_cast<uint16_t>((V_HIGH / VREF) * ADC_MAX_12BIT + 0.5f);
static constexpr uint16_t THRESH_MID  = static_cast<uint16_t>((V_MID  / VREF) * ADC_MAX_12BIT + 0.5f);
static constexpr uint16_t THRESH_LOW  = static_cast<uint16_t>((V_LOW  / VREF) * ADC_MAX_12BIT + 0.5f);

// 防抖帧数：候选档位连续 N 帧一致才更新稳定档位
static constexpr uint8_t BACK_DIVIDER_DEBOUNCE_FRAMES = 3;
static constexpr uint8_t BACK_GPIO_DEBOUNCE_FRAMES = 3;

enum BackMapIndex : uint8_t {
    LEFT_BACK1 = 0,
    LEFT_BACK2 = 1,
    RIGHT_BACK1 = 2,
    RIGHT_BACK2 = 3,
    LEFT_EL = 4,
    RIGHT_ER = 5,
};

bool BackButtonDividerAddon::available() {
    // 始终可用：由是否设置了背键映射决定是否实际输出
    return true;
}

void BackButtonDividerAddon::buildMappings() {
    const BackButtonAddonOptions& opts = getActiveBackButtonOptions(Storage::getInstance().getAddonOptions());
    mapTable_.setCount(6);
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_BACK1)) {
        ActionMappingCommon::parseActionMapping(opts.leftBack1Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_BACK2)) {
        ActionMappingCommon::parseActionMapping(opts.leftBack2Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_BACK1)) {
        ActionMappingCommon::parseActionMapping(opts.rightBack1Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_BACK2)) {
        ActionMappingCommon::parseActionMapping(opts.rightBack2Mapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(LEFT_EL)) {
        ActionMappingCommon::parseActionMapping(opts.leftElMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(RIGHT_ER)) {
        ActionMappingCommon::parseActionMapping(opts.rightErMapping, *entry);
    }
}

void BackButtonDividerAddon::applySlotIfEnabled(Gamepad* gamepad, uint8_t index) {
    const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(index);
    if (entry != nullptr) {
        outputScope_.apply(gamepad, *entry);
    }
}

void BackButtonDividerAddon::setup() {
    // 初始化 ADC 硬件（只做一次）
    if (!adcInitialized) {
        adc_init();
        adc_gpio_init(26);
        adc_gpio_init(27);
        adcInitialized = true;
    }

    gpio_init(GPIO_PIN_LEFT_EL);
    gpio_set_dir(GPIO_PIN_LEFT_EL, GPIO_IN);
    gpio_pull_up(GPIO_PIN_LEFT_EL);

    gpio_init(GPIO_PIN_RIGHT_ER);
    gpio_set_dir(GPIO_PIN_RIGHT_ER, GPIO_IN);
    gpio_pull_up(GPIO_PIN_RIGHT_ER);

    buildMappings();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceLevel(leftDebounce_, -1);
    ActionMappingCommon::resetDebounceLevel(rightDebounce_, -1);
    ActionMappingCommon::resetDebounceBool(leftElDebounce_, false);
    ActionMappingCommon::resetDebounceBool(rightErDebounce_, false);
}

void BackButtonDividerAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceLevel(leftDebounce_, -1);
    ActionMappingCommon::resetDebounceLevel(rightDebounce_, -1);
    ActionMappingCommon::resetDebounceBool(leftElDebounce_, false);
    ActionMappingCommon::resetDebounceBool(rightErDebounce_, false);
}

void BackButtonDividerAddon::preprocess() {
    if (!adcInitialized) {
        return;
    }

    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    // 读取左侧（ADC0, GPIO26）
    adc_select_input(0);
    uint16_t rawLeft = adc_read();
    // 读取右侧（ADC1, GPIO27）
    adc_select_input(1);
    uint16_t rawRight = adc_read();

    auto classifyLevel = [](uint16_t raw) -> int8_t {
        if (raw > THRESH_HIGH) return -1; // 无输出
        if (raw > THRESH_MID)  return 0;  // 背键2
        if (raw > THRESH_LOW)  return 1;  // 背键1
        return 2;                          // 背键1+2
    };

    const int8_t candLeft = classifyLevel(rawLeft);
    const int8_t candRight = classifyLevel(rawRight);
    ActionMappingCommon::updateDebounceLevel(candLeft, leftDebounce_, BACK_DIVIDER_DEBOUNCE_FRAMES, true);
    ActionMappingCommon::updateDebounceLevel(candRight, rightDebounce_, BACK_DIVIDER_DEBOUNCE_FRAMES, true);

    auto applyByStableLevel = [&](int8_t level, uint8_t idx1, uint8_t idx2) {
        if (level < 0) {
            return;
        }
        if (level == 0) {
            applySlotIfEnabled(gamepad, idx2);
            return;
        }
        if (level == 1) {
            applySlotIfEnabled(gamepad, idx1);
            return;
        }
        applySlotIfEnabled(gamepad, idx1);
        applySlotIfEnabled(gamepad, idx2);
    };

    applyByStableLevel(leftDebounce_.stable, LEFT_BACK1, LEFT_BACK2);
    applyByStableLevel(rightDebounce_.stable, RIGHT_BACK1, RIGHT_BACK2);

    const bool leftElPressed = !gpio_get(GPIO_PIN_LEFT_EL);
    const bool rightErPressed = !gpio_get(GPIO_PIN_RIGHT_ER);
    ActionMappingCommon::updateDebounceBool(leftElPressed, leftElDebounce_, BACK_GPIO_DEBOUNCE_FRAMES);
    ActionMappingCommon::updateDebounceBool(rightErPressed, rightErDebounce_, BACK_GPIO_DEBOUNCE_FRAMES);

    if (leftElDebounce_.stable) {
        applySlotIfEnabled(gamepad, LEFT_EL);
    }
    if (rightErDebounce_.stable) {
        applySlotIfEnabled(gamepad, RIGHT_ER);
    }

    outputScope_.endFrame();
}

void BackButtonDividerAddon::process() {
    // 所有逻辑在 preprocess 中完成
}
