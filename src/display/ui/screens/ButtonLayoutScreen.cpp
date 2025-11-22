#include "ButtonLayoutScreen.h"
#include "buttonlayouts.h"
#include "drivermanager.h"
#include "drivers/ps4/PS4Driver.h"
#include "drivers/xbone/XBOneDriver.h"
#include "drivers/xinput/XInputDriver.h"
#include "drivers/p5general/P5GeneralDriver.h"

// Display names for input history
static const char * displayNames[INPUT_HISTORY_MAX_MODES][INPUT_HISTORY_MAX_INPUTS] = {
    {        // PS3 - 0
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            CHAR_CROSS, CHAR_CIRCLE, CHAR_SQUARE, CHAR_TRIANGLE,
            "L1", "R1", "L2", "R2",
            "SL", "ST", "L3", "R3", "PS", "A2"
    },
    {        // Switch - 1
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "B", "A", "Y", "X",
            "L", "R", "ZL", "ZR",
            "-", "+", "LS", "RS", CHAR_HOME_S, CHAR_CAP_S
    },
    {        // XInput - 2
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "A", "B", "X", "Y",
            "LB", "RB", "LT", "RT",
            CHAR_VIEW_X, CHAR_MENU_X, "LS", "RS", CHAR_HOME_X, "A2"
    },
    {        // Keyboard / HID-KB - 3
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "B1", "B2", "B3", "B4",
            "L1", "R1", "L2", "R2",
            "S1", "S2", "L3", "R3", "A1", "A2"
    },
    {        // PS4/PS5 - 4
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            CHAR_CROSS, CHAR_CIRCLE, CHAR_SQUARE, CHAR_TRIANGLE,
            "L1", "R1", "L2", "R2",
            CHAR_SHARE_P, "OP", "L3", "R3", CHAR_HOME_P, CHAR_TPAD_P
    },
    {        // GEN/MD Mini - 5
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "A", "B", "X", "Y",
            "", "Z", "", "C",
            "M", "S", "", "", "", ""
    },
    {        // Neo Geo Mini - 6
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "B", "D", "A", "C",
            "", "", "", "",
            "SE", "ST", "", "", "", ""
    },
    {        // PC Engine/TG16 Mini - 7
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "I", "II", "", "",
            "", "", "", "",
            "SE", "RUN", "", "", "", ""
    },
    {        // Egret II Mini - 8
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "A", "B", "C", "D",
            "", "E", "", "F",
            "CRD", "ST", "", "", "MN", ""
    },
    {        // Astro City Mini - 9
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "A", "B", "D", "E",
            "", "C", "", "F",
            "CRD", "ST", "", "", "", ""
    },
    {        // Original Xbox - 10
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "A", "B", "X", "Y",
            "BL", "WH", "L", "R",
            "BK", "ST", "LS", "RS", "", ""
    },
    {        // HID / DINPUT - 11
            CHAR_UP, CHAR_DOWN, CHAR_LEFT, CHAR_RIGHT,
            CHAR_UL, CHAR_UR, CHAR_DL, CHAR_DR,
            "2", "3", "1", "4",
            "5", "6", "7", "8",
            "9", "10", "11", "12", "13", "14"
    }
};

