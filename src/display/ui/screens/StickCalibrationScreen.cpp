#include "StickCalibrationScreen.h"
#include "eventmanager.h"
#include "GPGFX_UI_screens.h"
#include "system.h"
#include "addons/analog_utils.h"
#include "MainMenuScreen.h"
#include "pico/stdlib.h"

static constexpr int kCenterSampleCount = 5;

void StickCalibrationScreen::init() {
    getRenderer()->clearScreen();
    currentState = STATE_PROMPT;
    prevButtonState = getGamepad()->state.buttons;
}

void StickCalibrationScreen::shutdown() {
    clearElements();
}

void StickCalibrationScreen::readJoystickCenter(uint8_t stickNum, uint16_t& x, uint16_t& y, int sampleCount) {
    uint32_t sumX = 0;
    uint32_t sumY = 0;
    int validSamples = 0;

    for (int i = 0; i < sampleCount; i++) {
        uint32_t sx = 0;
        uint32_t sy = 0;
        uint32_t adcMax = 0;
        if (!readJoystickADC(stickNum, sx, sy, adcMax)) {
            break;
        }
        sumX += sx;
        sumY += sy;
        validSamples++;
    }

    if (validSamples > 0) {
        x = static_cast<uint16_t>(sumX / static_cast<uint32_t>(validSamples));
        y = static_cast<uint16_t>(sumY / static_cast<uint32_t>(validSamples));
    } else {
        x = 0;
        y = 0;
    }
}

void StickCalibrationScreen::performDualStickCalibrationAndExit() {
    uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    readJoystickCenter(0, x1, y1, kCenterSampleCount);
    sleep_ms(2);
    readJoystickCenter(1, x2, y2, kCenterSampleCount);
    saveCalibrationValues(x1, y1, x2, y2);
    MainMenuScreen::flagHMLConfigRestartPending();
    MainMenuScreen::flagOpenHMLConfigMenu();
}

int8_t StickCalibrationScreen::update() {
    uint16_t buttonState = getGamepad()->state.buttons;

    bool b1Pressed = (buttonState & GAMEPAD_MASK_B1) && !(prevButtonState & GAMEPAD_MASK_B1);
    bool b2Pressed = (buttonState & GAMEPAD_MASK_B2) && !(prevButtonState & GAMEPAD_MASK_B2);

    if (b2Pressed) {
        prevButtonState = buttonState;
        MainMenuScreen::flagOpenHMLConfigMenu();
        return DisplayMode::MAIN_MENU;
    }

    prevButtonState = buttonState;

    if (currentState == STATE_SAMPLING) {
        performDualStickCalibrationAndExit();
        return DisplayMode::MAIN_MENU;
    }

    if (currentState == STATE_PROMPT && b1Pressed) {
        prevButtonState = buttonState;
        currentState = STATE_SAMPLING;
        return -1;
    }

    return -1;
}

void StickCalibrationScreen::drawScreen() {
    getRenderer()->clearScreen();

    getRenderer()->drawText(2, 0, "[Calibration]");
    if (currentState == STATE_SAMPLING) {
        getRenderer()->drawText(3, 2, "Calibrating...");
    } else {
        getRenderer()->drawText(1, 2, "Calibrate sticks?");
        getRenderer()->drawText(0, 3, "Center both sticks");
        getRenderer()->drawText(0, 5, "B1: Yes");
        getRenderer()->drawText(0, 6, "B2: Cancel");
    }
}
