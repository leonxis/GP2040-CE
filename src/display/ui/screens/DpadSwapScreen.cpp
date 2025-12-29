#include "DpadSwapScreen.h"

#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "GPStorageSaveEvent.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"
#include "MainMenuScreen.h"

#include <algorithm>
#include <cstdio>
#include <functional>

void DpadSwapScreen::init() {
    getRenderer()->clearScreen();

    currentState = State::SELECT_MODE;
    changesPending = false;
    exitToScreen = -1;

    if (gpMenu == nullptr) {
        gpMenu = new GPMenu();
        gpMenu->setRenderer(getRenderer());
        gpMenu->setPosition(8, 16);
        gpMenu->setMenuSize(18, menuLineSize);
        addElement(gpMenu);
    } else {
        gpMenu->setRenderer(getRenderer());
        gpMenu->setMenuSize(18, menuLineSize);
    }

    if (mapMenuUp) { delete mapMenuUp; mapMenuUp = nullptr; }
    if (mapMenuDown) { delete mapMenuDown; mapMenuDown = nullptr; }
    if (mapMenuLeft) { delete mapMenuLeft; mapMenuLeft = nullptr; }
    if (mapMenuRight) { delete mapMenuRight; mapMenuRight = nullptr; }
    if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
    if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }

    mapMenuUp = new GamepadButtonMapping(GAMEPAD_MASK_UP);
    mapMenuDown = new GamepadButtonMapping(GAMEPAD_MASK_DOWN);
    mapMenuLeft = new GamepadButtonMapping(GAMEPAD_MASK_LEFT);
    mapMenuRight = new GamepadButtonMapping(GAMEPAD_MASK_RIGHT);
    mapMenuSelect = new GamepadButtonMapping(GAMEPAD_MASK_B1);
    mapMenuBack = new GamepadButtonMapping(GAMEPAD_MASK_B2);

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        switch (pinMappings[pin].action) {
            case GpioAction::MENU_NAVIGATION_UP: mapMenuUp->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_DOWN: mapMenuDown->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_LEFT: mapMenuLeft->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_RIGHT: mapMenuRight->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_SELECT: mapMenuSelect->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_BACK: mapMenuBack->pinMask |= 1 << pin; break;
            default: break;
        }
    }

    buildMenus();

    currentMenu = &modeSelectionMenu;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Dpad Swap]");
    gpMenu->setIndex(0);
    gpMenu->setVisibility(true);

    resetInputState();
    isMenuReady = true;
}

void DpadSwapScreen::buildMenus() {
    modeSelectionMenu.clear();
    modeSelectionMenu.push_back({"Dpad", nullptr, nullptr,
        std::bind(&DpadSwapScreen::currentMode, this), std::bind(&DpadSwapScreen::enterEdit, this, 0), 0});
    modeSelectionMenu.push_back({"LeftJoystick", nullptr, nullptr,
        std::bind(&DpadSwapScreen::currentMode, this), std::bind(&DpadSwapScreen::enterEdit, this, 1), 1});
    modeSelectionMenu.push_back({"RightJoystick", nullptr, nullptr,
        std::bind(&DpadSwapScreen::currentMode, this), std::bind(&DpadSwapScreen::enterEdit, this, 2), 2});
}

void DpadSwapScreen::resetInputState() {
    prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;
    prevButtonState = getGamepad()->state.buttons;
    prevDpadState = getGamepad()->state.dpad;
}

void DpadSwapScreen::shutdown() {
    clearElements();
    isMenuReady = false;
    currentMenu = nullptr;
    gpMenu = nullptr;

    if (mapMenuUp) { delete mapMenuUp; mapMenuUp = nullptr; }
    if (mapMenuDown) { delete mapMenuDown; mapMenuDown = nullptr; }
    if (mapMenuLeft) { delete mapMenuLeft; mapMenuLeft = nullptr; }
    if (mapMenuRight) { delete mapMenuRight; mapMenuRight = nullptr; }
    if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
    if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }
}

