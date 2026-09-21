#include "addons/hml_back_key.h"

#include "hml_back_mapping_preset.h"
#include "storagemanager.h"
#include "gamepad.h"
#include "hardware/gpio.h"

// 防抖帧数：电平连续 N 帧一致才更新稳定状态
static constexpr uint8_t HML_BACK_KEY_DEBOUNCE_FRAMES = 3;

// 2354B 版型背键/FN/MT 固定走线（低电平有效），引脚号硬编码于此：
// 顺序固定：LB1/RB1, LB2/RB2, LB3/RB3, LFN/RFN, LMT/RMT
extern const HmlBackKeyDef kHmlBackKeyDefs[HML_BACK_KEY_COUNT] = {
    { 31, "leftBack1",  &HmlBackMappingPreset::leftBack1Mapping  }, // 左背键1
    { 30, "rightBack1", &HmlBackMappingPreset::rightBack1Mapping }, // 右背键1
    { 33, "leftBack2",  &HmlBackMappingPreset::leftBack2Mapping  }, // 左背键2
    { 34, "rightBack2", &HmlBackMappingPreset::rightBack2Mapping }, // 右背键2
    { 35, "leftBack3",  &HmlBackMappingPreset::leftBack3Mapping  }, // 左背键3
    { 36, "rightBack3", &HmlBackMappingPreset::rightBack3Mapping }, // 右背键3
    { 45, "leftFn",     &HmlBackMappingPreset::leftFnMapping     }, // 左FN
    { 39, "rightFn",    &HmlBackMappingPreset::rightFnMapping    }, // 右FN
    { 47, "leftMt",     &HmlBackMappingPreset::leftMtMapping     }, // 左MT
    { 38, "rightMt",    &HmlBackMappingPreset::rightMtMapping    }, // 右MT
};

bool HmlBackKeyAddon::available() {
    // 仅 48 GPIO 的 RP2350B 版型启用；引脚为固定走线，无需 BoardConfig 宏
    return HML_BACK_KEY_SUPPORTED;
}

void HmlBackKeyAddon::buildMappings() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const HmlBackMappingPreset& activePreset = getActiveHmlBackPreset(addonOptions);
    mapTable_.setCount(HML_BACK_KEY_COUNT);
    for (uint8_t i = 0; i < HML_BACK_KEY_COUNT; i++) {
        ActionMappingCommon::ActionMappingEntry* entry = mapTable_.at(i);
        if (entry != nullptr) {
            ActionMappingCommon::parseActionMapping(activePreset.*(kHmlBackKeyDefs[i].presetField), *entry);
        }
    }
}

void HmlBackKeyAddon::setup() {
    for (uint8_t i = 0; i < HML_BACK_KEY_COUNT; i++) {
        gpio_init(kHmlBackKeyDefs[i].pin);
        gpio_set_dir(kHmlBackKeyDefs[i].pin, GPIO_IN);
        gpio_pull_up(kHmlBackKeyDefs[i].pin);
    }

    buildMappings();
    outputScope_.reset();
    for (uint8_t i = 0; i < HML_BACK_KEY_COUNT; i++) {
        ActionMappingCommon::resetDebounceBool(debounce_[i], false);
    }
}

void HmlBackKeyAddon::reinit() {
    buildMappings();
    outputScope_.reset();
    for (uint8_t i = 0; i < HML_BACK_KEY_COUNT; i++) {
        ActionMappingCommon::resetDebounceBool(debounce_[i], false);
    }
}

void HmlBackKeyAddon::preprocess() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    for (uint8_t i = 0; i < HML_BACK_KEY_COUNT; i++) {
        const bool pressed = !gpio_get(kHmlBackKeyDefs[i].pin);
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
