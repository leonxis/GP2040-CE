#include "addons/input_macro.h"
#include "storagemanager.h"
#include "GamepadState.h"
#include "enums.pb.h"
#include "drivermanager.h"
#include "eventmanager.h"
#include "GPStorageSaveEvent.h"

namespace {

// Pointer to memory-mapped flash inside a recording slot (XIP).
inline const uint8_t * recFlashPtr(uint8_t slot, uint32_t layoutOffset)
{
    return reinterpret_cast<const uint8_t *>(
        XIP_BASE + MACRO_REC_FLASH_OFFSET +
        static_cast<uint32_t>(slot) * MACRO_REC_SLOT_SIZE + layoutOffset);
}

// Flash offset (from flash base) expected by flash_range_* / FlashPROM wrappers.
inline uint32_t recFlashOffset(uint8_t slot, uint32_t layoutOffset)
{
    return MACRO_REC_FLASH_OFFSET +
           static_cast<uint32_t>(slot) * MACRO_REC_SLOT_SIZE + layoutOffset;
}

// Pack the final output state into the recorded button mask.
inline uint32_t recStateMask(const GamepadState & s)
{
    return s.buttons | (static_cast<uint32_t>(s.dpad & 0x0F) << 16);
}

} // namespace

bool InputMacro::available() {
    const MacroOptions& macroOptions = Storage::getInstance().getAddonOptions().macroOptions;
    for (int i = 0; i < MAX_MACRO_LIMIT; i++) {
        const Macro& macro = macroOptions.macroList[i];
        // Recorded slots (macro 1/2) load with zero edited steps; edited
        // macros (3-6) need at least one step.
        if (macro.enabled &&
            (isRecordedMacroIndex(i) || macro.macroInputs_count > 0)) {
            return true;
        }
    }
    return false;
}

void InputMacro::setup() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = Mask_t{1} << pin;
                break;
            default:
                break;
        }
    }

    inputMacroOptions = &Storage::getInstance().getAddonOptions().macroOptions;
    prevMacroInputPressed = false;

    for (uint8_t slot = 0; slot < MACRO_REC_SLOT_COUNT; ++slot) {
        MacroRecSlot & r = recs[slot];
        memset(&r, 0, sizeof(r));
        r.playEvtPtr = nullptr;
    }

    for (uint8_t slot = 0; slot < MACRO_REC_SLOT_COUNT; ++slot)
        validateRecordingStream(slot);

    reset();
}


void InputMacro::reset() {
    // All playback terminations funnel through here — normal completion,
    // user interruption (other button / toggle-off / trigger release).
    // Emit the one-shot blue blink once per real playback session; the
    // setup()-time call has isMacroRunning == false and must stay silent.
    const bool wasRunning = isMacroRunning;
    if (isRecordedMacroIndex(macroPosition))
        recs[macroPosition].playActive = false;
    macroPosition = -1;
    pressedMacro = -1;
    isMacroRunning = false;
    macroStartTime = 0;
    macroInputPosition = 0;
    isMacroTriggerHeld = false;
    macroInputHoldTime = INPUT_HOLD_US;
    if (wasRunning)
        Storage::getInstance().pulseMacroHint();
}

void InputMacro::restart(Macro& macro) {
    macroStartTime = currentMicros;
    macroInputPosition = 0;
    MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
    uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
    macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
}

bool InputMacro::isRecordedMacroEnabled(uint8_t slot) const {
    return inputMacroOptions != nullptr &&
           inputMacroOptions->macroList[slot].enabled;
}