int8_t DpadSwapScreen::update() {
    if (!isMenuReady) {
        int8_t result = exitToScreen;
        exitToScreen = -1;
        return result;
    }

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;
    uint16_t buttonState = getGamepad()->state.buttons;
    uint8_t dpadState = getGamepad()->state.dpad;

    auto dispatchAction = [&](GpioAction action) {
        switch (currentState) {
            case State::SELECT_MODE: updateMenuNavigation(action); break;
            case State::EDIT_VALUES: updateEditNavigation(action); break;
        }
    };

    if (prevValues != values) {
        if (values & mapMenuUp->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
        else if (values & mapMenuDown->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
        else if (values & mapMenuLeft->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
        else if (values & mapMenuRight->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
        else if (values & mapMenuSelect->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
        else if (values & mapMenuBack->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
    }

    if (gamepadOptions.miniMenuGamepadInput) {
        if (prevDpadState != dpadState) {
            if (dpadState == mapMenuUp->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
            else if (dpadState == mapMenuDown->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
            else if (dpadState == mapMenuLeft->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
            else if (dpadState == mapMenuRight->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
        }
        if (prevButtonState != buttonState) {
            if (buttonState == mapMenuSelect->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
            else if (buttonState == mapMenuBack->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
        }
    }

    prevValues = values;
    prevButtonState = buttonState;
    prevDpadState = dpadState;

    if (exitToScreen != -1) {
        int8_t result = exitToScreen;
        exitToScreen = -1;
        return result;
    }

    return -1;
}

void DpadSwapScreen::drawScreen() {
    if (currentState == State::SELECT_MODE) {
        if (gpMenu) gpMenu->setVisibility(true);
        return;
    }

    getRenderer()->clearScreen();
    if (gpMenu) gpMenu->setVisibility(false);

    const char* modeLabel = "";
    switch (editingMode) {
        case 0: modeLabel = "Dpad"; break;
        case 1: modeLabel = "LeftJoystick"; break;
        case 2: modeLabel = "RightJoystick"; break;
    }
    getRenderer()->drawText(3, 0, "[Dpad Swap]");
    getRenderer()->drawText(2, 1, modeLabel);

    struct RowInfo {
        const char* label;
        int value;
    };

    RowInfo rows[2] = {
        {"angle", angleValue},
        {"deadzone", deadzoneValue},
    };

    for (int i = 0; i < 2; i++) {
        int y = 3 + (i * 2);
        getRenderer()->drawText(2, y, rows[i].label);

        char valueText[6];
        snprintf(valueText, sizeof(valueText), "%2d", rows[i].value);

        if (selectedRow == i) {
            getRenderer()->drawText(10, y, CHAR_LEFT);
        }
        getRenderer()->drawText(12, y, valueText);
        if (selectedRow == i) {
            getRenderer()->drawText(15, y, CHAR_RIGHT);
        }
    }
}

void DpadSwapScreen::updateMenuNavigation(GpioAction action) {
    if (!gpMenu || currentMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = currentMenu->size();

    switch (action) {
        case GpioAction::MENU_NAVIGATION_UP:
            if (menuSize == 0) break;
            if (menuIndex == 0) gpMenu->setIndex(menuSize - 1);
            else gpMenu->setIndex(menuIndex - 1);
            break;
        case GpioAction::MENU_NAVIGATION_DOWN:
            if (menuSize == 0) break;
            if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
            else gpMenu->setIndex(0);
            break;
        case GpioAction::MENU_NAVIGATION_SELECT:
            if (menuSize == 0) break;
            // Set dpadMode immediately when selecting a menu item
            setDpadMode(gpMenu->getIndex());
            enterEdit(gpMenu->getIndex());
            break;
        case GpioAction::MENU_NAVIGATION_BACK:
            // Return to HML Config menu instead of main menu
            MainMenuScreen::flagOpenHMLConfigMenu();
            exitToScreen = DisplayMode::MAIN_MENU;
            isMenuReady = false;
            break;
        default:
            break;
    }
}

void DpadSwapScreen::updateEditNavigation(GpioAction action) {
    switch (action) {
        case GpioAction::MENU_NAVIGATION_UP:
            selectedRow = (selectedRow + 1) % 2;
            break;
        case GpioAction::MENU_NAVIGATION_DOWN:
            selectedRow = (selectedRow + 1) % 2;
            break;
        case GpioAction::MENU_NAVIGATION_LEFT:
            adjustCurrentValue(-1);
            break;
        case GpioAction::MENU_NAVIGATION_RIGHT:
            adjustCurrentValue(1);
            break;
        case GpioAction::MENU_NAVIGATION_BACK:
            exitEdit(false);
            break;
        default:
            break;
    }
}

void DpadSwapScreen::enterEdit(int modeIndex) {
    editingMode = modeIndex;
    selectedRow = 0;
    changesPending = false;
    currentState = State::EDIT_VALUES;
    if (gpMenu) gpMenu->setVisibility(false);

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    // Load current values
    angleValue = std::clamp<int>(gamepadOptions.dpadTriggerThreshold > 0 ? gamepadOptions.dpadTriggerThreshold : 10, 0, 90);
    deadzoneValue = std::clamp<int>(gamepadOptions.dpadDeadzone > 0 ? gamepadOptions.dpadDeadzone : 10, 0, 90);

    resetInputState();
}

void DpadSwapScreen::exitEdit(bool discardChanges) {
    if (!discardChanges && changesPending) {
        applyChanges();
    }

    currentState = State::SELECT_MODE;
    if (gpMenu) {
        gpMenu->setMenuData(&modeSelectionMenu);
        gpMenu->setMenuTitle("[Dpad Swap]");
        gpMenu->setIndex(static_cast<uint16_t>(editingMode));
        gpMenu->setVisibility(true);
    }
    resetInputState();
}

void DpadSwapScreen::adjustCurrentValue(int delta) {
    int* target = nullptr;
    switch (selectedRow) {
        case 0: target = &angleValue; break;
        case 1: target = &deadzoneValue; break;
        default: break;
    }
    if (target == nullptr) return;

    int newValue = std::clamp(*target + delta, 0, 90);
    if (newValue != *target) {
        *target = newValue;
        changesPending = true;
    }
}

int32_t DpadSwapScreen::currentMode() {
    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    return static_cast<int32_t>(gamepadOptions.dpadMode);
}

void DpadSwapScreen::setDpadMode(int modeIndex) {
    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    // Set dpadMode based on modeIndex
    switch (modeIndex) {
        case 0: gamepadOptions.dpadMode = DpadMode::DPAD_MODE_DIGITAL; break;
        case 1: gamepadOptions.dpadMode = DpadMode::DPAD_MODE_LEFT_ANALOG; break;
        case 2: gamepadOptions.dpadMode = DpadMode::DPAD_MODE_RIGHT_ANALOG; break;
    }

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
}

void DpadSwapScreen::applyChanges() {
    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    // Set angle and deadzone values
    gamepadOptions.dpadTriggerThreshold = static_cast<uint32_t>(angleValue);
    gamepadOptions.dpadDeadzone = static_cast<uint32_t>(deadzoneValue);

    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));

    changesPending = false;
    resetInputState();
}

