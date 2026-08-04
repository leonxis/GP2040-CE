#include "addons/unified_joystick_travel_key.h"

#include "addons/ads8332_adc.h"
#include "addons/analog.h"
#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "gamepad.h"
#include "storagemanager.h"

namespace {
static constexpr uint8_t TRAVEL_KEY_DEBOUNCE_FRAMES = 2u;
}

bool UnifiedJoystickTravelKeyAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;

    const bool hasSamplingSource =
        addonOptions.ads8332Options.enabled ||
        addonOptions.mcp3208Options.enabled ||
        analogOptions.enabled;
    if (!hasSamplingSource) {
        return false;
    }

    const bool leftEnabled = (analogOptions.joystick_travel_button_threshold > 0u) &&
        (analogOptions.joystick_travel_button_action != GpioAction::NONE);
    const bool rightEnabled = (analogOptions.joystick_travel_button_threshold2 > 0u) &&
        (analogOptions.joystick_travel_button_action2 != GpioAction::NONE);
    return leftEnabled || rightEnabled;
}

void UnifiedJoystickTravelKeyAddon::setup() {
    resolveSource();
    buildMapsAndThresholds();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
    ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
}

void UnifiedJoystickTravelKeyAddon::reinit() {
    resolveSource();
    buildMapsAndThresholds();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
    ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
}

void UnifiedJoystickTravelKeyAddon::resolveSource() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    if (addonOptions.ads8332Options.enabled) {
        source_ = StickSource::ADS8332;
        return;
    }
    if (addonOptions.mcp3208Options.enabled) {
        source_ = StickSource::MCP3208;
        return;
    }
    if (addonOptions.analogOptions.enabled) {
        source_ = StickSource::OnboardADC;
        return;
    }
    source_ = StickSource::None;
}

uint64_t UnifiedJoystickTravelKeyAddon::getThresholdSquared(uint16_t adcMax, uint32_t thresholdPercent) {
    if (adcMax == 0u || thresholdPercent == 0u) {
        return 0u;
    }
    // Distance is measured from stick center, so threshold percent should map to half-range.
    const uint64_t halfRange = static_cast<uint64_t>(adcMax) / 2u;
    const uint64_t threshold = (halfRange * thresholdPercent) / 100u;
    return threshold * threshold;
}

void UnifiedJoystickTravelKeyAddon::buildMapsAndThresholds() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    entries_.setCount(2);

    if (ActionMappingCommon::ActionMappingEntry* leftEntry = entries_.at(0)) {
        ActionMappingCommon::parseActionMapping(
            analogOptions.joystick_travel_button_action,
            analogOptions.joystick_travel_button_custom_button_mask,
            analogOptions.joystick_travel_button_custom_dpad_mask,
            *leftEntry
        );
    }
    if (ActionMappingCommon::ActionMappingEntry* rightEntry = entries_.at(1)) {
        ActionMappingCommon::parseActionMapping(
            analogOptions.joystick_travel_button_action2,
            analogOptions.joystick_travel_button_custom_button_mask2,
            analogOptions.joystick_travel_button_custom_dpad_mask2,
            *rightEntry
        );
    }

    uint16_t adcMax = 0;
    if (source_ == StickSource::ADS8332) {
        adcMax = 65535u;
    } else if (source_ == StickSource::MCP3208 ||
        source_ == StickSource::OnboardADC) {
        adcMax = 4095u;
    }

    const ActionMappingCommon::ActionMappingEntry* leftEntry = entries_.at(0);
    const ActionMappingCommon::ActionMappingEntry* rightEntry = entries_.at(1);
    thresholdSquared_[0] = (leftEntry && leftEntry->enabled)
        ? getThresholdSquared(adcMax, analogOptions.joystick_travel_button_threshold)
        : 0u;
    thresholdSquared_[1] = (rightEntry && rightEntry->enabled)
        ? getThresholdSquared(adcMax, analogOptions.joystick_travel_button_threshold2)
        : 0u;
}

bool UnifiedJoystickTravelKeyAddon::readRawStick(uint8_t stickNum, uint16_t& rawX, uint16_t& rawY, uint16_t& centerX, uint16_t& centerY, uint16_t& adcMax) {
    bool xValid = false;
    bool yValid = false;

    // Unified sampler contract:
    // stick0 => ANALOG_ADC_1_VRX/VRY, stick1 => ANALOG_ADC_2_VRX/VRY.
    switch (source_) {
        case StickSource::ADS8332:
            return ADS8332ADCAddon::getRawStickForProcessor(stickNum, rawX, rawY, centerX, centerY, xValid, yValid, adcMax) && xValid && yValid;
        case StickSource::MCP3208:
            return MCP3208ADCAddon::getRawStickForProcessor(stickNum, rawX, rawY, centerX, centerY, xValid, yValid, adcMax) && xValid && yValid;
        case StickSource::OnboardADC:
            return AnalogInput::getRawStickForProcessor(stickNum, rawX, rawY, centerX, centerY, xValid, yValid, adcMax) && xValid && yValid;
        case StickSource::None:
        default:
            return false;
    }
}

void UnifiedJoystickTravelKeyAddon::process() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    if (!available() || source_ == StickSource::None) {
        outputScope_.endFrame();
        ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
        ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
        return;
    }

    bool candidateActive[2] = {false, false};

    for (uint8_t stick = 0; stick < 2; stick++) {
        const ActionMappingCommon::ActionMappingEntry* entry = entries_.at(stick);
        if (entry == nullptr || !entry->enabled || thresholdSquared_[stick] == 0u) {
            continue;
        }

        uint16_t rawX = 0, rawY = 0, centerX = 0, centerY = 0, adcMax = 0;
        if (!readRawStick(stick, rawX, rawY, centerX, centerY, adcMax) || adcMax == 0u) {
            continue;
        }

        const int32_t dx = static_cast<int32_t>(rawX) - static_cast<int32_t>(centerX);
        const int32_t dy = static_cast<int32_t>(rawY) - static_cast<int32_t>(centerY);
        const int64_t dx64 = static_cast<int64_t>(dx);
        const int64_t dy64 = static_cast<int64_t>(dy);
        const uint64_t distSquared =
            static_cast<uint64_t>(dx64 * dx64) + static_cast<uint64_t>(dy64 * dy64);
        candidateActive[stick] = (distSquared > thresholdSquared_[stick]);
    }

    for (uint8_t stick = 0; stick < 2; stick++) {
        ActionMappingCommon::updateDebounceBool(candidateActive[stick], debounceState_[stick], TRAVEL_KEY_DEBOUNCE_FRAMES);
        if (!debounceState_[stick].stable) {
            continue;
        }
        const ActionMappingCommon::ActionMappingEntry* entry = entries_.at(stick);
        if (entry != nullptr) {
            outputScope_.apply(gamepad, *entry);
        }
    }

    outputScope_.endFrame();
}
