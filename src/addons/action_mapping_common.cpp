#include "addons/action_mapping_common.h"

#include "eventmanager.h"
#include "events/GPMenuNavigateEvent.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"

namespace ActionMappingCommon {
namespace {
static constexpr uint32_t KEYBOARD_KEY_ACTION_BASE = 131u;
static constexpr uint8_t MOUSE_LEFT_BIT = (1u << 0);
static constexpr uint8_t MOUSE_RIGHT_BIT = (1u << 1);
static constexpr uint8_t MOUSE_MIDDLE_BIT = (1u << 2);

static bool isComplexAction(GpioAction action) {
    switch (action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_X_POS:
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS:
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_X_POS:
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS:
        case GpioAction::MENU_NAVIGATION_UP:
        case GpioAction::MENU_NAVIGATION_DOWN:
        case GpioAction::MENU_NAVIGATION_LEFT:
        case GpioAction::MENU_NAVIGATION_RIGHT:
        case GpioAction::MENU_NAVIGATION_SELECT:
        case GpioAction::MENU_NAVIGATION_BACK:
        case GpioAction::MENU_NAVIGATION_TOGGLE:
        case GpioAction::BUTTON_PRESS_TURBO:
        case GpioAction::BUTTON_PRESS_MACRO:
        case GpioAction::BUTTON_PRESS_MACRO_1:
        case GpioAction::BUTTON_PRESS_MACRO_2:
        case GpioAction::BUTTON_PRESS_MACRO_3:
        case GpioAction::BUTTON_PRESS_MACRO_4:
        case GpioAction::BUTTON_PRESS_MACRO_5:
        case GpioAction::BUTTON_PRESS_MACRO_6:
            return true;
        default:
            return false;
    }
}

static void applyComplexAction(Gamepad* gamepad, GpioAction action) {
    switch (action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG: gamepad->state.lx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_X_POS: gamepad->state.lx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG: gamepad->state.ly = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS: gamepad->state.ly = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG: gamepad->state.rx = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_X_POS: gamepad->state.rx = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG: gamepad->state.ry = GAMEPAD_JOYSTICK_MIN; break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS: gamepad->state.ry = GAMEPAD_JOYSTICK_MAX; break;
        case GpioAction::MENU_NAVIGATION_UP:
        case GpioAction::MENU_NAVIGATION_DOWN:
        case GpioAction::MENU_NAVIGATION_LEFT:
        case GpioAction::MENU_NAVIGATION_RIGHT:
        case GpioAction::MENU_NAVIGATION_SELECT:
        case GpioAction::MENU_NAVIGATION_BACK:
        case GpioAction::MENU_NAVIGATION_TOGGLE:
            EventManager::getInstance().triggerEvent(new GPMenuNavigateEvent(action));
            break;
        default:
            // TURBO / MACRO actions are accepted by the shared parser but are currently
            // not represented by GamepadState bitmasks, so no-op here.
            break;
    }
}

static void clearComplexAction(Gamepad* gamepad, GpioAction action) {
    switch (action) {
        case GpioAction::ANALOG_DIRECTION_LS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_X_POS:
            gamepad->state.lx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_LS_Y_POS:
            gamepad->state.ly = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_X_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_X_POS:
            gamepad->state.rx = GAMEPAD_JOYSTICK_MID;
            break;
        case GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
        case GpioAction::ANALOG_DIRECTION_RS_Y_POS:
            gamepad->state.ry = GAMEPAD_JOYSTICK_MID;
            break;
        default:
            break;
    }
}
} // namespace

void ActionMappingTable::clear() {
    count = 0;
    for (uint8_t i = 0; i < ACTION_MAPPING_TABLE_MAX_ENTRIES; i++) {
        entries[i] = ActionMappingEntry{};
    }
}

void ActionMappingTable::setCount(uint8_t newCount) {
    count = (newCount > ACTION_MAPPING_TABLE_MAX_ENTRIES) ? ACTION_MAPPING_TABLE_MAX_ENTRIES : newCount;
    for (uint8_t i = count; i < ACTION_MAPPING_TABLE_MAX_ENTRIES; i++) {
        entries[i] = ActionMappingEntry{};
    }
}

ActionMappingEntry* ActionMappingTable::at(uint8_t index) {
    return (index < count) ? &entries[index] : nullptr;
}

const ActionMappingEntry* ActionMappingTable::at(uint8_t index) const {
    return (index < count) ? &entries[index] : nullptr;
}

void resetDebounceBool(DebounceBoolState& state, bool stableValue) {
    state.stable = stableValue;
    state.pending = stableValue;
    state.count = 0;
}

void updateDebounceBool(bool candidate, DebounceBoolState& state, uint8_t debounceFrames) {
    if (candidate == state.stable) {
        state.count = 0;
        return;
    }
    if (candidate == state.pending) {
        state.count++;
        if (state.count >= debounceFrames) {
            state.stable = state.pending;
            state.count = 0;
        }
    } else {
        state.pending = candidate;
        state.count = 1;
    }
}

void resetDebounceLevel(DebounceLevelState& state, int8_t stableValue) {
    state.stable = stableValue;
    state.pending = stableValue;
    state.count = 0;
}

void updateDebounceLevel(int8_t candidate, DebounceLevelState& state, uint8_t debounceFrames, bool immediateReleaseToNone) {
    if (candidate == state.stable) {
        state.count = 0;
        return;
    }
    if (candidate == state.pending) {
        if (immediateReleaseToNone && candidate == -1) {
            state.stable = -1;
            state.count = 0;
            return;
        }
        state.count++;
        if (state.count >= debounceFrames) {
            state.stable = state.pending;
            state.count = 0;
        }
    } else {
        state.pending = candidate;
        state.count = 1;
    }
}

void parseActionMapping(const GpioMappingInfo& mapping, ActionMappingEntry& entry) {
    parseActionMapping(mapping.action, mapping.customButtonMask, mapping.customDpadMask, entry);
}

void parseActionMapping(GpioAction action, uint32_t customButtonMask, uint32_t customDpadMask, ActionMappingEntry& entry) {
    entry = ActionMappingEntry{};

    if (action == GpioAction::NONE || action == GpioAction::RESERVED || action == GpioAction::ASSIGNED_TO_ADDON) {
        return;
    }

    if (action == GpioAction::CUSTOM_BUTTON_COMBO) {
        entry.buttonMask = customButtonMask;
        entry.dpadMask = static_cast<uint8_t>(customDpadMask & GAMEPAD_MASK_DPAD);
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
        const uint8_t keyboardBit = static_cast<uint8_t>(static_cast<uint32_t>(action) - KEYBOARD_KEY_ACTION_BASE);
        entry.keyboardMask = (1ULL << keyboardBit);
    } else if (action == GpioAction::MOUSE_LEFT_BUTTON) {
        entry.mouseButtonMask = MOUSE_LEFT_BIT;
    } else if (action == GpioAction::MOUSE_RIGHT_BUTTON) {
        entry.mouseButtonMask = MOUSE_RIGHT_BIT;
    } else if (action == GpioAction::MOUSE_MIDDLE_BUTTON) {
        entry.mouseButtonMask = MOUSE_MIDDLE_BIT;
    }

    if (isComplexAction(action)) {
        entry.complexAction = action;
    }

    entry.enabled =
        (entry.buttonMask != 0u) ||
        (entry.dpadMask != 0u) ||
        (entry.auxMask != 0u) ||
        (entry.keyboardMask != 0u) ||
        (entry.mouseButtonMask != 0u) ||
        (entry.complexAction != GpioAction::NONE);
}

void applyActionMappingEntry(Gamepad* gamepad, const ActionMappingEntry& entry) {
    if (gamepad == nullptr || !entry.enabled) {
        return;
    }

    gamepad->state.buttons |= entry.buttonMask;
    gamepad->state.dpad |= entry.dpadMask;
    gamepad->state.aux |= entry.auxMask;
    gamepad->addonKeyboardKeyMask |= entry.keyboardMask;
    gamepad->addonMouseButtonMask |= entry.mouseButtonMask;

    if (entry.buttonMask & GAMEPAD_MASK_L2) {
        gamepad->state.lt = GAMEPAD_TRIGGER_MAX;
    }
    if (entry.buttonMask & GAMEPAD_MASK_R2) {
        gamepad->state.rt = GAMEPAD_TRIGGER_MAX;
    }

    if (entry.complexAction != GpioAction::NONE) {
        applyComplexAction(gamepad, entry.complexAction);
    }
}

void clearActionMappingEntry(Gamepad* gamepad, const ActionMappingEntry& entry) {
    if (gamepad == nullptr || !entry.enabled) {
        return;
    }

    gamepad->state.buttons &= ~entry.buttonMask;
    gamepad->state.dpad &= static_cast<uint8_t>(~entry.dpadMask);
    gamepad->state.aux &= static_cast<uint16_t>(~entry.auxMask);
    gamepad->addonKeyboardKeyMask &= ~entry.keyboardMask;
    gamepad->addonMouseButtonMask &= static_cast<uint8_t>(~entry.mouseButtonMask);

    if (entry.complexAction != GpioAction::NONE) {
        clearComplexAction(gamepad, entry.complexAction);
    }
}

void ActionOutputScope::reset() {
    lastButtons_ = 0;
    lastDpad_ = 0;
    lastAux_ = 0;
    lastKeyboard_ = 0;
    lastMouse_ = 0;
    lastComplexCount_ = 0;
    frameButtons_ = 0;
    frameDpad_ = 0;
    frameAux_ = 0;
    frameKeyboard_ = 0;
    frameMouse_ = 0;
    frameComplexCount_ = 0;
}

void ActionOutputScope::beginFrame(Gamepad* gamepad) {
    if (gamepad == nullptr) {
        return;
    }

    gamepad->state.buttons &= ~lastButtons_;
    gamepad->state.dpad &= static_cast<uint8_t>(~lastDpad_);
    gamepad->state.aux &= static_cast<uint16_t>(~lastAux_);
    gamepad->addonKeyboardKeyMask &= ~lastKeyboard_;
    gamepad->addonMouseButtonMask &= static_cast<uint8_t>(~lastMouse_);

    for (uint8_t i = 0; i < lastComplexCount_; i++) {
        clearComplexAction(gamepad, lastComplex_[i]);
    }

    frameButtons_ = 0;
    frameDpad_ = 0;
    frameAux_ = 0;
    frameKeyboard_ = 0;
    frameMouse_ = 0;
    frameComplexCount_ = 0;
}

void ActionOutputScope::apply(Gamepad* gamepad, const ActionMappingEntry& entry) {
    if (gamepad == nullptr || !entry.enabled) {
        return;
    }

    applyActionMappingEntry(gamepad, entry);
    frameButtons_ |= entry.buttonMask;
    frameDpad_ |= entry.dpadMask;
    frameAux_ |= entry.auxMask;
    frameKeyboard_ |= entry.keyboardMask;
    frameMouse_ |= entry.mouseButtonMask;

    if (entry.complexAction != GpioAction::NONE && frameComplexCount_ < ACTION_OUTPUT_SCOPE_MAX_COMPLEX_ACTIONS) {
        bool exists = false;
        for (uint8_t i = 0; i < frameComplexCount_; i++) {
            if (frameComplex_[i] == entry.complexAction) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            frameComplex_[frameComplexCount_++] = entry.complexAction;
        }
    }
}

void ActionOutputScope::endFrame() {
    lastButtons_ = frameButtons_;
    lastDpad_ = frameDpad_;
    lastAux_ = frameAux_;
    lastKeyboard_ = frameKeyboard_;
    lastMouse_ = frameMouse_;
    lastComplexCount_ = frameComplexCount_;
    for (uint8_t i = 0; i < frameComplexCount_; i++) {
        lastComplex_[i] = frameComplex_[i];
    }
}

} // namespace ActionMappingCommon