bool InputMacro::validateRecordingStream(uint8_t slot) {
    MacroRecSlot & r = recs[slot];
    const MacroRecHeader * hdr =
        reinterpret_cast<const MacroRecHeader *>(recFlashPtr(slot, MACRO_REC_HEADER_OFFSET));
    bool ok = hdr->magic[0] == 'G' && hdr->magic[1] == 'R' &&
              hdr->magic[2] == '0' && hdr->magic[3] == '1' &&
              hdr->version == MACRO_REC_VERSION &&
              hdr->totalFrames > 0 &&
              hdr->totalFrames <= MACRO_REC_FRAME_CAP &&
              static_cast<uint64_t>(hdr->eventCount) * MACRO_REC_MAX_EVENT_BYTES
                  <= MACRO_REC_EVENT_SIZE;
    if (ok) {
        r.hdrFrames = hdr->totalFrames;
        r.hdrEvents = hdr->eventCount;
    } else {
        r.hdrFrames = 0;
        r.hdrEvents = 0;
    }
    r.streamValid = ok;

    // Self-heal: metadata claims a recording but flash has no valid stream
    // (e.g. power loss during the start erase). Never replay 0xFF garbage.
    Macro& macroSlot = Storage::getInstance()
                        .getAddonOptions().macroOptions.macroList[slot];
    if (macroSlot.hasRecording && !ok) {
        macroSlot.hasRecording = false;
        macroSlot.recFrames = 0;
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(false));
    }
    return ok;
}

void InputMacro::checkMacroPress() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    Mask_t allPins = gamepad->debouncedGpio;

    // Go through our macro list
    pressedMacro = -1;
    for(int i = 0; i < MAX_MACRO_LIMIT; i++) {
        const Macro& macroConst = inputMacroOptions->macroList[i];
        if (!macroConst.enabled)
            continue;
        const bool recordedMacro =
            isRecordedMacroIndex(i) && macroConst.hasRecording &&
            recs[i].streamValid;
        if (!recordedMacro && macroConst.macroInputs_count == 0)
            continue;
        Macro * macro = &inputMacroOptions->macroList[i];
        if ( gamepad->addonMacroTriggerMask & (1U << i) ) {
            // Addon mappings are an independent trigger source.
            pressedMacro = i;
            break;
        } else if ( macro->useMacroTriggerButton ) {
            // Use Gamepad Button for Macro Trigger
            if ((allPins & macroButtonMask) &&
                ((gamepad->state.buttons & macro->macroTriggerButton) ||
                    (gamepad->state.dpad & (macro->macroTriggerButton >> 16))) ) {
                pressedMacro = i;
                break;
            }
        } else if ( allPins & macroPinMasks[i] ) {
            // Use Pin Manager for Macro Trigger
            pressedMacro = i;
            break;
        }
    }
}

void InputMacro::checkMacroAction() {
    bool macroInputPressed = (pressedMacro != -1); // Was any macro input pressed?

    // Is our pressed macro different from our current macro AND no macro is running?
    if ( pressedMacro != macroPosition && !isMacroRunning ) {
        macroPosition = pressedMacro; // move our position to that macro
    }

    bool newPress = macroInputPressed && (prevMacroInputPressed ^ macroInputPressed);

    if (!isMacroRunning && macroPosition == -1) {
        isMacroTriggerHeld = false;
        prevMacroInputPressed = false;
        return;
    }

    // Check to see if we should change the current macro (or turn off based on input)
    if ( inputMacroOptions->macroList[macroPosition].macroType == ON_PRESS ) {
        // START Macro: On Press or On Hold Repeat
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        }
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_HOLD_REPEAT ) {
        isMacroTriggerHeld = macroInputPressed;
    } else if ( inputMacroOptions->macroList[macroPosition].macroType == ON_TOGGLE ) {
        if (!isMacroRunning ) {
            isMacroTriggerHeld = newPress;
        } else if (isMacroRunning && newPress) {
            // STOP Macro: Toggle on new press
            reset(); // Stop Macro: Toggle
            prevMacroInputPressed = macroInputPressed;
            return;
        }
    }

    prevMacroInputPressed = macroInputPressed;
    if (!isMacroRunning && isMacroTriggerHeld) {
        // New Macro to run
        macroPosition = pressedMacro; // Set current macro
        Macro& macro = inputMacroOptions->macroList[macroPosition];
        if (isRecordedMacroIndex(macroPosition) && macro.hasRecording &&
            recs[macroPosition].streamValid) {
            // Recorded stream carries its own timeline.
            macroInputHoldTime = recs[macroPosition].hdrFrames * MACRO_REC_TICK_US;
        } else {
            MacroInput& macroInput = macro.macroInputs[macroInputPosition];
            uint32_t macroInputDuration = macroInput.duration + macroInput.waitDuration;
            macroInputHoldTime = macroInputDuration <= 0 ? INPUT_HOLD_US : macroInputDuration;
        }
        isMacroRunning = true;
        macroStartTime = getMicro(); // current time
    }
}

