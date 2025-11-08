#include "StickCalibrationScreen.h"
#include "eventmanager.h"
#include "GPGFX_UI_screens.h"
#include "system.h"

void StickCalibrationScreen::init() {
    getRenderer()->clearScreen();
    currentState = STATE_STICK1_TOP_LEFT;
    
    // Initialize prevButtonState with current button state to prevent immediate button press detection
    // when entering this screen with a button still pressed
    prevButtonState = getGamepad()->state.buttons;
    
    // Initialize calibration values
    for (int i = 0; i < 8; i++) {
        calibrationValues[i] = 0;
        calibrationValues2[i] = 0;
    }
}

void StickCalibrationScreen::shutdown() {
    clearElements();
}

void StickCalibrationScreen::readJoystickCenter(uint8_t stickNum, uint16_t& x, uint16_t& y) {
    const AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    x = 0;
    y = 0;
    
    if (!analogOptions.enabled) {
        return;
    }
    
    adc_init();
    
    Pin_t xPin = (stickNum == 0) ? analogOptions.analogAdc1PinX : analogOptions.analogAdc2PinX;
    Pin_t yPin = (stickNum == 0) ? analogOptions.analogAdc1PinY : analogOptions.analogAdc2PinY;
    
    const uint8_t ADC_PIN_OFFSET = 26;
    if (isValidPin(xPin)) {
        adc_gpio_init(xPin);
        adc_select_input(xPin - ADC_PIN_OFFSET);
        x = adc_read();
    }
    
    if (isValidPin(yPin)) {
        adc_gpio_init(yPin);
        adc_select_input(yPin - ADC_PIN_OFFSET);
        y = adc_read();
    }
}

void StickCalibrationScreen::getStateInfo(const char*& stickLabel, const char*& direction) {
    switch (currentState) {
        case STATE_STICK1_TOP_LEFT:
            stickLabel = "Stick Left";
            direction = "Top-Left";
            break;
        case STATE_STICK1_TOP_RIGHT:
            stickLabel = "Stick Left";
            direction = "Top-Right";
            break;
        case STATE_STICK1_BOTTOM_LEFT:
            stickLabel = "Stick Left";
            direction = "Bottom-Left";
            break;
        case STATE_STICK1_BOTTOM_RIGHT:
            stickLabel = "Stick Left";
            direction = "Bottom-Right";
            break;
        case STATE_STICK2_TOP_LEFT:
            stickLabel = "Stick Right";
            direction = "Top-Left";
            break;
        case STATE_STICK2_TOP_RIGHT:
            stickLabel = "Stick Right";
            direction = "Top-Right";
            break;
        case STATE_STICK2_BOTTOM_LEFT:
            stickLabel = "Stick Right";
            direction = "Bottom-Left";
            break;
        case STATE_STICK2_BOTTOM_RIGHT:
            stickLabel = "Stick Right";
            direction = "Bottom-Right";
            break;
        default:
            stickLabel = "";
            direction = "";
            break;
    }
}

void StickCalibrationScreen::saveCalibration() {
    // Calculate average center values for stick 1
    uint32_t avgX1 = (calibrationValues[0] + calibrationValues[2] + 
                       calibrationValues[4] + calibrationValues[6]) / 4;
    uint32_t avgY1 = (calibrationValues[1] + calibrationValues[3] + 
                       calibrationValues[5] + calibrationValues[7]) / 4;
    
    // Calculate average center values for stick 2
    uint32_t avgX2 = (calibrationValues2[0] + calibrationValues2[2] + 
                      calibrationValues2[4] + calibrationValues2[6]) / 4;
    uint32_t avgY2 = (calibrationValues2[1] + calibrationValues2[3] + 
                      calibrationValues2[5] + calibrationValues2[7]) / 4;
    
    // Save to storage
    AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;
    analogOptions.joystick_center_x = avgX1;
    analogOptions.joystick_center_y = avgY1;
    analogOptions.joystick_center_x2 = avgX2;
    analogOptions.joystick_center_y2 = avgY2;
    
    // Disable auto calibration to use manual values
    analogOptions.auto_calibrate = 0;
    analogOptions.auto_calibrate2 = 0;
    
    // Save to flash
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
}