void ButtonLayoutScreen::init() {
    isInputHistoryEnabled = Storage::getInstance().getDisplayOptions().inputHistoryEnabled;
    inputHistoryX = Storage::getInstance().getDisplayOptions().inputHistoryRow;
    inputHistoryY = Storage::getInstance().getDisplayOptions().inputHistoryCol;
    inputHistoryLength = Storage::getInstance().getDisplayOptions().inputHistoryLength;
    bannerDelayStart = getMillis();
    gamepad = Storage::getInstance().GetGamepad();
    inputMode = DriverManager::getInstance().getInputMode();

    EventManager::getInstance().registerEventHandler(GP_EVENT_PROFILE_CHANGE, GPEVENT_CALLBACK(this->handleProfileChange(event)));
    EventManager::getInstance().registerEventHandler(GP_EVENT_USBHOST_MOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
    EventManager::getInstance().registerEventHandler(GP_EVENT_USBHOST_UNMOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));

    footer = "";
    historyString = "";
    inputHistory.clear();

    setViewport((isInputHistoryEnabled ? 8 : 0), 0, (isInputHistoryEnabled ? 56 : getRenderer()->getDriver()->getMetrics()->height), getRenderer()->getDriver()->getMetrics()->width);

    // load layout (drawElement pushes element to the display list)
    uint16_t elementCtr = 0;
    LayoutManager::LayoutList currLayoutLeft = LayoutManager::getInstance().getLayoutA();
    LayoutManager::LayoutList currLayoutRight = LayoutManager::getInstance().getLayoutB();
    for (elementCtr = 0; elementCtr < currLayoutLeft.size(); elementCtr++) {
        pushElement(currLayoutLeft[elementCtr]);
    }
    for (elementCtr = 0; elementCtr < currLayoutRight.size(); elementCtr++) {
        pushElement(currLayoutRight[elementCtr]);
    }

    // start with profile mode displayed
    bannerDisplay = true;
    prevProfileNumber = -1;

    prevLayoutLeft = Storage::getInstance().getDisplayOptions().buttonLayout;
    prevLayoutRight = Storage::getInstance().getDisplayOptions().buttonLayoutRight;
    prevLeftOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsLeft;
    prevRightOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsRight;
    prevOrientation = Storage::getInstance().getDisplayOptions().buttonLayoutOrientation;

    // we cannot look at macro options enabled, pull the pins

    // macro display now uses our pin functions, so we need to check if pins are enabled...
    macroEnabled = false;
    hasTurboAssigned = false;
    // Macro Button initialized by void Gamepad::setup()
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
    {
        switch( pinMappings[pin].action ) {
            case GpioAction::BUTTON_PRESS_MACRO:
            case GpioAction::BUTTON_PRESS_MACRO_1:
            case GpioAction::BUTTON_PRESS_MACRO_2:
            case GpioAction::BUTTON_PRESS_MACRO_3:
            case GpioAction::BUTTON_PRESS_MACRO_4:
            case GpioAction::BUTTON_PRESS_MACRO_5:
            case GpioAction::BUTTON_PRESS_MACRO_6:
                macroEnabled = true;
                break;
            case GpioAction::BUTTON_PRESS_TURBO:
                hasTurboAssigned = true;
                break;
            default:
                break;
        }
    }

    // determine which fields will be displayed on the status bar
    showInputMode = Storage::getInstance().getDisplayOptions().inputMode;
    showTurboMode = Storage::getInstance().getDisplayOptions().turboMode && hasTurboAssigned;
    showDpadMode = Storage::getInstance().getDisplayOptions().dpadMode;
    showSocdMode = Storage::getInstance().getDisplayOptions().socdMode;
    showMacroMode = Storage::getInstance().getDisplayOptions().macroMode;
    showProfileMode = Storage::getInstance().getDisplayOptions().profileMode;

    getRenderer()->clearScreen();
}

