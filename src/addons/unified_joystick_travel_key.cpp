#include "addons/unified_joystick_travel_key.h"

#include "addons/ads8332_adc.h"
#include "addons/analog.h"
#include "addons/mcp3208_adc.h"
#include "config.pb.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "storagemanager.h"

namespace {
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE = 131u;
static constexpr uint8_t KEYBOARD_KEY_INVALID_BIT = 0xFFu;
static constexpr uint8_t MOUSE_LEFT_BIT = (1u << 0);
static constexpr uint8_t MOUSE_RIGHT_BIT = (1u << 1);
static constexpr uint8_t MOUSE_MIDDLE_BIT = (1u << 2);
}

bool UnifiedJoystickTravelKeyAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const AnalogOptions& analogOptions = addonOptions.analogOptions;

    const bool hasSamplingSource = addonOptions.ads8332Options.enabled || addonOptions.mcp3208Options.enabled || analogOptions.enabled;
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
}

void UnifiedJoystickTravelKeyAddon::reinit() {
    resolveSource();
    buildMapsAndThresholds();
    lastButtons_[0] = lastButtons_[1] = 0;
    lastDpad_[0] = lastDpad_[1] = 0;
    lastAux_[0] = lastAux_[1] = 0;
    lastKeyboard_[0] = lastKeyboard_[1] = 0;
    lastMouse_[0] = lastMouse_[1] = 0;
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

void UnifiedJoystickTravelKeyAddon::buildEntryFromMapping(TravelMappingEntry& entry, GpioAction action, uint32_t customButtonMask, uint32_t customDpadMask) {
    entry.buttonMask = 0;
    entry.dpadMask = 0;
    entry.auxMask = 0;
    entry.keyboardKeyBit = KEYBOARD_KEY_INVALID_BIT;
    entry.mouseButtonMask = 0;
    entry.enabled = false;

    if (action == GpioAction::NONE) {
        return;
    }

    if (action == GpioAction::CUSTOM_BUTTON_COMBO) {
        entry.buttonMask = customButtonMask;
        if (customDpadMask & GAMEPAD_MASK_UP) entry.dpadMask |= GAMEPAD_MASK_UP;
        if (customDpadMask & GAMEPAD_MASK_DOWN) entry.dpadMask |= GAMEPAD_MASK_DOWN;
        if (customDpadMask & GAMEPAD_MASK_LEFT) entry.dpadMask |= GAMEPAD_MASK_LEFT;
        if (customDpadMask & GAMEPAD_MASK_RIGHT) entry.dpadMask |= GAMEPAD_MASK_RIGHT;
        entry.enabled = (entry.buttonMask != 0u) || (entry.dpadMask != 0u);
        return;
    }

    switch (action) {
        case GpioAction::BUTTON_PRESS_UP: entry.dpadMask |= GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN: entry.dpadMask |= GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT: entry.dpadMask |= GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: entry.dpadMask |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1: entry.buttonMask |= GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2: entry.buttonMask |= GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3: entry.buttonMask |= GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4: entry.buttonMask |= GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1: entry.buttonMask |= GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1: entry.buttonMask |= GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2: entry.buttonMask |= GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2: entry.buttonMask |= GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1: entry.buttonMask |= GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2: entry.buttonMask |= GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3: entry.buttonMask |= GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3: entry.buttonMask |= GAMEPAD_MASK_R3; break;
        case GpioAction::BUTTON_PRESS_A1: entry.buttonMask |= GAMEPAD_MASK_A1; break;
        case GpioAction::BUTTON_PRESS_A2: entry.buttonMask |= GAMEPAD_MASK_A2; break;
        case GpioAction::BUTTON_PRESS_A3: entry.buttonMask |= GAMEPAD_MASK_A3; break;
        case GpioAction::BUTTON_PRESS_A4: entry.buttonMask |= GAMEPAD_MASK_A4; break;
        case GpioAction::BUTTON_PRESS_E1: entry.buttonMask |= GAMEPAD_MASK_E1; break;
        case GpioAction::BUTTON_PRESS_E2: entry.buttonMask |= GAMEPAD_MASK_E2; break;
        case GpioAction::BUTTON_PRESS_E3: entry.buttonMask |= GAMEPAD_MASK_E3; break;
        case GpioAction::BUTTON_PRESS_E4: entry.buttonMask |= GAMEPAD_MASK_E4; break;
        case GpioAction::BUTTON_PRESS_E5: entry.buttonMask |= GAMEPAD_MASK_E5; break;
        case GpioAction::BUTTON_PRESS_E6: entry.buttonMask |= GAMEPAD_MASK_E6; break;
        case GpioAction::BUTTON_PRESS_E7: entry.buttonMask |= GAMEPAD_MASK_E7; break;
        case GpioAction::BUTTON_PRESS_E8: entry.buttonMask |= GAMEPAD_MASK_E8; break;
        case GpioAction::BUTTON_PRESS_E9: entry.buttonMask |= GAMEPAD_MASK_E9; break;
        case GpioAction::BUTTON_PRESS_E10: entry.buttonMask |= GAMEPAD_MASK_E10; break;
        case GpioAction::BUTTON_PRESS_E11: entry.buttonMask |= GAMEPAD_MASK_E11; break;
        case GpioAction::BUTTON_PRESS_E12: entry.buttonMask |= GAMEPAD_MASK_E12; break;
        case GpioAction::BUTTON_PRESS_FN: entry.auxMask |= AUX_MASK_FUNCTION; break;
        default:
            break;
    }

    if (action >= GpioAction::KEYBOARD_KEY_A && action <= GpioAction::KEYBOARD_KEY_9) {
        entry.keyboardKeyBit = static_cast<uint8_t>(static_cast<uint32_t>(action) - KEYBOARD_KEY_ACTION_BASE);
    } else if (action == GpioAction::MOUSE_LEFT_BUTTON) {
        entry.mouseButtonMask = MOUSE_LEFT_BIT;
    } else if (action == GpioAction::MOUSE_RIGHT_BUTTON) {
        entry.mouseButtonMask = MOUSE_RIGHT_BIT;
    } else if (action == GpioAction::MOUSE_MIDDLE_BUTTON) {
        entry.mouseButtonMask = MOUSE_MIDDLE_BIT;
    }

    entry.enabled = !isEntryEffectivelyEmpty(entry);
}

bool UnifiedJoystickTravelKeyAddon::isEntryEffectivelyEmpty(const TravelMappingEntry& entry) {
    return (entry.buttonMask == 0u) &&
        (entry.dpadMask == 0u) &&
        (entry.auxMask == 0u) &&
        (entry.keyboardKeyBit == KEYBOARD_KEY_INVALID_BIT) &&
        (entry.mouseButtonMask == 0u);
}

uint64_t UnifiedJoystickTravelKeyAddon::getThresholdSquared(uint16_t adcMax, uint32_t thresholdPercent) {
    if (adcMax == 0u || thresholdPercent == 0u) {
        return 0u;
    }
    const uint64_t threshold = (static_cast<uint64_t>(adcMax) * thresholdPercent) / 100u;
    return threshold * threshold;
}

void UnifiedJoystickTravelKeyAddon::buildMapsAndThresholds() {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

    buildEntryFromMapping(
        entries_[0],
        analogOptions.joystick_travel_button_action,
        analogOptions.joystick_travel_button_custom_button_mask,
        analogOptions.joystick_travel_button_custom_dpad_mask
    );
    buildEntryFromMapping(
        entries_[1],
        analogOptions.joystick_travel_button_action2,
        analogOptions.joystick_travel_button_custom_button_mask2,
        analogOptions.joystick_travel_button_custom_dpad_mask2
    );

    uint16_t adcMax = 0;
    if (source_ == StickSource::ADS8332) {
        adcMax = 65535u;
    } else if (source_ == StickSource::MCP3208 || source_ == StickSource::OnboardADC) {
        adcMax = 4095u;
    }

    thresholdSquared_[0] = entries_[0].enabled
        ? getThresholdSquared(adcMax, analogOptions.joystick_travel_button_threshold)
        : 0u;
    thresholdSquared_[1] = entries_[1].enabled
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

void UnifiedJoystickTravelKeyAddon::clearLastOutputs(Gamepad* gamepad) {
    gamepad->state.buttons &= ~(lastButtons_[0] | lastButtons_[1]);
    gamepad->state.dpad &= ~(lastDpad_[0] | lastDpad_[1]);
    gamepad->state.aux &= static_cast<uint16_t>(~(lastAux_[0] | lastAux_[1]));
    gamepad->addonKeyboardKeyMask &= ~(lastKeyboard_[0] | lastKeyboard_[1]);
    gamepad->addonMouseButtonMask &= static_cast<uint8_t>(~(lastMouse_[0] | lastMouse_[1]));

    lastButtons_[0] = lastButtons_[1] = 0;
    lastDpad_[0] = lastDpad_[1] = 0;
    lastAux_[0] = lastAux_[1] = 0;
    lastKeyboard_[0] = lastKeyboard_[1] = 0;
    lastMouse_[0] = lastMouse_[1] = 0;
}

void UnifiedJoystickTravelKeyAddon::applyOutputs(Gamepad* gamepad, const uint32_t currentButtons[2], const uint32_t currentDpad[2], const uint16_t currentAux[2], const uint64_t currentKeyboard[2], const uint8_t currentMouse[2]) {
    gamepad->state.buttons &= ~(lastButtons_[0] | lastButtons_[1]);
    gamepad->state.dpad &= ~(lastDpad_[0] | lastDpad_[1]);
    gamepad->state.aux &= static_cast<uint16_t>(~(lastAux_[0] | lastAux_[1]));
    gamepad->addonKeyboardKeyMask &= ~(lastKeyboard_[0] | lastKeyboard_[1]);
    gamepad->addonMouseButtonMask &= static_cast<uint8_t>(~(lastMouse_[0] | lastMouse_[1]));

    const uint32_t nextButtons = currentButtons[0] | currentButtons[1];
    const uint32_t nextDpad = currentDpad[0] | currentDpad[1];
    const uint16_t nextAux = currentAux[0] | currentAux[1];
    const uint64_t nextKeyboard = currentKeyboard[0] | currentKeyboard[1];
    const uint8_t nextMouse = currentMouse[0] | currentMouse[1];

    gamepad->state.buttons |= nextButtons;
    gamepad->state.dpad |= nextDpad;
    gamepad->state.aux |= nextAux;
    gamepad->addonKeyboardKeyMask |= nextKeyboard;
    gamepad->addonMouseButtonMask |= nextMouse;

    lastButtons_[0] = currentButtons[0];
    lastButtons_[1] = currentButtons[1];
    lastDpad_[0] = currentDpad[0];
    lastDpad_[1] = currentDpad[1];
    lastAux_[0] = currentAux[0];
    lastAux_[1] = currentAux[1];
    lastKeyboard_[0] = currentKeyboard[0];
    lastKeyboard_[1] = currentKeyboard[1];
    lastMouse_[0] = currentMouse[0];
    lastMouse_[1] = currentMouse[1];
}

void UnifiedJoystickTravelKeyAddon::process() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }

    if (!available() || source_ == StickSource::None) {
        clearLastOutputs(gamepad);
        return;
    }

    uint32_t currentButtons[2] = {0, 0};
    uint32_t currentDpad[2] = {0, 0};
    uint16_t currentAux[2] = {0, 0};
    uint64_t currentKeyboard[2] = {0, 0};
    uint8_t currentMouse[2] = {0, 0};

    for (uint8_t stick = 0; stick < 2; stick++) {
        if (!entries_[stick].enabled || thresholdSquared_[stick] == 0u) {
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
        if (distSquared <= thresholdSquared_[stick]) {
            continue;
        }

        const TravelMappingEntry& entry = entries_[stick];
        currentButtons[stick] = entry.buttonMask;
        currentDpad[stick] = entry.dpadMask;
        currentAux[stick] = entry.auxMask;
        if (entry.keyboardKeyBit != KEYBOARD_KEY_INVALID_BIT) {
            currentKeyboard[stick] = (1ULL << entry.keyboardKeyBit);
        }
        currentMouse[stick] = entry.mouseButtonMask;
    }

    applyOutputs(gamepad, currentButtons, currentDpad, currentAux, currentKeyboard, currentMouse);
}
