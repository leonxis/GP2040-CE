#include "PinViewerScreen.h"

#include "pico/stdlib.h"
#include "drivermanager.h"
#include "BoardConfig.h"

void PinViewerScreen::init() {
    getRenderer()->clearScreen();
}

void PinViewerScreen::shutdown() {
    clearElements();
}

void PinViewerScreen::drawScreen() {
    // 按下态：低有效引脚低电平=按下，高有效引脚高电平=按下（BUTTON_ACTIVE_HIGH_MASK）
#ifdef BUTTON_ACTIVE_HIGH_MASK
    const Mask_t activeHighMask = (Mask_t)BUTTON_ACTIVE_HIGH_MASK;
#else
    const Mask_t activeHighMask = 0;
#endif
    const Mask_t gpioState = gpio_get_all64();
    const Mask_t pinValues = (~gpioState & ~activeHighMask)
                           |  (gpioState &  activeHighMask);
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();

    std::string pinsPressed = "GPIO Pin : ";
    std::string pinsInUse = "In Use   : ";
    std::string pinsUndefined = "Undefined: ";
    std::string buttonsPressed = "Button   : ";

    getRenderer()->drawText(2, 0, "[GPIO Pin Viewer]");

    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        if ((pinMappings[pin].action > 0) && (pinMappings[pin].action != GpioAction::CUSTOM_BUTTON_COMBO)) {
            if ((pinValues >> pin) & 0x01) {
                pinsPressed += std::to_string(pin);
                pinsPressed += " ";

                const char * action = buttonLookup[pinMappings[pin].action];
                buttonsPressed += action;
                buttonsPressed += " ";
            }
        } else {
            if (pinMappings[pin].action != GpioAction::NONE) {
                pinsInUse += std::to_string(pin);
                pinsInUse += " ";
            } else {
                pinsUndefined += std::to_string(pin);
                pinsUndefined += " ";
            }
        }
    }

    getRenderer()->drawText(0, 2, pinsUndefined.c_str());
    getRenderer()->drawText(0, 3, pinsInUse.c_str());
    getRenderer()->drawText(0, 4, pinsPressed.c_str());
    getRenderer()->drawText(0, 5, buttonsPressed.c_str());

    getRenderer()->drawText(4, 7, "A2 to Return");
}

int8_t PinViewerScreen::update() {
    if (DriverManager::getInstance().isConfigMode()) {
        uint16_t buttonState = getGamepad()->state.buttons;
        if (prevButtonState && !buttonState) {
            if (prevButtonState == GAMEPAD_MASK_A2) {
                prevButtonState = 0;
                return DisplayMode::CONFIG_INSTRUCTION;
            }
        }
        prevButtonState = buttonState;
    }

    return -1; // -1 means no change in screen state
}
