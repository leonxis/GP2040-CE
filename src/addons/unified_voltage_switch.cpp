#include "addons/unified_voltage_switch.h"

#include "addons/ads8332_adc.h"
#include "config.pb.h"
#include "gamepad.h"
#include "storagemanager.h"

namespace {
static constexpr uint8_t DEBOUNCE_FRAMES = 2;
static constexpr float CH25_T1_RATIO = 496.0f / 4095.0f;
static constexpr float CH25_T2_RATIO = 1488.0f / 4095.0f;
static constexpr float CH25_T3_RATIO = 2482.0f / 4095.0f;
static constexpr float CH25_T4_RATIO = 3596.0f / 4095.0f;
}

bool UnifiedVoltageSwitchAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    return addonOptions.ads8332Options.enabled;
}

void UnifiedVoltageSwitchAddon::setup() {
    buildMaps();
    outputScope_.reset();
}

void UnifiedVoltageSwitchAddon::reinit() {
    buildMaps();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceLevel(leftDebounce_, -1);
    ActionMappingCommon::resetDebounceLevel(rightDebounce_, -1);
}

uint16_t UnifiedVoltageSwitchAddon::scaledThreshold(float ratio, uint16_t adcMax) {
    const float scaled = ratio * static_cast<float>(adcMax);
    if (scaled < 0.0f) {
        return 0;
    }
    if (scaled > static_cast<float>(adcMax)) {
        return adcMax;
    }
    return static_cast<uint16_t>(scaled);
}

void UnifiedVoltageSwitchAddon::buildMaps() {
    const FnKeyMappingOptions& fn = Storage::getInstance().getAddonOptions().fnKeyMappingOptions;
    left_map_.setCount(4);
    right_map_.setCount(4);

    if (ActionMappingCommon::ActionMappingEntry* entry = left_map_.at(0)) {
        ActionMappingCommon::parseActionMapping(fn.leftMtMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = left_map_.at(1)) {
        ActionMappingCommon::parseActionMapping(GpioAction::BUTTON_PRESS_L3, 0, 0, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = left_map_.at(2)) {
        ActionMappingCommon::parseActionMapping(fn.leftExtTriggerMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = left_map_.at(3)) {
        ActionMappingCommon::parseActionMapping(fn.leftFnMapping, *entry);
    }

    if (ActionMappingCommon::ActionMappingEntry* entry = right_map_.at(0)) {
        ActionMappingCommon::parseActionMapping(fn.rightMtMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = right_map_.at(1)) {
        ActionMappingCommon::parseActionMapping(GpioAction::BUTTON_PRESS_R3, 0, 0, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = right_map_.at(2)) {
        ActionMappingCommon::parseActionMapping(fn.rightExtTriggerMapping, *entry);
    }
    if (ActionMappingCommon::ActionMappingEntry* entry = right_map_.at(3)) {
        ActionMappingCommon::parseActionMapping(fn.rightFnMapping, *entry);
    }
}

void UnifiedVoltageSwitchAddon::process() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    if (!available()) {
        outputScope_.endFrame();
        ActionMappingCommon::resetDebounceLevel(leftDebounce_, -1);
        ActionMappingCommon::resetDebounceLevel(rightDebounce_, -1);
        return;
    }

    uint16_t left = 0, right = 0, adcMax = 0;
    bool leftValid = false, rightValid = false;
    const bool hasSource = ADS8332ADCAddon::getRawDividerForProcessor(left, right, adcMax, leftValid, rightValid);
    if (!hasSource || adcMax == 0) {
        outputScope_.endFrame();
        ActionMappingCommon::resetDebounceLevel(leftDebounce_, -1);
        ActionMappingCommon::resetDebounceLevel(rightDebounce_, -1);
        return;
    }

    const uint16_t th1 = scaledThreshold(CH25_T1_RATIO, adcMax);
    const uint16_t th2 = scaledThreshold(CH25_T2_RATIO, adcMax);
    const uint16_t th3 = scaledThreshold(CH25_T3_RATIO, adcMax);
    const uint16_t th4 = scaledThreshold(CH25_T4_RATIO, adcMax);

    auto detectLevel = [](uint16_t value, uint16_t t1, uint16_t t2, uint16_t t3, uint16_t t4) -> int8_t {
        if (value < t1) return 0;
        if (value < t2) return 1;
        if (value < t3) return 2;
        if (value < t4) return 3;
        return -1;
    };

    const int8_t leftCandidate = leftValid ? detectLevel(left, th1, th2, th3, th4) : -1;
    const int8_t rightCandidate = rightValid ? detectLevel(right, th1, th2, th3, th4) : -1;
    ActionMappingCommon::updateDebounceLevel(leftCandidate, leftDebounce_, DEBOUNCE_FRAMES, false);
    ActionMappingCommon::updateDebounceLevel(rightCandidate, rightDebounce_, DEBOUNCE_FRAMES, false);

    if (leftDebounce_.stable >= 0) {
        const ActionMappingCommon::ActionMappingEntry* entry = left_map_.at(static_cast<uint8_t>(leftDebounce_.stable));
        if (entry != nullptr) {
            outputScope_.apply(gamepad, *entry);
        }
    }
    if (rightDebounce_.stable >= 0) {
        const ActionMappingCommon::ActionMappingEntry* entry = right_map_.at(static_cast<uint8_t>(rightDebounce_.stable));
        if (entry != nullptr) {
            outputScope_.apply(gamepad, *entry);
        }
    }

    outputScope_.endFrame();
}