void InputMacro::beginRecordedPlayback(uint8_t slot) {
    MacroRecSlot & r = recs[slot];
    r.playActive = true;
    r.playEvtPtr = recFlashPtr(slot, MACRO_REC_EVENT_OFFSET);
    r.playBytesLeft = MACRO_REC_EVENT_SIZE;
    r.playEventsRead = 0;
    r.playMask = 0;
    r.playLt = 0;
    r.playRt = 0;
}

void InputMacro::restartRecorded(uint8_t slot) {
    macroStartTime = getMicro();
    macroInputPosition = 0;
    beginRecordedPlayback(slot);
}

void InputMacro::runRecordedMacro(uint8_t slot, uint64_t now) {
    MacroRecSlot & r = recs[slot];
    if (!r.playActive)
        beginRecordedPlayback(slot);

    const Macro& macro = inputMacroOptions->macroList[slot];
    Gamepad * gamepad = Storage::getInstance().GetGamepad();

    // Interruptible hold-repeat macros stop immediately when their trigger is released.
    if (macro.macroType == ON_HOLD_REPEAT &&
            macro.interruptible &&
            !isMacroTriggerHeld) {
        reset();
        return;
    }

    uint32_t tick = static_cast<uint32_t>((now - macroStartTime) / MACRO_REC_TICK_US);

    // Same exclusive/interruptible split as edited macros (exclusive is always
    // true for recorded slots, enforced on save).
    if (!macro.interruptible) {
        // Exclusive: drop all live user buttons/dpad for this pass.
        gamepad->state.dpad = 0;
        gamepad->state.buttons = 0;
    } else {
        if (macro.useMacroTriggerButton) {
            // Remove the trigger button from the input state
            gamepad->state.dpad &= ~(macro.macroTriggerButton >> 16);
            gamepad->state.buttons &= ~macro.macroTriggerButton;
        }
        if (gamepad->state.buttons != 0 || gamepad->state.dpad != 0) {
            // Interruptible and the user pressed something else.
            reset();
            return;
        }
    }

    // End of the recorded stream: play once, repeat while held, or loop.
    if (tick >= r.hdrFrames) {
        if (macro.macroType == ON_PRESS ||
            (macro.macroType == ON_HOLD_REPEAT && !isMacroTriggerHeld)) {
            reset();
            return;
        }
        // ON_HOLD_REPEAT while held, or ON_TOGGLE: restart from the first frame
        // and emit the initial state during this same loop iteration.
        restartRecorded(slot);
        tick = 0;
    }

    // Consume every event whose frame index has been reached.
    while (r.playEventsRead < r.hdrEvents) {
        uint32_t frame = 0, mask = 0;
        uint8_t n1 = macroRecDecodeUVarint(r.playEvtPtr, r.playBytesLeft, frame);
        if (n1 == 0 || static_cast<uint32_t>(n1) + 9 > r.playBytesLeft) break;
        if (frame > tick) break;
        const uint8_t * p = r.playEvtPtr + n1;
        uint32_t left = r.playBytesLeft - n1;
        uint8_t n2 = macroRecDecodeUVarint(p, left, mask);
        if (n2 == 0 || static_cast<uint32_t>(n2) + 2 > left) break;
        r.playMask = mask;
        r.playLt = p[n2];
        r.playRt = p[n2 + 1];
        uint8_t used = n1 + n2 + 2;
        r.playEvtPtr += used;
        r.playBytesLeft -= used;
        r.playEventsRead++;
    }

    if (r.playMask & GAMEPAD_MASK_DU) gamepad->state.dpad |= GAMEPAD_MASK_UP;
    if (r.playMask & GAMEPAD_MASK_DD) gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
    if (r.playMask & GAMEPAD_MASK_DL) gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
    if (r.playMask & GAMEPAD_MASK_DR) gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
    gamepad->state.buttons |= r.playMask;
}

