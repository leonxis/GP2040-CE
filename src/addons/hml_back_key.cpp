#include "addons/hml_back_key.h"

#include "hml_back_mapping_preset.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"

// 防抖帧数：电平连续 N 帧一致才更新稳定状态
static constexpr uint8_t HML_BACK_KEY_DEBOUNCE_FRAMES = 3;

enum BackKeyIndex : uint8_t {
    LB1 = 0,  // 左背键1
    RB1,      // 右背键1
    LB2,      // 左背键2
    RB2,      // 右背键2
    LB3,      // 左背键3
    RB3,      // 右背键3
    LFN,      // 左FN
    RFN,      // 右FN
    LMT,      // 左MT
    RMT,      // 右MT
    KEY_COUNT
};

struct BackKeyPinDef {
    uint8_t pin;
    GpioMappingInfo HmlBackMappingPreset::*field;
};

static const BackKeyPinDef kBackKeyPins[KEY_COUNT] = {
#if defined(HML_BACK_KEY_LB1_PIN)
    { HML_BACK_KEY_LB1_PIN, &HmlBackMappingPreset::leftBack1Mapping },
#endif
#if defined(HML_BACK_KEY_RB1_PIN)
    { HML_BACK_KEY_RB1_PIN, &HmlBackMappingPreset::rightBack1Mapping },
#endif
#if defined(HML_BACK_KEY_LB2_PIN)
    { HML_BACK_KEY_LB2_PIN, &HmlBackMappingPreset::leftBack2Mapping },
#endif
#if defined(HML_BACK_KEY_RB2_PIN)
    { HML_BACK_KEY_RB2_PIN, &HmlBackMappingPreset::rightBack2Mapping },
#endif
#if defined(HML_BACK_KEY_LB3_PIN)
    { HML_BACK_KEY_LB3_PIN, &HmlBackMappingPreset::leftBack3Mapping },
#endif
#if defined(HML_BACK_KEY_RB3_PIN)
    { HML_BACK_KEY_RB3_PIN, &HmlBackMappingPreset::rightBack3Mapping },
#endif
#if defined(HML_BACK_KEY_LFN_PIN)
    { HML_BACK_KEY_LFN_PIN, &HmlBackMappingPreset::leftFnMapping },
#endif
#if defined(HML_BACK_KEY_RFN_PIN)
    { HML_BACK_KEY_RFN_PIN, &HmlBackMappingPreset::rightFnMapping },
#endif
#if defined(HML_BACK_KEY_LMT_PIN)
    { HML_BACK_KEY_LMT_PIN, &HmlBackMappingPreset::leftMtMapping },
#endif
#if defined(HML_BACK_KEY_RMT_PIN)
    { HML_BACK_KEY_RMT_PIN, &HmlBackMappingPreset::rightMtMapping },
#endif
};

bool HmlBackKeyAddon::available() {
#if defined(HML_BACK_KEY_LB1_PIN) && defined(HML_BACK_KEY_RB1_PIN) \
    && defined(HML_BACK_KEY_LB2_PIN) && defined(HML_BACK_KEY_RB2_PIN) \
    && defined(HML_BACK_KEY_LB3_PIN) && defined(HML_BACK_KEY_RB3_PIN) \
    && defined(HML_BACK_KEY_LFN_PIN) && defined(HML_BACK_KEY_RFN_PIN) \
    && defined(HML_BACK_KEY_LMT_PIN) && defined(HML_BACK_KEY_RMT_PIN)
    // 始终可用：由背键映射是否配置决定是否实际输出
    return true;
#else
    return false;
#endif
}

void HmlBackKeyAddon::buildMappings() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const HmlBackMappingPreset& activePreset = getActiveHmlBackPreset(addonOptions);
    mapTable_.setCount(KEY_COUNT);
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(i);
        if (entry != nullptr) {
            ActionMappingCommon::parseActionMapping(activePreset.*(kBackKeyPins[i].field), *entry);
        }
    }
}

void HmlBackKeyAddon::setup() {
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        gpio_init(kBackKeyPins[i].pin);
        gpio_set_dir(kBackKeyPins[i].pin, GPIO_IN);
        gpio_pull_up(kBackKeyPins[i].pin);
    }

    buildMappings();
    outputScope_.reset();
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        ActionMappingCommon::resetDebounceBool(debounce_[i], false);
    }
}

void HmlBackKeyAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        ActionMappingCommon::resetDebounceBool(debounce_[i], false);
    }
}

void HmlBackKeyAddon::preprocess() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        const bool pressed = !gpio_get(kBackKeyPins[i].pin);
        ActionMappingCommon::updateDebounceBool(pressed, debounce_[i], HML_BACK_KEY_DEBOUNCE_FRAMES);
        if (debounce_[i].stable) {
            const ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(i);
            if (entry != nullptr) {
                outputScope_.apply(gamepad, *entry);
            }
        }
    }

    outputScope_.endFrame();
}

void HmlBackKeyAddon::process() {
    // 所有逻辑在 preprocess 中完成
}