int8_t StickCalibrationScreen::update() {
    uint16_t buttonState = getGamepad()->state.buttons;
    
    // Detect button press (B1 - MENU_NAVIGATION_SELECT)
    bool b1Pressed = (buttonState & GAMEPAD_MASK_B1) && !(prevButtonState & GAMEPAD_MASK_B1);
    bool b2Pressed = (buttonState & GAMEPAD_MASK_B2) && !(prevButtonState & GAMEPAD_MASK_B2);
    
    // Handle B2 (back/cancel)
    if (b2Pressed) {
        prevButtonState = buttonState;
        return DisplayMode::MAIN_MENU;
    }
    
    prevButtonState = buttonState;
    
    if (currentState == STATE_COMPLETE) {
        // Wait for B1 press to confirm restart
        if (b1Pressed) {
            // Update prevButtonState before restarting to prevent button press from being detected
            // by other screens after restart
            prevButtonState = buttonState;
            // Trigger restart to apply calibration values immediately
            // This ensures the new calibration values are loaded from storage and applied on boot
            EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
        }
        return -1;
    }
    
    if (b1Pressed) {
        // Read current joystick center
        uint16_t x = 0, y = 0;
        uint8_t stickNum = (currentState < STATE_STICK2_TOP_LEFT) ? 0 : 1;
        readJoystickCenter(stickNum, x, y);
        
        // Store calibration value based on current state
        int idx = -1;
        switch (currentState) {
            case STATE_STICK1_TOP_LEFT:
                idx = 0;
                break;
            case STATE_STICK1_TOP_RIGHT:
                idx = 2;
                break;
            case STATE_STICK1_BOTTOM_LEFT:
                idx = 4;
                break;
            case STATE_STICK1_BOTTOM_RIGHT:
                idx = 6;
                break;
            case STATE_STICK2_TOP_LEFT:
                idx = 0;
                break;
            case STATE_STICK2_TOP_RIGHT:
                idx = 2;
                break;
            case STATE_STICK2_BOTTOM_LEFT:
                idx = 4;
                break;
            case STATE_STICK2_BOTTOM_RIGHT:
                idx = 6;
                break;
            default:
                break;
        }
        
        if (idx >= 0) {
            if (stickNum == 0) {
                calibrationValues[idx] = x;
                calibrationValues[idx + 1] = y;
            } else {
                calibrationValues2[idx] = x;
                calibrationValues2[idx + 1] = y;
            }
        }
        
        // Move to next state
        switch (currentState) {
            case STATE_STICK1_TOP_LEFT:
                currentState = STATE_STICK1_TOP_RIGHT;
                break;
            case STATE_STICK1_TOP_RIGHT:
                currentState = STATE_STICK1_BOTTOM_LEFT;
                break;
            case STATE_STICK1_BOTTOM_LEFT:
                currentState = STATE_STICK1_BOTTOM_RIGHT;
                break;
            case STATE_STICK1_BOTTOM_RIGHT:
                currentState = STATE_STICK2_TOP_LEFT;
                break;
            case STATE_STICK2_TOP_LEFT:
                currentState = STATE_STICK2_TOP_RIGHT;
                break;
            case STATE_STICK2_TOP_RIGHT:
                currentState = STATE_STICK2_BOTTOM_LEFT;
                break;
            case STATE_STICK2_BOTTOM_LEFT:
                currentState = STATE_STICK2_BOTTOM_RIGHT;
                break;
            case STATE_STICK2_BOTTOM_RIGHT:
                saveCalibration();
                currentState = STATE_COMPLETE;
                // Wait for user to press B1 before restarting
                break;
            default:
                break;
        }
    }
    
    return -1;
}

void StickCalibrationScreen::drawScreen() {
    getRenderer()->clearScreen();
    
    if (currentState == STATE_COMPLETE) {
        // Completion screen
        getRenderer()->drawText(2, 0, "[Calibration]");
        getRenderer()->drawText(4, 1, "Complete!");
        getRenderer()->drawText(3, 4, "Press B1 to");
        getRenderer()->drawText(5, 5, "restart");
    } else {
        // Calibration in progress
        const char* stickLabel;
        const char* direction;
        getStateInfo(stickLabel, direction);
        
        // Calculate current step (1-4 for each stick)
        uint8_t step = 0;
        switch (currentState) {
            case STATE_STICK1_TOP_LEFT:
                step = 1;
                break;
            case STATE_STICK1_TOP_RIGHT:
                step = 2;
                break;
            case STATE_STICK1_BOTTOM_LEFT:
                step = 3;
                break;
            case STATE_STICK1_BOTTOM_RIGHT:
                step = 4;
                break;
            case STATE_STICK2_TOP_LEFT:
                step = 1;
                break;
            case STATE_STICK2_TOP_RIGHT:
                step = 2;
                break;
            case STATE_STICK2_BOTTOM_LEFT:
                step = 3;
                break;
            case STATE_STICK2_BOTTOM_RIGHT:
                step = 4;
                break;
            default:
                step = 0;
                break;
        }
        
        // Header
        getRenderer()->drawText(2, 0, "[Calibration]");
        
        // Stick and step info
        char stickStepMsg[32];
        snprintf(stickStepMsg, sizeof(stickStepMsg), "%s Step %d/4", stickLabel, step);
        getRenderer()->drawText(0, 1, stickStepMsg);
        
        // Direction instruction (with empty line before it)
        char directionMsg[32];
        snprintf(directionMsg, sizeof(directionMsg), "Move to %s", direction);
        getRenderer()->drawText(0, 3, directionMsg);
        
        // Instructions (at bottom of screen)
        getRenderer()->drawText(0, 6, "B1: Confirm");
        
        // Cancel option (at bottom of screen)
        getRenderer()->drawText(0, 7, "B2: Cancel");
    }
}