void InputMacro::runCurrentMacro() {
    // Do nothing if macro is not currently running
    if (!isMacroRunning ||
            macroPosition == -1)
        return;

    Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Recorded macro: dedicated timeline branch, no edited-step logic.
    if (isRecordedMacroIndex(macroPosition) && macro.hasRecording &&
        recs[macroPosition].streamValid) {
        currentMicros = getMicro();
        runRecordedMacro(static_cast<uint8_t>(macroPosition), currentMicros);
        return;
    }

    // Interruptible hold-repeat macros stop immediately when their trigger is released.
    if (macro.macroType == ON_HOLD_REPEAT &&
            macro.interruptible &&
            !isMacroTriggerHeld) {
        reset();
        return;
    }

    MacroInput& macroInput = macro.macroInputs[macroInputPosition];
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    currentMicros = getMicro();

    if (!macro.interruptible && macro.exclusive) {
        // Prevent any other inputs from modifying our input (Exclusive)
        gamepad->state.dpad = 0;
        gamepad->state.buttons = 0;
    } else {
        if (macro.useMacroTriggerButton) {
            // Remove the trigger button from the input state
            gamepad->state.dpad &= ~(macro.macroTriggerButton >> 16);
            gamepad->state.buttons &= ~macro.macroTriggerButton;
        }
        if (macro.interruptible &&
            (gamepad->state.buttons != 0 || gamepad->state.dpad != 0)) {
            // Macro is interruptible and a user pressed something
            reset();
            return;
        }
    }

    // Have we elapsed the input hold time?
    if ((currentMicros - macroStartTime) >= macroInputHoldTime) {
        macroStartTime = currentMicros;
        macroInputPosition++;

        if (macroInputPosition >= (macro.macroInputs_count)) {
            if (macro.macroType == ON_PRESS ||
                (macro.macroType == ON_HOLD_REPEAT && !isMacroTriggerHeld)) {
                // Non-interruptible hold-repeat macros finish the current pass after release.
                reset();
            } else {
                restart(macro); // On Hold-Repeat or On Toggle = start macro again
            }
        } else {
            MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
            uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
            macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
        }
    }

    // Check if we should still hold this macro input based on duration
    if ((currentMicros - macroStartTime) <= macroInput.duration) {
        uint32_t buttonMask = macroInput.buttonMask;
        if (buttonMask & GAMEPAD_MASK_DU) {
            gamepad->state.dpad |= GAMEPAD_MASK_UP;
        }
        if (buttonMask & GAMEPAD_MASK_DD) {
            gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
        }
        if (buttonMask & GAMEPAD_MASK_DL) {
            gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
        }
        if (buttonMask & GAMEPAD_MASK_DR) {
            gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
        }
        gamepad->state.buttons |= buttonMask;

        // Handle stick direction if set
        if (macroInput.has_stickDirection && macroInput.stickDirection != 0) {
            uint32_t stickDirection = macroInput.stickDirection;
            uint16_t joystickMid = GAMEPAD_JOYSTICK_MID;

            // Get joystick midpoint from driver if available
            if (DriverManager::getInstance().getDriver() != nullptr) {
                joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
            }

            // Check for stick center commands
            if (stickDirection == 0xFFFFFFFE) {
                // Center left stick
                gamepad->state.lx = joystickMid;
                gamepad->state.ly = joystickMid;
            } else if (stickDirection == 0xFFFFFFFD) {
                // Center right stick
                gamepad->state.rx = joystickMid;
                gamepad->state.ry = joystickMid;
            } else {
                // Apply stick direction based on GpioAction value
                switch (stickDirection) {
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_X_NEG:
                        gamepad->state.lx = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_X_POS:
                        gamepad->state.lx = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
                        gamepad->state.ly = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_Y_POS:
                        gamepad->state.ly = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_X_NEG:
                        gamepad->state.rx = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_X_POS:
                        gamepad->state.rx = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
                        gamepad->state.ry = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_Y_POS:
                        gamepad->state.ry = GAMEPAD_JOYSTICK_MAX;
                        break;
                    default:
                        // Unknown stick direction, do nothing
                        break;
                }
            }
        }
    }
}