void ButtonLayoutScreen::shutdown() {
    clearElements();

    EventManager::getInstance().unregisterEventHandler(GP_EVENT_PROFILE_CHANGE, GPEVENT_CALLBACK(this->handleProfileChange(event)));
    EventManager::getInstance().unregisterEventHandler(GP_EVENT_USBHOST_MOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
    EventManager::getInstance().unregisterEventHandler(GP_EVENT_USBHOST_UNMOUNT, GPEVENT_CALLBACK(this->handleUSB(event)));
}

int8_t ButtonLayoutScreen::update() {
    bool configMode = DriverManager::getInstance().isConfigMode();
    uint8_t profileNumber = getGamepad()->getOptions().profileNumber;

    // Check if we've updated button layouts while in config mode
    if (configMode) {
        uint8_t layoutLeft = Storage::getInstance().getDisplayOptions().buttonLayout;
        uint8_t layoutRight = Storage::getInstance().getDisplayOptions().buttonLayoutRight;
        uint8_t buttonLayoutOrientation = Storage::getInstance().getDisplayOptions().buttonLayoutOrientation;
        bool inputHistoryEnabled = Storage::getInstance().getDisplayOptions().inputHistoryEnabled;
        if ((prevLayoutLeft != layoutLeft) || (prevLayoutRight != layoutRight) || (isInputHistoryEnabled != inputHistoryEnabled) || compareCustomLayouts() || (prevOrientation != buttonLayoutOrientation)) {
            shutdown();
            init();
        }
    }

    // main logic loop
    if (prevProfileNumber != profileNumber) {
        bannerDelayStart = getMillis();
        prevProfileNumber = profileNumber;
        bannerDisplay = true;
    }

    // main logic loop
    generateHeader();
    if (isInputHistoryEnabled)
        processInputHistory();

    // check for exit/screen change
    if (DriverManager::getInstance().isConfigMode()) {
        uint16_t buttonState = getGamepad()->state.buttons;
        if (prevButtonState && !buttonState) {
            if (prevButtonState == GAMEPAD_MASK_B1) {
                prevButtonState = 0;
                return DisplayMode::CONFIG_INSTRUCTION;
            }
        }
        prevButtonState = buttonState;
    }

    return -1;
}

void ButtonLayoutScreen::generateHeader() {
    // Limit to 21 chars with 6x8 font for now
    statusBar.clear();
    Storage& storage = Storage::getInstance();

    // Display Profile # banner
    if ( bannerDisplay ) {
        if (((getMillis() - bannerDelayStart) / 1000) < bannerDelay) {
            if (bannerMessage.empty()) {
                statusBar.assign(storage.currentProfileLabel(), strlen(storage.currentProfileLabel()));
                if (statusBar.empty()) {
                    statusBar = "     Profile #";
                    statusBar +=  std::to_string(getGamepad()->getOptions().profileNumber);
                } else {
                    statusBar.insert(statusBar.begin(), (21-statusBar.length())/2, ' ');
                }
            } else {
                statusBar = bannerMessage;
            }
            return;
        } else {
            bannerDisplay = false;
            bannerMessage.clear();
        }
    }

    if (showInputMode) {
        // Display standard header
        switch (inputMode)
        {
            case INPUT_MODE_PS3:    statusBar += "PS3"; break;
            case INPUT_MODE_GENERIC: statusBar += "USBHID"; break;
            case INPUT_MODE_SWITCH: statusBar += "SWITCH"; break;
            case INPUT_MODE_MDMINI: statusBar += "GEN/MD"; break;
            case INPUT_MODE_NEOGEO: statusBar += "NGMINI"; break;
            case INPUT_MODE_PCEMINI: statusBar += "PCE/TG"; break;
            case INPUT_MODE_EGRET: statusBar += "EGRET"; break;
            case INPUT_MODE_ASTRO: statusBar += "ASTRO"; break;
            case INPUT_MODE_PSCLASSIC: statusBar += "PSC"; break;
            case INPUT_MODE_XBOXORIGINAL: statusBar += "OGXBOX"; break;
            case INPUT_MODE_SWITCH_PRO: statusBar += "SWPRO"; break;
            case INPUT_MODE_PS4:
                statusBar += "PS4";
                if(((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent() == true )
                    statusBar += ":AS";
                else
                    statusBar += "   ";
                break;
            case INPUT_MODE_PS5:
                statusBar += "PS5";
                if(((PS4Driver*)DriverManager::getInstance().getDriver())->getAuthSent() == true )
                    statusBar += ":AS";
                else
                    statusBar += "   ";
                break;
            case INPUT_MODE_P5GENERAL:
                statusBar += "P5G";
                if(((P5GeneralDriver*)DriverManager::getInstance().getDriver())->getAuthSent() == true )
                    statusBar += ":AS";
                else
                    statusBar += "   ";
                break;
            case INPUT_MODE_XBONE:
                statusBar += "XBON";
                if(((XBOneDriver*)DriverManager::getInstance().getDriver())->getAuthSent() == true )
                    statusBar += "E";
                else
                    statusBar += "*";
                break;
            case INPUT_MODE_XINPUT:
                statusBar += "X";
                if(((XInputDriver*)DriverManager::getInstance().getDriver())->getAuthSent() == true )
                    statusBar += "B360";
                else
                    statusBar += "INPUT";
                break;
            case INPUT_MODE_KEYBOARD: statusBar += "HID-KB"; break;
            case INPUT_MODE_CONFIG: statusBar += "CONFIG"; break;
        }
    }

    if (showTurboMode) {
        const TurboOptions& turboOptions = storage.getAddonOptions().turboOptions;
        if ( turboOptions.enabled ) {
            statusBar += " T";
            if ( turboOptions.shotCount < 10 ) // padding
                statusBar += "0";
            statusBar += std::to_string(turboOptions.shotCount);
        } else {
            statusBar += "    "; // no turbo, don't show Txx setting
        }
    }

    const GamepadOptions & options = gamepad->getOptions();

    if (showDpadMode) {
        switch (gamepad->getActiveDpadMode())
        {
            case DPAD_MODE_DIGITAL:      statusBar += " D"; break;
            case DPAD_MODE_LEFT_ANALOG:  statusBar += " L"; break;
            case DPAD_MODE_RIGHT_ANALOG: statusBar += " R"; break;
        }
    }

    if (showSocdMode) {
        switch (Gamepad::resolveSOCDMode(gamepad->getOptions()))
        {
            case SOCD_MODE_NEUTRAL:               statusBar += " SOCD-N"; break;
            case SOCD_MODE_UP_PRIORITY:           statusBar += " SOCD-U"; break;
            case SOCD_MODE_SECOND_INPUT_PRIORITY: statusBar += " SOCD-L"; break;
            case SOCD_MODE_FIRST_INPUT_PRIORITY:  statusBar += " SOCD-F"; break;
            case SOCD_MODE_BYPASS:                statusBar += " SOCD-X"; break;
        }
    }

    if (showMacroMode && macroEnabled) statusBar += " M";

    if (showProfileMode) {
        statusBar += " ";

        std::string profile;
        profile.assign(storage.currentProfileLabel(), strlen(storage.currentProfileLabel()));
        if (profile.empty()) {
            statusBar += std::to_string(getGamepad()->getOptions().profileNumber);
        } else {
            statusBar += profile;
        }
    }

    trim(statusBar);
}

void ButtonLayoutScreen::drawScreen() {
    if (bannerDisplay) {
        getRenderer()->drawRectangle(0, 0, 128, 7, true, true);
        getRenderer()->drawText(0, 0, statusBar, true);
    } else {
        getRenderer()->drawText(0, 0, statusBar);
    }
    getRenderer()->drawText(0, 7, footer);
}

GPLever* ButtonLayoutScreen::addLever(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor, uint16_t inputType) {
    GPLever* lever = new GPLever();
    lever->setRenderer(getRenderer());
    lever->setPosition(startX, startY);
    lever->setStrokeColor(strokeColor);
    lever->setFillColor(fillColor);
    lever->setRadius(sizeX);
    lever->setInputType(inputType);
    lever->setViewport(this->getViewport());
    return (GPLever*)addElement(lever);
}

GPButton* ButtonLayoutScreen::addButton(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor, int16_t inputMask) {
    GPButton* button = new GPButton();
    button->setRenderer(getRenderer());
    button->setPosition(startX, startY);
    button->setStrokeColor(strokeColor);
    button->setFillColor(fillColor);
    button->setSize(sizeX, sizeY);
    button->setInputMask(inputMask);
    button->setViewport(this->getViewport());
    return (GPButton*)addElement(button);
}

GPShape* ButtonLayoutScreen::addShape(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY, uint16_t strokeColor, uint16_t fillColor) {
    GPShape* shape = new GPShape();
    shape->setRenderer(getRenderer());
    shape->setPosition(startX, startY);
    shape->setStrokeColor(strokeColor);
    shape->setFillColor(fillColor);
    shape->setSize(sizeX,sizeY);
    shape->setViewport(this->getViewport());
    return (GPShape*)addElement(shape);
}

GPSprite* ButtonLayoutScreen::addSprite(uint16_t startX, uint16_t startY, uint16_t sizeX, uint16_t sizeY) {
    GPSprite* sprite = new GPSprite();
    sprite->setRenderer(getRenderer());
    sprite->setPosition(startX, startY);
    sprite->setSize(sizeX,sizeY);
    sprite->setViewport(this->getViewport());
    return (GPSprite*)addElement(sprite);
}

GPWidget* ButtonLayoutScreen::pushElement(GPButtonLayout element) {
    if (element.elementType == GP_ELEMENT_LEVER) {
        return addLever(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill, element.parameters.value);
    } else if ((element.elementType == GP_ELEMENT_BTN_BUTTON) || (element.elementType == GP_ELEMENT_DIR_BUTTON) || (element.elementType == GP_ELEMENT_PIN_BUTTON)) {
        GPButton* button = addButton(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill, element.parameters.value);

        // set type of button
        button->setInputType(element.elementType);
        button->setInputDirection(false);
        button->setShape((GPShape_Type)element.parameters.shape);
        button->setAngle(element.parameters.angleStart);
        button->setAngleEnd(element.parameters.angleEnd);
        button->setClosed(element.parameters.closed);

        if (element.elementType == GP_ELEMENT_DIR_BUTTON) button->setInputDirection(true);

        return (GPWidget*)button;
    } else if (element.elementType == GP_ELEMENT_SPRITE) {
        return addSprite(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2);
    } else if (element.elementType == GP_ELEMENT_SHAPE) {
        GPShape* shape = addShape(element.parameters.x1, element.parameters.y1, element.parameters.x2, element.parameters.y2, element.parameters.stroke, element.parameters.fill);
        shape->setShape((GPShape_Type)element.parameters.shape);
        shape->setAngle(element.parameters.angleStart);
        shape->setAngleEnd(element.parameters.angleEnd);
        shape->setClosed(element.parameters.closed);
        return shape;
    }
    return NULL;
}

void ButtonLayoutScreen::processInputHistory() {
    std::deque<std::string> pressed;

    // Get key states
    std::array<bool, INPUT_HISTORY_MAX_INPUTS> currentInput = {

        pressedUp(),
        pressedDown(),
        pressedLeft(),
        pressedRight(),

        pressedUpLeft(),
        pressedUpRight(),
        pressedDownLeft(),
        pressedDownRight(),

        getProcessedGamepad()->pressedB1(),
        getProcessedGamepad()->pressedB2(),
        getProcessedGamepad()->pressedB3(),
        getProcessedGamepad()->pressedB4(),
        getProcessedGamepad()->pressedL1(),
        getProcessedGamepad()->pressedR1(),
        getProcessedGamepad()->pressedL2(),
        getProcessedGamepad()->pressedR2(),
        getProcessedGamepad()->pressedS1(),
        getProcessedGamepad()->pressedS2(),
        getProcessedGamepad()->pressedL3(),
        getProcessedGamepad()->pressedR3(),
        getProcessedGamepad()->pressedA1(),
        getProcessedGamepad()->pressedA2(),
    };

    uint8_t mode = ((displayModeLookup.count(inputMode) > 0) ? displayModeLookup.at(inputMode) : 0);

    // Check if any new keys have been pressed
    if (lastInput != currentInput) {
        // Iterate through array
        for (uint8_t x=0; x<INPUT_HISTORY_MAX_INPUTS; x++) {
            // Add any pressed keys to deque
            std::string inputChar(displayNames[mode][x]);
            if (currentInput[x] && (inputChar != "")) pressed.push_back(inputChar);
        }
        // Update the last keypress array
        lastInput = currentInput;
    }

    if (pressed.size() > 0) {
        std::string newInput;
        for(const auto &s : pressed) {
                if(!newInput.empty())
                        newInput += "+";
                newInput += s;
        }

        inputHistory.push_back(newInput);
    }

    if (inputHistory.size() > (inputHistoryLength / 2) + 1) {
        inputHistory.pop_front();
    }

    std::string ret;

    for (auto it = inputHistory.crbegin(); it != inputHistory.crend(); ++it) {
        std::string newRet = ret;
        if (!newRet.empty())
            newRet = " " + newRet;

        newRet = *it + newRet;
        ret = newRet;

        if (ret.size() >= inputHistoryLength) {
            break;
        }
    }

    if(ret.size() >= inputHistoryLength) {
        historyString = ret.substr(ret.size() - inputHistoryLength);
    } else {
        historyString = ret;
    }

    footer = historyString;
}

bool ButtonLayoutScreen::compareCustomLayouts()
{
    ButtonLayoutParamsLeft leftOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsLeft;
    ButtonLayoutParamsRight rightOptions = Storage::getInstance().getDisplayOptions().buttonLayoutCustomOptions.paramsRight;

    bool leftChanged = ((leftOptions.layout != prevLeftOptions.layout) || (leftOptions.common.startX != prevLeftOptions.common.startX) || (leftOptions.common.startY != prevLeftOptions.common.startY) || (leftOptions.common.buttonPadding != prevLeftOptions.common.buttonPadding) || (leftOptions.common.buttonRadius != prevLeftOptions.common.buttonRadius));
    bool rightChanged = ((rightOptions.layout != prevRightOptions.layout) || (rightOptions.common.startX != prevRightOptions.common.startX) || (rightOptions.common.startY != prevRightOptions.common.startY) || (rightOptions.common.buttonPadding != prevRightOptions.common.buttonPadding) || (rightOptions.common.buttonRadius != prevRightOptions.common.buttonRadius));

    return (leftChanged || rightChanged);
}

bool ButtonLayoutScreen::pressedUp()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_UP);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MIN;
    }

    return false;
}

