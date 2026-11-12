#include "addons/unified_joystick_travel_key.h"

#include "addons/unified_analog_processor.h"
#include "gamepad.h"
#include "storagemanager.h"

#include <algorithm>

namespace {
static constexpr uint8_t TRAVEL_KEY_DEBOUNCE_FRAMES = 2u;
}

bool UnifiedJoystickTravelKeyAddon::available() {
    return isActive();
}

bool UnifiedJoystickTravelKeyAddon::isActive() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;

    const bool leftEnabled = (analogOptions.joystick_travel_button_threshold > 0u) &&
        (analogOptions.joystick_travel_button_action != GpioAction::NONE);
    const bool rightEnabled = (analogOptions.joystick_travel_button_threshold2 > 0u) &&
        (analogOptions.joystick_travel_button_action2 != GpioAction::NONE);
    return leftEnabled || rightEnabled;
}

void UnifiedJoystickTravelKeyAddon::setup() {
    buildMapsAndThresholds();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
    ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
}

void UnifiedJoystickTravelKeyAddon::reinit() {
    buildMapsAndThresholds();
    outputScope_.reset();
    ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
    ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
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

    // Threshold percent maps directly onto the unified processor's normalized
    // travel, so 99% means near-full calibrated travel and stays reachable even
    // when the pot's electrical center/max are far from 2047/4095.
    const auto toNormalized = [](uint32_t percent) {
        return std::clamp(static_cast<float>(percent) / 100.0f, 0.0f, 1.0f);
    };

    const ActionMappingCommon::ActionMappingEntry* leftEntry = entries_.at(0);
    const ActionMappingCommon::ActionMappingEntry* rightEntry = entries_.at(1);
    thresholds_[0] = (leftEntry && leftEntry->enabled)
        ? toNormalized(analogOptions.joystick_travel_button_threshold)
        : 0.0f;
    thresholds_[1] = (rightEntry && rightEntry->enabled)
        ? toNormalized(analogOptions.joystick_travel_button_threshold2)
        : 0.0f;
}

void __not_in_flash_func(UnifiedJoystickTravelKeyAddon::process)() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    outputScope_.beginFrame(gamepad);

    if (!available()) {
        outputScope_.endFrame();
        ActionMappingCommon::resetDebounceBool(debounceState_[0], false);
        ActionMappingCommon::resetDebounceBool(debounceState_[1], false);
        return;
    }

    bool candidateActive[2] = {false, false};

    for (uint8_t stick = 0; stick < 2; stick++) {
        const ActionMappingCommon::ActionMappingEntry* entry = entries_.at(stick);
        if (entry == nullptr || !entry->enabled || thresholds_[stick] <= 0.0f) {
            continue;
        }

        float nx = 0.0f;
        float ny = 0.0f;
        if (!UnifiedAnalogProcessorAddon::getNormalizedStick(stick, nx, ny)) {
            continue;
        }

        // Radial travel normalized to 1.0 = calibrated full travel.
        const float travelSquared = nx * nx + ny * ny;
        candidateActive[stick] = travelSquared > thresholds_[stick] * thresholds_[stick];
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