void InputMacro::checkRecordHotkey() {
    // Don't toggle while any macro is playing back.
    if (isMacroRunning) {
        recs[0].hotkeyPrev = false;
        recs[1].hotkeyPrev = false;
        return;
    }

    if (Storage::getInstance().getGamepadOptions().lockHotkeys)
        return;

    const HotkeyOptions& hotkeyOptions = Storage::getInstance().getHotkeyOptions();
    const HotkeyEntry entries[16] = {
        hotkeyOptions.hotkey01, hotkeyOptions.hotkey02, hotkeyOptions.hotkey03,
        hotkeyOptions.hotkey04, hotkeyOptions.hotkey05, hotkeyOptions.hotkey06,
        hotkeyOptions.hotkey07, hotkeyOptions.hotkey08, hotkeyOptions.hotkey09,
        hotkeyOptions.hotkey10, hotkeyOptions.hotkey11, hotkeyOptions.hotkey12,
        hotkeyOptions.hotkey13, hotkeyOptions.hotkey14, hotkeyOptions.hotkey15,
        hotkeyOptions.hotkey16,
    };

    Gamepad * gamepad = Storage::getInstance().GetGamepad();

    for (uint8_t slot = 0; slot < MACRO_REC_SLOT_COUNT; ++slot) {
        MacroRecSlot & r = recs[slot];

        if (!isRecordedMacroEnabled(slot)) {
            r.hotkeyPrev = false;
            continue;
        }

        // Slots are mutually exclusive: only scan when no slot is recording,
        // or this slot itself is active (its own hotkey then stops it). The
        // active set is re-read every iteration (not snapshotted) so that
        // starting slot 0 inside this very call prevents slot 1 from starting
        // when both combos happen to be held at once.
        const bool anySlotActive = recs[0].active || recs[1].active;
        bool pressed = false;
        if (!anySlotActive || r.active) {
            const GamepadHotkey wanted = static_cast<GamepadHotkey>(
                HOTKEY_MACRO_RECORD_1 + slot);
            for (const HotkeyEntry & entry : entries) {
                if (entry.action == wanted && gamepad->pressedHotkey(entry)) {
                    pressed = true;
                    break;
                }
            }
        }

        if (pressed && !r.hotkeyPrev) {
            if (r.active) {
                stopRecording(slot, false);
            } else {
                startRecording(slot);
            }
        }
        r.hotkeyPrev = pressed;
    }
}

void InputMacro::preprocess()
{
    FocusModeOptions * focusModeOptions = &Storage::getInstance().getAddonOptions().focusModeOptions;
    if (focusModeOptions->enabled && focusModeOptions->macroLockEnabled) {
        Gamepad * gamepad = Storage::getInstance().GetGamepad();
        // Override Toggle Pressed OR focus mode pin is set
        if (focusModeOptions->overrideEnabled ||
            (gamepad->mapFocusMode->pinMask && (gamepad->debouncedGpio & gamepad->mapFocusMode->pinMask))) {
            return;
        }
    }

    checkRecordHotkey();

    // While any slot is recording, all macro playback/trigger logic is suspended.
    if (recs[0].active || recs[1].active)
        return;

    checkMacroPress();
    checkMacroAction();
    runCurrentMacro();
}

