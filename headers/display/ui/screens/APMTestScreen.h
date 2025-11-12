#ifndef _APMTESTSCREEN_H_
#define _APMTESTSCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "GamepadState.h"
#include "enums.pb.h"

class GamepadButtonMapping;

class APMTestScreen : public GPScreen {
public:
    APMTestScreen() {}
    APMTestScreen(GPGFX* renderer) { setRenderer(renderer); }
    virtual ~APMTestScreen() {}

    virtual void init();
    virtual void shutdown();
    virtual int8_t update();

protected:
    virtual void drawScreen();

private:
    void resetInputState();
    void updateNavigation(GpioAction action);
    void resetTest();
    void handleButtonPress();

    GamepadButtonMapping* mapMenuSelect = nullptr;
    GamepadButtonMapping* mapMenuBack = nullptr;

    Mask_t prevValues = 0;
    uint16_t prevButtonState = 0;
    uint8_t prevDpadState = 0;

    int8_t exitToScreen = -1;
    bool isReady = false;

    // APM Test state
    float timeRemaining = 10.0f;  // 10 seconds countdown
    uint32_t buttonCount = 0;      // Number of button presses
    bool isRunning = false;        // Whether countdown is running
    uint32_t startTime = 0;        // Start time in milliseconds
    uint32_t lastUpdateTime = 0;   // Last update time for countdown
};

#endif