bool ButtonLayoutScreen::pressedDown()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_DOWN);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MAX;
    }

    return false;
}

bool ButtonLayoutScreen::pressedLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_LEFT);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MIN;
    }

    return false;
}

bool ButtonLayoutScreen::pressedRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == GAMEPAD_MASK_RIGHT);
        case DPAD_MODE_LEFT_ANALOG:  return getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX;
        case DPAD_MODE_RIGHT_ANALOG: return getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MAX;
    }

    return false;
}

bool ButtonLayoutScreen::pressedUpLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.rx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ry == GAMEPAD_JOYSTICK_MIN);
    }

    return false;
}

bool ButtonLayoutScreen::pressedUpRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MIN);
    }

    return false;
}

bool ButtonLayoutScreen::pressedDownLeft()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MIN) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
    }

    return false;
}

bool ButtonLayoutScreen::pressedDownRight()
{
    switch (getGamepad()->getActiveDpadMode())
    {
        case DPAD_MODE_DIGITAL:      return ((getProcessedGamepad()->state.dpad & GAMEPAD_MASK_DPAD) == (GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT));
        case DPAD_MODE_LEFT_ANALOG:  return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
        case DPAD_MODE_RIGHT_ANALOG: return (getProcessedGamepad()->state.lx == GAMEPAD_JOYSTICK_MAX) && (getProcessedGamepad()->state.ly == GAMEPAD_JOYSTICK_MAX);
    }

    return false;
}

void ButtonLayoutScreen::handleProfileChange(GPEvent* e) {
    GPProfileChangeEvent* event = (GPProfileChangeEvent*)e;

    profileNumber = event->currentValue;
    prevProfileNumber = event->previousValue;
}

void ButtonLayoutScreen::handleUSB(GPEvent* e) {
    GPUSBHostEvent* event = (GPUSBHostEvent*)e;
    bannerDelayStart = getMillis();
    prevProfileNumber = profileNumber;

    if (e->eventType() == GP_EVENT_USBHOST_MOUNT) {
        bannerMessage = "    USB Connected";
    } else if (e->eventType() == GP_EVENT_USBHOST_UNMOUNT) {
        bannerMessage = "  USB Disconnnected";
    }
    bannerDisplay = true;
}

void ButtonLayoutScreen::trim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            std::not1(std::ptr_fun<int, int>(std::isspace))));
}