#include "addons/unified_voltage_switch.h"

#include "addons/ads8332_adc.h"
#include "config.pb.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "storagemanager.h"

namespace {
static constexpr uint8_t DEBOUNCE_FRAMES = 2;
static constexpr float CH25_T1_RATIO = 496.0f / 4095.0f;
static constexpr float CH25_T2_RATIO = 1488.0f / 4095.0f;
static constexpr float CH25_T3_RATIO = 2482.0f / 4095.0f;
static constexpr float CH25_T4_RATIO = 3596.0f / 4095.0f;
static constexpr uint32_t CH25_KEYBOARD_KEY_BASE = 131u;
static constexpr uint8_t MOUSE_LEFT_BIT = (1u << 0);
static constexpr uint8_t MOUSE_RIGHT_BIT = (1u << 1);
static constexpr uint8_t MOUSE_MIDDLE_BIT = (1u << 2);
} // namespace

bool UnifiedVoltageSwitchAddon::available() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    return addonOptions.ads8332Options.enabled;
}

void UnifiedVoltageSwitchAddon::setup() {
    buildMaps();
}

void UnifiedVoltageSwitchAddon::reinit() {
    buildMaps();
    left_stable_level_ = left_pending_level_ = right_stable_level_ = right_pending_level_ = -1;
    left_debounce_count_ = right_debounce_count_ = 0;
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

void UnifiedVoltageSwitchAddon::gpioMappingToMasks(const GpioMappingInfo& mapping, uint32_t* outButtons, uint32_t* outDpad) {
    *outButtons = 0;
    *outDpad = 0;
    if (mapping.action == GpioAction::NONE) return;
    if (mapping.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        *outButtons = mapping.customButtonMask;
        if (mapping.customDpadMask & GAMEPAD_MASK_UP) *outDpad |= GAMEPAD_MASK_UP;
        if (mapping.customDpadMask & GAMEPAD_MASK_DOWN) *outDpad |= GAMEPAD_MASK_DOWN;
        if (mapping.customDpadMask & GAMEPAD_MASK_LEFT) *outDpad |= GAMEPAD_MASK_LEFT;
        if (mapping.customDpadMask & GAMEPAD_MASK_RIGHT) *outDpad |= GAMEPAD_MASK_RIGHT;
        return;
    }
    switch (mapping.action) {
        case GpioAction::BUTTON_PRESS_UP: *outDpad |= GAMEPAD_MASK_UP; break;
        case GpioAction::BUTTON_PRESS_DOWN: *outDpad |= GAMEPAD_MASK_DOWN; break;
        case GpioAction::BUTTON_PRESS_LEFT: *outDpad |= GAMEPAD_MASK_LEFT; break;
        case GpioAction::BUTTON_PRESS_RIGHT: *outDpad |= GAMEPAD_MASK_RIGHT; break;
        case GpioAction::BUTTON_PRESS_B1: *outButtons |= GAMEPAD_MASK_B1; break;
        case GpioAction::BUTTON_PRESS_B2: *outButtons |= GAMEPAD_MASK_B2; break;
        case GpioAction::BUTTON_PRESS_B3: *outButtons |= GAMEPAD_MASK_B3; break;
        case GpioAction::BUTTON_PRESS_B4: *outButtons |= GAMEPAD_MASK_B4; break;
        case GpioAction::BUTTON_PRESS_L1: *outButtons |= GAMEPAD_MASK_L1; break;
        case GpioAction::BUTTON_PRESS_R1: *outButtons |= GAMEPAD_MASK_R1; break;
        case GpioAction::BUTTON_PRESS_L2: *outButtons |= GAMEPAD_MASK_L2; break;
        case GpioAction::BUTTON_PRESS_R2: *outButtons |= GAMEPAD_MASK_R2; break;
        case GpioAction::BUTTON_PRESS_S1: *outButtons |= GAMEPAD_MASK_S1; break;
        case GpioAction::BUTTON_PRESS_S2: *outButtons |= GAMEPAD_MASK_S2; break;
        case GpioAction::BUTTON_PRESS_L3: *outButtons |= GAMEPAD_MASK_L3; break;
        case GpioAction::BUTTON_PRESS_R3: *outButtons |= GAMEPAD_MASK_R3; break;
        default: break;
    }
}

void UnifiedVoltageSwitchAddon::fillEntryFromMapping(VoltageSwitchEntry& entry, const GpioMappingInfo& mapping, uint32_t buttonMask, uint32_t dpadMask) {
    entry.buttonMask = buttonMask;
    entry.dpadMask = dpadMask;
    entry.keyboardKeyBit = 0xFF;
    entry.mouseButtonMask = 0;
    if (mapping.action >= GpioAction::KEYBOARD_KEY_A && mapping.action <= GpioAction::KEYBOARD_KEY_9) {
        entry.keyboardKeyBit = static_cast<uint8_t>(static_cast<uint32_t>(mapping.action) - CH25_KEYBOARD_KEY_BASE);
    } else if (mapping.action == GpioAction::MOUSE_LEFT_BUTTON) {
        entry.mouseButtonMask = MOUSE_LEFT_BIT;
    } else if (mapping.action == GpioAction::MOUSE_RIGHT_BUTTON) {
        entry.mouseButtonMask = MOUSE_RIGHT_BIT;
    } else if (mapping.action == GpioAction::MOUSE_MIDDLE_BUTTON) {
        entry.mouseButtonMask = MOUSE_MIDDLE_BIT;
    }
}

void UnifiedVoltageSwitchAddon::buildMaps() {
    const FnKeyMappingOptions& fn = Storage::getInstance().getAddonOptions().fnKeyMappingOptions;
    uint32_t b = 0, d = 0;

    gpioMappingToMasks(fn.leftMtMapping, &b, &d);
    fillEntryFromMapping(left_map_[0], fn.leftMtMapping, b, d);
    left_map_[1].buttonMask = GAMEPAD_MASK_L3;
    left_map_[1].dpadMask = 0;
    left_map_[1].keyboardKeyBit = 0xFF;
    left_map_[1].mouseButtonMask = 0;
    gpioMappingToMasks(fn.leftExtTriggerMapping, &b, &d);
    fillEntryFromMapping(left_map_[2], fn.leftExtTriggerMapping, b, d);
    gpioMappingToMasks(fn.leftFnMapping, &b, &d);
    fillEntryFromMapping(left_map_[3], fn.leftFnMapping, b, d);

    gpioMappingToMasks(fn.rightMtMapping, &b, &d);
    fillEntryFromMapping(right_map_[0], fn.rightMtMapping, b, d);
    right_map_[1].buttonMask = GAMEPAD_MASK_R3;
    right_map_[1].dpadMask = 0;
    right_map_[1].keyboardKeyBit = 0xFF;
    right_map_[1].mouseButtonMask = 0;
    gpioMappingToMasks(fn.rightExtTriggerMapping, &b, &d);
    fillEntryFromMapping(right_map_[2], fn.rightExtTriggerMapping, b, d);
    gpioMappingToMasks(fn.rightFnMapping, &b, &d);
    fillEntryFromMapping(right_map_[3], fn.rightFnMapping, b, d);
}

void UnifiedVoltageSwitchAddon::applyLevels(int leftLevel, int rightLevel, Gamepad* gamepad) {
    gamepad->state.buttons &= ~(last_left_buttons_ | last_right_buttons_);
    gamepad->state.dpad &= ~(last_left_dpad_ | last_right_dpad_);
    gamepad->addonKeyboardKeyMask &= ~(last_left_keyboard_ | last_right_keyboard_);
    gamepad->addonMouseButtonMask &= static_cast<uint8_t>(~(last_left_mouse_ | last_right_mouse_));

    uint32_t leftButtons = 0, leftDpad = 0, rightButtons = 0, rightDpad = 0;
    uint64_t leftKb = 0, rightKb = 0;
    uint8_t leftMouse = 0, rightMouse = 0;

    if (leftLevel >= 0) {
        const VoltageSwitchEntry& e = left_map_[leftLevel];
        leftButtons = e.buttonMask;
        leftDpad = e.dpadMask;
        if (e.keyboardKeyBit != 0xFF) leftKb = (1ULL << e.keyboardKeyBit);
        leftMouse = e.mouseButtonMask;
        gamepad->state.buttons |= leftButtons;
        gamepad->state.dpad |= leftDpad;
        gamepad->addonKeyboardKeyMask |= leftKb;
        gamepad->addonMouseButtonMask |= leftMouse;
    }
    if (rightLevel >= 0) {
        const VoltageSwitchEntry& e = right_map_[rightLevel];
        rightButtons = e.buttonMask;
        rightDpad = e.dpadMask;
        if (e.keyboardKeyBit != 0xFF) rightKb = (1ULL << e.keyboardKeyBit);
        rightMouse = e.mouseButtonMask;
        gamepad->state.buttons |= rightButtons;
        gamepad->state.dpad |= rightDpad;
        gamepad->addonKeyboardKeyMask |= rightKb;
        gamepad->addonMouseButtonMask |= rightMouse;
    }

    last_left_buttons_ = leftButtons;
    last_left_dpad_ = leftDpad;
    last_left_keyboard_ = leftKb;
    last_left_mouse_ = leftMouse;
    last_right_buttons_ = rightButtons;
    last_right_dpad_ = rightDpad;
    last_right_keyboard_ = rightKb;
    last_right_mouse_ = rightMouse;
}

void UnifiedVoltageSwitchAddon::process() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) {
        return;
    }
    if (!available()) {
        // Ensure previously applied voltage-switch outputs are released immediately.
        applyLevels(-1, -1, gamepad);
        return;
    }

    uint16_t left = 0, right = 0, adcMax = 0;
    bool leftValid = false, rightValid = false;
    const bool hasSource = ADS8332ADCAddon::getRawDividerForProcessor(left, right, adcMax, leftValid, rightValid);
    if (!hasSource || adcMax == 0) {
        // Source became unavailable; clear previously latched outputs.
        applyLevels(-1, -1, gamepad);
        left_stable_level_ = left_pending_level_ = right_stable_level_ = right_pending_level_ = -1;
        left_debounce_count_ = right_debounce_count_ = 0;
        return;
    }

    const uint16_t th1 = scaledThreshold(CH25_T1_RATIO, adcMax);
    const uint16_t th2 = scaledThreshold(CH25_T2_RATIO, adcMax);
    const uint16_t th3 = scaledThreshold(CH25_T3_RATIO, adcMax);
    const uint16_t th4 = scaledThreshold(CH25_T4_RATIO, adcMax);

    auto detectLevel = [](uint16_t value, uint16_t t1, uint16_t t2, uint16_t t3, uint16_t t4) -> int {
        if (value < t1) return 0;
        if (value < t2) return 1;
        if (value < t3) return 2;
        if (value < t4) return 3;
        return -1;
    };
    auto updateDebounce = [](int candidate, int8_t& stable, int8_t& pending, uint8_t& count) {
        if (candidate == stable) {
            count = 0;
            return;
        }
        if (candidate == pending) {
            count++;
            if (count >= DEBOUNCE_FRAMES) {
                stable = pending;
                count = 0;
            }
        } else {
            pending = static_cast<int8_t>(candidate);
            count = 1;
        }
    };

    const int leftCandidate = leftValid ? detectLevel(left, th1, th2, th3, th4) : -1;
    const int rightCandidate = rightValid ? detectLevel(right, th1, th2, th3, th4) : -1;
    updateDebounce(leftCandidate, left_stable_level_, left_pending_level_, left_debounce_count_);
    updateDebounce(rightCandidate, right_stable_level_, right_pending_level_, right_debounce_count_);
    applyLevels(left_stable_level_, right_stable_level_, gamepad);
}