void InputMacro::process()
{
    // Reapply stick/trigger values after other addons (like AnalogInput) have
    // processed, so macro values are not overwritten by physical input.
    if (!isMacroRunning || macroPosition == -1)
        return;

    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    currentMicros = getMicro();

    Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Recorded stream: analog axes/triggers come straight from the frame/event tracks.
    if (isRecordedMacroIndex(macroPosition) && macro.hasRecording &&
        recs[macroPosition].streamValid && recs[macroPosition].playActive) {
        MacroRecSlot & r = recs[macroPosition];
        uint32_t tick = static_cast<uint32_t>(
            (currentMicros - macroStartTime) / MACRO_REC_TICK_US);
        if (tick >= r.hdrFrames)
            tick = r.hdrFrames - 1;
        const uint8_t * frame = recFlashPtr(static_cast<uint8_t>(macroPosition),
                                             MACRO_REC_FRAME_OFFSET + tick * 4u);
        gamepad->state.lx = macroRecRestoreAxis(frame[0]);
        gamepad->state.ly = macroRecRestoreAxis(frame[1]);
        gamepad->state.rx = macroRecRestoreAxis(frame[2]);
        gamepad->state.ry = macroRecRestoreAxis(frame[3]);
        gamepad->state.lt = r.playLt;
        gamepad->state.rt = r.playRt;
        return;
    }

    MacroInput& macroInput = macro.macroInputs[macroInputPosition];

    // Only reapply stick direction if we're still within the duration window
    if ((currentMicros - macroStartTime) <= macroInput.duration) {
        if (macroInput.has_stickDirection && macroInput.stickDirection != 0) {
            uint32_t stickDirection = macroInput.stickDirection;
            uint16_t joystickMid = GAMEPAD_JOYSTICK_MID;

            if (DriverManager::getInstance().getDriver() != nullptr) {
                joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
            }

            if (stickDirection == 0xFFFFFFFE) {
                gamepad->state.lx = joystickMid;
                gamepad->state.ly = joystickMid;
            } else if (stickDirection == 0xFFFFFFFD) {
                gamepad->state.rx = joystickMid;
                gamepad->state.ry = joystickMid;
            } else {
                switch (stickDirection) {
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_X_NEG:
                        gamepad->state.lx = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_X_POS:
                        gamepad->state.lx = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_Y_NEG:
                        gamepad->state.ly = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_LS_Y_POS:
                        gamepad->state.ly = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_X_NEG:
                        gamepad->state.rx = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_X_POS:
                        gamepad->state.rx = GAMEPAD_JOYSTICK_MAX;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_Y_NEG:
                        gamepad->state.ry = GAMEPAD_JOYSTICK_MIN;
                        break;
                    case (uint32_t)GpioAction::ANALOG_DIRECTION_RS_Y_POS:
                        gamepad->state.ry = GAMEPAD_JOYSTICK_MAX;
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

bool InputMacro::hasStickDirection() const
{
    // Recorded playback drives all four axes from the frame track.
    for (uint8_t slot = 0; slot < MACRO_REC_SLOT_COUNT; ++slot) {
        if (recs[slot].playActive)
            return true;
    }

    if (!isMacroRunning || macroPosition == -1)
        return false;

    const Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Recorded macro about to run / running (process() may lag by one call).
    if (isRecordedMacroIndex(macroPosition) && macro.hasRecording &&
        recs[macroPosition].streamValid) {
        return true;
    }

    const MacroInput& macroInput = macro.macroInputs[macroInputPosition];
    uint64_t now = getMicro();

    return (now - macroStartTime) <= macroInput.duration &&
           macroInput.has_stickDirection &&
           macroInput.stickDirection != 0;
}

// --- Recording ---

void InputMacro::startRecording(uint8_t slot) {
    // One-shot erase of this slot's 192KB before the first sample is taken.
    // Typical ~0.45s stall (3 x 64KB block erases); no samples exist yet.
    FlashPROM::eraseRange(
        MACRO_REC_FLASH_OFFSET + static_cast<uint32_t>(slot) * MACRO_REC_SLOT_SIZE,
        MACRO_REC_SLOT_SIZE);

    MacroRecSlot & r = recs[slot];
    r.framesWritten = 0;
    r.eventCount = 0;
    r.evtBytePos = 0;
    r.evtPagePos = 0;
    r.frmPagePos = 0;
    r.lastMask = 0;
    r.lastLt = 0;
    r.lastRt = 0;
    r.hasLastEvent = false;
    r.streamValid = false;
    r.hdrFrames = 0;
    r.hdrEvents = 0;
    r.playActive = false;
    r.startUs = getMicro();
    r.active = true;

    // One-shot blue blink on recording start (no continuous recording light).
    Storage::getInstance().pulseMacroHint();
}

void InputMacro::recordWriteBytes(uint8_t slot, const uint8_t * data, uint8_t len) {
    MacroRecSlot & r = recs[slot];
    for (uint8_t i = 0; i < len; ++i) {
        r.evtPage[r.evtPagePos++] = data[i];
        ++r.evtBytePos;
        if (r.evtPagePos == 256)
            flushEventPage(slot);
    }
}

void InputMacro::flushEventPage(uint8_t slot) {
    MacroRecSlot & r = recs[slot];
    if (r.evtPagePos == 0)
        return;
    uint32_t pageLayout = MACRO_REC_EVENT_OFFSET + r.evtBytePos - r.evtPagePos;
    while (r.evtPagePos < 256)
        r.evtPage[r.evtPagePos++] = 0xFF;
    FlashPROM::programPages(recFlashOffset(slot, pageLayout), r.evtPage, 256);
    r.evtPagePos = 0;
}

void InputMacro::flushFramePage(uint8_t slot) {
    MacroRecSlot & r = recs[slot];
    if (r.frmPagePos == 0)
        return;
    uint32_t frameBytesUsed = r.framesWritten * 4u;
    uint32_t pageLayout = MACRO_REC_FRAME_OFFSET + frameBytesUsed - r.frmPagePos;
    while (r.frmPagePos < 256)
        r.frmPage[r.frmPagePos++] = 0xFF;
    FlashPROM::programPages(recFlashOffset(slot, pageLayout), r.frmPage, 256);
    r.frmPagePos = 0;
}

bool InputMacro::appendRecordEvent(uint8_t slot, uint32_t frame, uint32_t mask, uint8_t lt, uint8_t rt) {
    MacroRecSlot & r = recs[slot];
    uint8_t enc[MACRO_REC_MAX_EVENT_BYTES];
    uint8_t n1 = macroRecEncodeUVarint(frame, enc);
    uint8_t n2 = macroRecEncodeUVarint(mask, enc + n1);
    uint8_t total = n1 + n2 + 2;
    if (r.evtBytePos + total > MACRO_REC_EVENT_SIZE)
        return false;
    recordWriteBytes(slot, enc, n1 + n2);
    r.evtPage[r.evtPagePos++] = lt;
    ++r.evtBytePos;
    if (r.evtPagePos == 256) flushEventPage(slot);
    r.evtPage[r.evtPagePos++] = rt;
    ++r.evtBytePos;
    if (r.evtPagePos == 256) flushEventPage(slot);
    r.eventCount++;
    r.lastMask = mask;
    r.lastLt = lt;
    r.lastRt = rt;
    r.hasLastEvent = true;
    return true;
}

void InputMacro::recordSample(uint8_t slot, uint64_t now) {
    MacroRecSlot & r = recs[slot];
    GamepadState & s = Storage::getInstance().GetGamepad()->state;

    uint32_t tick = static_cast<uint32_t>((now - r.startUs) / MACRO_REC_TICK_US);
    // All frame slots used means the stream is full.
    bool overflow = (tick >= MACRO_REC_FRAME_CAP);
    uint32_t targetTick = overflow ? MACRO_REC_FRAME_CAP - 1 : tick;

    // The current sample is valid at the current tick, so write its slot and
    // every slot skipped since the last sample (e.g. across a page-program
    // stall) with the same sample. The frame timeline stays dense.
    const uint8_t q[4] = {
        macroRecQuantAxis(s.lx), macroRecQuantAxis(s.ly),
        macroRecQuantAxis(s.rx), macroRecQuantAxis(s.ry)
    };
    while (r.framesWritten <= targetTick && r.framesWritten < MACRO_REC_FRAME_CAP) {
        r.frmPage[r.frmPagePos++] = q[0];
        r.frmPage[r.frmPagePos++] = q[1];
        r.frmPage[r.frmPagePos++] = q[2];
        r.frmPage[r.frmPagePos++] = q[3];
        ++r.framesWritten;
        if (r.frmPagePos == 256)
            flushFramePage(slot);
    }

    // Change-driven button/trigger event paired with the current tick's frame.
    if (!overflow) {
        uint32_t mask = recStateMask(s);
        if (!r.hasLastEvent || mask != r.lastMask ||
            s.lt != r.lastLt || s.rt != r.lastRt) {
            if (!appendRecordEvent(slot, targetTick, mask, s.lt, s.rt))
                overflow = true;
        }
    }

    if (overflow)
        stopRecording(slot, true);
}

void InputMacro::postprocess(bool sent) {
    (void)sent;
    for (uint8_t slot = 0; slot < MACRO_REC_SLOT_COUNT; ++slot) {
        if (recs[slot].active)
            recordSample(slot, getMicro());
    }
}

void InputMacro::stopRecording(uint8_t slot, bool overflow) {
    MacroRecSlot & r = recs[slot];
    if (!r.active)
        return;

    r.active = false;
    flushFramePage(slot);
    flushEventPage(slot);

    // Header page last: power-loss before this point leaves an invalid stream.
    uint8_t headerPage[256];
    memset(headerPage, 0xFF, sizeof(headerPage));
    MacroRecHeader hdr;
    hdr.magic[0] = 'G'; hdr.magic[1] = 'R'; hdr.magic[2] = '0'; hdr.magic[3] = '1';
    hdr.version = MACRO_REC_VERSION;
    hdr.reserved[0] = hdr.reserved[1] = hdr.reserved[2] = 0;
    hdr.totalFrames = r.framesWritten;
    hdr.eventCount = r.eventCount;
    memcpy(headerPage, &hdr, sizeof(hdr));
    FlashPROM::programPages(recFlashOffset(slot, MACRO_REC_HEADER_OFFSET), headerPage, 256);

    r.streamValid = r.framesWritten > 0;
    r.hdrFrames = r.framesWritten;
    r.hdrEvents = r.eventCount;

    // Capacity-induced auto stop gets the red 3-blink warning; a manual
    // hotkey stop stays silent (blue blink is only emitted at record start).
    if (overflow)
        Storage::getInstance().pulseMacroRecFull();

    // Commit metadata to config (stream itself is already on flash).
    // Recorded slots are always exclusive; macroType/interruptible are user
    // configurable and left untouched.
    Macro& macro = inputMacroOptions->macroList[slot];
    macro.enabled = true;
    macro.exclusive = true;
    macro.macroInputs_count = 0;
    macro.hasRecording = r.framesWritten > 0;
    macro.recFrames = r.framesWritten;
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(false));
}

void InputMacro::reinit() {
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    macroButtonMask = 0;
    memset(macroPinMasks, 0, sizeof(macroPinMasks));
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
                macroButtonMask = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_1:
                macroPinMasks[0] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_2:
                macroPinMasks[1] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_3:
                macroPinMasks[2] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_4:
                macroPinMasks[3] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_5:
                macroPinMasks[4] = Mask_t{1} << pin;
                break;
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroPinMasks[5] = Mask_t{1} << pin;
                break;
            default:
                break;
        }
    }
}
