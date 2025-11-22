#include "APMTestScreen.h"

#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"
#include "MainMenuScreen.h"

#include <cstdio>
#include <cstdlib>

extern uint32_t getMillis();

void APMTestScreen::init() {
    getRenderer()->clearScreen();

    exitToScreen = -1;
    timeRemaining = 10.0f;
    buttonCount = 0;
    isRunning = false;
    startTime = 0;
    lastUpdateTime = getMillis();

    if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
    if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }

    mapMenuSelect = new GamepadButtonMapping(GAMEPAD_MASK_B1);
    mapMenuBack = new GamepadButtonMapping(GAMEPAD_MASK_B2);

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        switch (pinMappings[pin].action) {
            case GpioAction::MENU_NAVIGATION_SELECT: mapMenuSelect->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_BACK: mapMenuBack->pinMask |= 1 << pin; break;
            default: break;
        }
    }

    resetInputState();
    isReady = true;
}

void APMTestScreen::resetInputState() {
    prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;
    prevButtonState = getGamepad()->state.buttons;
    prevDpadState = getGamepad()->state.dpad;
}

void APMTestScreen::shutdown() {
    clearElements();
    isReady = false;

    if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
    if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }
}

void APMTestScreen::resetTest() {
    timeRemaining = 10.0f;
    buttonCount = 0;
    isRunning = false;
    startTime = 0;
    lastUpdateTime = getMillis();
}

void APMTestScreen::handleButtonPress() {
    if (!isRunning) {
        // First button press - start the countdown
        isRunning = true;
        startTime = getMillis();
        lastUpdateTime = startTime;
    }

    // Only count if timer is still running
    if (isRunning && timeRemaining > 0.0f) {
        buttonCount++;
    }
}

int8_t APMTestScreen::update() {
    if (!isReady) {
        int8_t result = exitToScreen;
        exitToScreen = -1;
        return result;
    }

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;
    uint16_t buttonState = getGamepad()->state.buttons;
    uint8_t dpadState = getGamepad()->state.dpad;

    // Handle B1 (Reset) and B2 (Cancel) - check for button press (not release)
    if (prevButtonState != buttonState && mapMenuSelect && mapMenuBack) {
        uint16_t pressedButtons = buttonState & ~prevButtonState; // Newly pressed buttons
        if (pressedButtons & mapMenuSelect->buttonMask) {
            resetTest();
            prevButtonState = buttonState;
            prevDpadState = dpadState;
            prevValues = values;
            return -1;
        } else if (pressedButtons & mapMenuBack->buttonMask) {
            // B2 pressed - exit to HML Config menu
            MainMenuScreen::flagOpenHMLConfigMenu();
            exitToScreen = DisplayMode::MAIN_MENU;
            prevButtonState = buttonState;
            prevDpadState = dpadState;
            prevValues = values;
            int8_t result = exitToScreen;
            exitToScreen = -1;
            return result;
        }
    }

    // Handle GPIO button presses for B1/B2
    if (prevValues != values && mapMenuSelect && mapMenuBack) {
        Mask_t pressedPins = values & ~prevValues; // Newly pressed pins
        if (pressedPins & mapMenuSelect->pinMask) {
            resetTest();
            prevButtonState = buttonState;
            prevDpadState = dpadState;
            prevValues = values;
            return -1;
        } else if (pressedPins & mapMenuBack->pinMask) {
            MainMenuScreen::flagOpenHMLConfigMenu();
            exitToScreen = DisplayMode::MAIN_MENU;
            prevButtonState = buttonState;
            prevDpadState = dpadState;
            prevValues = values;
            int8_t result = exitToScreen;
            exitToScreen = -1;
            return result;
        }
    }

    // Handle button presses for counting (all buttons except B1/B2)
    if (mapMenuSelect && mapMenuBack) {
        if (gamepadOptions.miniMenuGamepadInput) {
            // Check for D-pad presses
            if (prevDpadState != dpadState && dpadState != 0) {
                handleButtonPress();
            }
            // Check for button presses (except B1/B2)
            if (prevButtonState != buttonState) {
                uint16_t pressedButtons = buttonState & ~prevButtonState;
                if (pressedButtons & ~mapMenuSelect->buttonMask & ~mapMenuBack->buttonMask) {
                    handleButtonPress();
                }
            }
        } else {
            // GPIO input mode - check for any button press (except B1/B2)
            if (prevValues != values) {
                Mask_t pressedPins = values & ~prevValues;
                if (pressedPins & ~mapMenuSelect->pinMask & ~mapMenuBack->pinMask) {
                    handleButtonPress();
                }
            }
        }
    }

    // Update countdown timer
    if (isRunning && timeRemaining > 0.0f) {
        uint32_t currentTime = getMillis();
        uint32_t elapsed = currentTime - lastUpdateTime;
        lastUpdateTime = currentTime;

        float elapsedSeconds = elapsed / 1000.0f;
        timeRemaining -= elapsedSeconds;

        if (timeRemaining <= 0.0f) {
            timeRemaining = 0.0f;
            isRunning = false;
        }
    }

    prevButtonState = buttonState;
    prevDpadState = dpadState;
    prevValues = values;

    int8_t result = exitToScreen;
    if (result != -1) {
        exitToScreen = -1;
    }
    return result;
}

void APMTestScreen::drawScreen() {
    // Title: [APM Test] - centered on line 0
    std::string title = "[APM Test]";
    uint16_t titleX = (21 - title.length()) / 2;
    getRenderer()->drawText(titleX, 0, title.c_str());

    // Empty line (line 1) - already empty

    // Time: XX.XX sec - left aligned on line 2
    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "Time: %.2f sec", timeRemaining);
    getRenderer()->drawText(0, 2, timeStr);

    // Your Score Is: XXXX - left aligned on line 3
    char scoreStr[32];
    snprintf(scoreStr, sizeof(scoreStr), "Your Score Is: %u", (unsigned int)buttonCount);
    getRenderer()->drawText(0, 3, scoreStr);

    // B1: Reset - left aligned on second to last line (line 6)
    getRenderer()->drawText(0, 6, "B1: Reset");

    // B2: Cancel - left aligned on last line (line 7)
    getRenderer()->drawText(0, 7, "B2: Cancel");
}

