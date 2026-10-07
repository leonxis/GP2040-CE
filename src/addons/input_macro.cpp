#include "addons/input_macro.h"
#include "storagemanager.h"
#include "GamepadState.h"
#include "enums.pb.h"
#include "drivermanager.h"
#include "eventmanager.h"
#include "GPStorageSaveEvent.h"

namespace {

// Pointer to memory-mapped flash inside the macro recording region (XIP).
inline const uint8_t * recFlashPtr(uint32_t layoutOffset)
{
    return reinterpret_cast<const uint8_t *>(
        XIP_BASE + MACRO_REC_FLASH_OFFSET + layoutOffset);
}

// Flash offset (from flash base) expected by flash_range_* / FlashPROM wrappers.
inline uint32_t recFlashOffset(uint32_t layoutOffset)
{
    return MACRO_REC_FLASH_OFFSET + layoutOffset;
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
        // Record-mode macro 1 loads the addon even with zero edited steps.
        if (macro.enabled &&
            (macro.macroInputs_count > 0 ||
             (i == MACRO_REC_INDEX && macro.recordMode))) {
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

    // Recording state
    recActive = false;
    recHotkeyPrev = false;
    recStreamValid = false;
    recHdrFrames = 0;
    recHdrEvents = 0;
    recStartUs = 0;
    recFramesWritten = 0;
    recEventCount = 0;
    recEvtBytePos = 0;
    recEvtPagePos = 0;
    recFrmPagePos = 0;
    recLastMask = 0;
    recLastLt = 0;
    recLastRt = 0;
    recHasLastEvent = false;
    recPlayActive = false;
    recPlayEvtPtr = nullptr;
    recPlayBytesLeft = 0;
    recPlayEventsRead = 0;
    recPlayMask = 0;
    recPlayLt = 0;
    recPlayRt = 0;

    validateRecordingStream();
    reset();
}


void InputMacro::reset() {
    macroPosition = -1;
    pressedMacro = -1;
    isMacroRunning = false;
    macroStartTime = 0;
    macroInputPosition = 0;
    isMacroTriggerHeld = false;
    macroInputHoldTime = INPUT_HOLD_US;
    recPlayActive = false;
}

void InputMacro::restart(Macro& macro) {
    macroStartTime = currentMicros;
    macroInputPosition = 0;
    MacroInput& newMacroInput = macro.macroInputs[macroInputPosition];
    uint32_t newMacroInputDuration = newMacroInput.duration + newMacroInput.waitDuration;
    macroInputHoldTime = newMacroInputDuration <= 0 ? INPUT_HOLD_US : newMacroInputDuration;
}

bool InputMacro::isRecordedMacroEnabled() const {
    return inputMacroOptions != nullptr &&
           inputMacroOptions->macroList[MACRO_REC_INDEX].enabled &&
           inputMacroOptions->macroList[MACRO_REC_INDEX].recordMode;
}

bool InputMacro::validateRecordingStream() {
    const MacroRecHeader * hdr =
        reinterpret_cast<const MacroRecHeader *>(recFlashPtr(MACRO_REC_HEADER_OFFSET));
    bool ok = hdr->magic[0] == 'G' && hdr->magic[1] == 'R' &&
              hdr->magic[2] == '0' && hdr->magic[3] == '1' &&
              hdr->version == MACRO_REC_VERSION &&
              hdr->totalFrames > 0 &&
              hdr->totalFrames <= MACRO_REC_FRAME_CAP &&
              static_cast<uint64_t>(hdr->eventCount) * MACRO_REC_MAX_EVENT_BYTES
                  <= MACRO_REC_EVENT_SIZE;
    if (ok) {
        recHdrFrames = hdr->totalFrames;
        recHdrEvents = hdr->eventCount;
    } else {
        recHdrFrames = 0;
        recHdrEvents = 0;
    }
    recStreamValid = ok;

    // Self-heal: metadata claims a recording but flash has no valid stream
    // (e.g. power loss during the start erase). Never replay 0xFF garbage.
    Macro& macro0 = Storage::getInstance()
                        .getAddonOptions().macroOptions.macroList[MACRO_REC_INDEX];
    if (macro0.hasRecording && !ok) {
        macro0.hasRecording = false;
        macro0.recFrames = 0;
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
            i == MACRO_REC_INDEX && macroConst.recordMode &&
            macroConst.hasRecording && recStreamValid;
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
        //isMacroTriggerHeld = macroInputPressed;
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
        if (macro.recordMode && macro.hasRecording && recStreamValid) {
            // Recorded stream carries its own timeline.
            macroInputHoldTime = recHdrFrames * MACRO_REC_TICK_US;
        } else {
            MacroInput& macroInput = macro.macroInputs[macroInputPosition];
            uint32_t macroInputDuration = macroInput.duration + macroInput.waitDuration;
            macroInputHoldTime = macroInputDuration <= 0 ? INPUT_HOLD_US : macroInputDuration;
        }
        isMacroRunning = true;
        macroStartTime = getMicro(); // current time
    }
}

void InputMacro::beginRecordedPlayback() {
    recPlayActive = true;
    recPlayEvtPtr = recFlashPtr(MACRO_REC_EVENT_OFFSET);
    recPlayBytesLeft = MACRO_REC_EVENT_SIZE;
    recPlayEventsRead = 0;
    recPlayMask = 0;
    recPlayLt = 0;
    recPlayRt = 0;
}

void InputMacro::runRecordedMacro(uint64_t now) {
    if (!recPlayActive)
        beginRecordedPlayback();

    uint32_t tick = static_cast<uint32_t>((now - macroStartTime) / MACRO_REC_TICK_US);

    // ON_PRESS semantics: play the stream once, then stop.
    if (tick >= recHdrFrames) {
        reset();
        return;
    }

    // Consume every event whose frame index has been reached.
    while (recPlayEventsRead < recHdrEvents) {
        uint32_t frame = 0, mask = 0;
        uint8_t n1 = macroRecDecodeUVarint(recPlayEvtPtr, recPlayBytesLeft, frame);
        if (n1 == 0 || static_cast<uint32_t>(n1) + 9 > recPlayBytesLeft) break;
        if (frame > tick) break;
        const uint8_t * p = recPlayEvtPtr + n1;
        uint32_t left = recPlayBytesLeft - n1;
        uint8_t n2 = macroRecDecodeUVarint(p, left, mask);
        if (n2 == 0 || static_cast<uint32_t>(n2) + 2 > left) break;
        recPlayMask = mask;
        recPlayLt = p[n2];
        recPlayRt = p[n2 + 1];
        uint8_t used = n1 + n2 + 2;
        recPlayEvtPtr += used;
        recPlayBytesLeft -= used;
        recPlayEventsRead++;
    }

    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    gamepad->state.dpad = 0;
    gamepad->state.buttons = 0;
    if (recPlayMask & GAMEPAD_MASK_DU) gamepad->state.dpad |= GAMEPAD_MASK_UP;
    if (recPlayMask & GAMEPAD_MASK_DD) gamepad->state.dpad |= GAMEPAD_MASK_DOWN;
    if (recPlayMask & GAMEPAD_MASK_DL) gamepad->state.dpad |= GAMEPAD_MASK_LEFT;
    if (recPlayMask & GAMEPAD_MASK_DR) gamepad->state.dpad |= GAMEPAD_MASK_RIGHT;
    gamepad->state.buttons |= recPlayMask;
}

static void applyManualAxes(GamepadState & state, const MacroInput & macroInput) {
    if (macroInput.has_lx) state.lx = static_cast<uint16_t>(macroInput.lx);
    if (macroInput.has_ly) state.ly = static_cast<uint16_t>(macroInput.ly);
    if (macroInput.has_rx) state.rx = static_cast<uint16_t>(macroInput.rx);
    if (macroInput.has_ry) state.ry = static_cast<uint16_t>(macroInput.ry);
}

void InputMacro::runCurrentMacro() {
    // Do nothing if macro is not currently running
    if (!isMacroRunning ||
            macroPosition == -1)
        return;

    Macro& macro = inputMacroOptions->macroList[macroPosition];

    // Recorded macro: dedicated timeline branch, no edited-step logic.
    if (macroPosition == MACRO_REC_INDEX && macro.recordMode &&
        macro.hasRecording && recStreamValid) {
        currentMicros = getMicro();
        runRecordedMacro(currentMicros);
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

        // Analog axes (web editor writes full-scale/center values per direction).
        applyManualAxes(gamepad->state, macroInput);
    }
}

void InputMacro::checkRecordHotkey() {
    if (!isRecordedMacroEnabled()) {
        recHotkeyPrev = false;
        return;
    }

    // Don't toggle while any macro is playing back.
    if (isMacroRunning) {
        recHotkeyPrev = false;
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
    bool pressed = false;
    for (const HotkeyEntry & entry : entries) {
        if (entry.action == HOTKEY_MACRO_RECORD_1 && gamepad->pressedHotkey(entry)) {
            pressed = true;
            break;
        }
    }

    if (pressed && !recHotkeyPrev) {
        if (recActive) {
            stopRecording(false);
        } else {
            startRecording();
        }
    }
    recHotkeyPrev = pressed;
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

    // While recording, all macro playback/trigger logic is suspended.
    if (recActive)
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
    if (macroPosition == MACRO_REC_INDEX && macro.recordMode &&
        macro.hasRecording && recStreamValid && recPlayActive) {
        uint32_t tick = static_cast<uint32_t>(
            (currentMicros - macroStartTime) / MACRO_REC_TICK_US);
        if (tick >= recHdrFrames)
            tick = recHdrFrames - 1;
        const uint8_t * frame = recFlashPtr(MACRO_REC_FRAME_OFFSET + tick * 4u);
        gamepad->state.lx = macroRecRestoreAxis(frame[0]);
        gamepad->state.ly = macroRecRestoreAxis(frame[1]);
        gamepad->state.rx = macroRecRestoreAxis(frame[2]);
        gamepad->state.ry = macroRecRestoreAxis(frame[3]);
        gamepad->state.lt = recPlayLt;
        gamepad->state.rt = recPlayRt;
        return;
    }

    MacroInput& macroInput = macro.macroInputs[macroInputPosition];

    // Only reapply stick direction if we're still within the duration window
    if ((currentMicros - macroStartTime) <= macroInput.duration) {
        applyManualAxes(gamepad->state, macroInput);
    }
}

bool InputMacro::hasStickDirection() const
{
    if (recPlayActive)
        return true;

    if (!isMacroRunning || macroPosition == -1)
        return false;

    const Macro& macro = inputMacroOptions->macroList[macroPosition];
    if (macroPosition == MACRO_REC_INDEX && macro.recordMode &&
        macro.hasRecording && recStreamValid) {
        return true;
    }

    const MacroInput& macroInput = macro.macroInputs[macroInputPosition];
    uint64_t now = getMicro();

    return (now - macroStartTime) <= macroInput.duration &&
           (macroInput.has_lx || macroInput.has_ly ||
            macroInput.has_rx || macroInput.has_ry);
}

// --- Recording ---

void InputMacro::startRecording() {
    // One-shot erase of the full 320KB before the first sample is taken.
    // Typical ~0.75s stall (5 x 64KB block erases); no samples exist yet.
    FlashPROM::eraseRange(MACRO_REC_FLASH_OFFSET, MACRO_REC_FLASH_SIZE);

    recFramesWritten = 0;
    recEventCount = 0;
    recEvtBytePos = 0;
    recEvtPagePos = 0;
    recFrmPagePos = 0;
    recLastMask = 0;
    recLastLt = 0;
    recLastRt = 0;
    recHasLastEvent = false;
    recStreamValid = false;
    recStartUs = getMicro();
    recActive = true;

    Storage::getInstance().setMacroRecording(true);
}

void InputMacro::recordWriteBytes(const uint8_t * data, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i) {
        recEvtPage[recEvtPagePos++] = data[i];
        ++recEvtBytePos;
        if (recEvtPagePos == 256)
            flushEventPage();
    }
}

void InputMacro::flushEventPage() {
    if (recEvtPagePos == 0)
        return;
    uint32_t pageLayout = MACRO_REC_EVENT_OFFSET + recEvtBytePos - recEvtPagePos;
    while (recEvtPagePos < 256)
        recEvtPage[recEvtPagePos++] = 0xFF;
    FlashPROM::programPages(recFlashOffset(pageLayout), recEvtPage, 256);
    recEvtPagePos = 0;
}

void InputMacro::flushFramePage() {
    if (recFrmPagePos == 0)
        return;
    uint32_t frameBytesUsed = recFramesWritten * 4u;
    uint32_t pageLayout = MACRO_REC_FRAME_OFFSET + frameBytesUsed - recFrmPagePos;
    while (recFrmPagePos < 256)
        recFrmPage[recFrmPagePos++] = 0xFF;
    FlashPROM::programPages(recFlashOffset(pageLayout), recFrmPage, 256);
    recFrmPagePos = 0;
}

bool InputMacro::appendRecordEvent(uint32_t frame, uint32_t mask, uint8_t lt, uint8_t rt) {
    uint8_t enc[MACRO_REC_MAX_EVENT_BYTES];
    uint8_t n1 = macroRecEncodeUVarint(frame, enc);
    uint8_t n2 = macroRecEncodeUVarint(mask, enc + n1);
    uint8_t total = n1 + n2 + 2;
    if (recEvtBytePos + total > MACRO_REC_EVENT_SIZE)
        return false;
    recordWriteBytes(enc, n1 + n2);
    recEvtPage[recEvtPagePos++] = lt;
    ++recEvtBytePos;
    if (recEvtPagePos == 256) flushEventPage();
    recEvtPage[recEvtPagePos++] = rt;
    ++recEvtBytePos;
    if (recEvtPagePos == 256) flushEventPage();
    recEventCount++;
    recLastMask = mask;
    recLastLt = lt;
    recLastRt = rt;
    recHasLastEvent = true;
    return true;
}

void InputMacro::recordSample(uint64_t now) {
    GamepadState & s = Storage::getInstance().GetGamepad()->state;

    uint32_t tick = static_cast<uint32_t>((now - recStartUs) / MACRO_REC_TICK_US);
    // All 79,872 slots (frames 0..cap-1) used means the stream is full.
    bool overflow = (tick >= MACRO_REC_FRAME_CAP);
    uint32_t targetTick = overflow ? MACRO_REC_FRAME_CAP - 1 : tick;

    // The current sample is valid at the current tick, so write its slot and
    // every slot skipped since the last sample (e.g. across a page-program
    // stall) with the same sample. The frame timeline stays dense.
    const uint8_t q[4] = {
        macroRecQuantAxis(s.lx), macroRecQuantAxis(s.ly),
        macroRecQuantAxis(s.rx), macroRecQuantAxis(s.ry)
    };
    while (recFramesWritten <= targetTick && recFramesWritten < MACRO_REC_FRAME_CAP) {
        recFrmPage[recFrmPagePos++] = q[0];
        recFrmPage[recFrmPagePos++] = q[1];
        recFrmPage[recFrmPagePos++] = q[2];
        recFrmPage[recFrmPagePos++] = q[3];
        ++recFramesWritten;
        if (recFrmPagePos == 256)
            flushFramePage();
    }

    // Change-driven button/trigger event paired with the current tick's frame.
    if (!overflow) {
        uint32_t mask = recStateMask(s);
        if (!recHasLastEvent || mask != recLastMask ||
            s.lt != recLastLt || s.rt != recLastRt) {
            if (!appendRecordEvent(targetTick, mask, s.lt, s.rt))
                overflow = true;
        }
    }

    if (overflow)
        stopRecording(true);
}

void InputMacro::postprocess(bool sent) {
    (void)sent;
    if (recActive)
        recordSample(getMicro());
}

void InputMacro::stopRecording(bool overflow) {
    if (!recActive)
        return;

    recActive = false;
    flushFramePage();
    flushEventPage();

    // Header page last: power-loss before this point leaves an invalid stream.
    uint8_t headerPage[256];
    memset(headerPage, 0xFF, sizeof(headerPage));
    MacroRecHeader hdr;
    hdr.magic[0] = 'G'; hdr.magic[1] = 'R'; hdr.magic[2] = '0'; hdr.magic[3] = '1';
    hdr.version = MACRO_REC_VERSION;
    hdr.reserved[0] = hdr.reserved[1] = hdr.reserved[2] = 0;
    hdr.totalFrames = recFramesWritten;
    hdr.eventCount = recEventCount;
    memcpy(headerPage, &hdr, sizeof(hdr));
    FlashPROM::programPages(recFlashOffset(MACRO_REC_HEADER_OFFSET), headerPage, 256);

    recStreamValid = recFramesWritten > 0;
    recHdrFrames = recFramesWritten;
    recHdrEvents = recEventCount;

    Storage::getInstance().setMacroRecording(false);
    if (overflow)
        Storage::getInstance().pulseMacroRecFull();

    // Commit metadata to config (stream itself is already on flash).
    Macro& macro = inputMacroOptions->macroList[MACRO_REC_INDEX];
    macro.recordMode = true;
    macro.enabled = true;
    macro.macroType = ON_PRESS;
    macro.exclusive = true;
    macro.interruptible = false;
    macro.macroInputs_count = 0;
    macro.hasRecording = recFramesWritten > 0;
    macro.recFrames = recFramesWritten;
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
