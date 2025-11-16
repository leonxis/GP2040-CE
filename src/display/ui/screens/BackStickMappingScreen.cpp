#include "BackStickMappingScreen.h"
#include "GPGFX_UI_screens.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"
#include "gamepad.h"
#include "MainMenuScreen.h"
#include "BoardConfig.h"

void BackStickMappingScreen::init() {
    getRenderer()->clearScreen();

    exitToScreen = -1;
    changesPending = false;
    currentState = STATE_SELECT_STICK;

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

    // Reset menu mappings
    if (mapMenuUp) delete mapMenuUp;
    if (mapMenuDown) delete mapMenuDown;
    if (mapMenuLeft) delete mapMenuLeft;
    if (mapMenuRight) delete mapMenuRight;
    if (mapMenuSelect) delete mapMenuSelect;
    if (mapMenuBack) delete mapMenuBack;

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

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();
    prevGPIO15Action = gpioMappings.pins[15].action;
    updateGPIO15Action = gpioMappings.pins[15].action;
    prevGPIO22Action = gpioMappings.pins[22].action;
    updateGPIO22Action = gpioMappings.pins[22].action;
    prevGPIO14Action = gpioMappings.pins[14].action;
    updateGPIO14Action = gpioMappings.pins[14].action;
    prevGPIO25Action = gpioMappings.pins[25].action;
    updateGPIO25Action = gpioMappings.pins[25].action;

    stickSelectionMenu.clear();
    bool usbPeripheralEnabled = isUSBPeripheralEnabled();
    
    // GPIO15 and GPIO14 are only available when USB peripheral is disabled
    // (USB peripheral uses these pins, so they conflict)
    if (!usbPeripheralEnabled) {
        stickSelectionMenu.push_back({"Left Plus backstick", nullptr, nullptr,
            std::bind(&BackStickMappingScreen::currentStickType, this),
            std::bind(&BackStickMappingScreen::selectStickType, this), 0});
    }
    stickSelectionMenu.push_back({"Left backstick", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), usbPeripheralEnabled ? 0 : 1});
    if (!usbPeripheralEnabled) {
        stickSelectionMenu.push_back({"Right Plus backstick", nullptr, nullptr,
            std::bind(&BackStickMappingScreen::currentStickType, this),
            std::bind(&BackStickMappingScreen::selectStickType, this), 2});
    }
    stickSelectionMenu.push_back({"Right backstick", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), usbPeripheralEnabled ? 1 : 3});

    buildButtonMappingMenu(&gpio15MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO15Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO15Mapping, this),
        true);
    buildButtonMappingMenu(&gpio22MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO22Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO22Mapping, this),
        true);
    buildButtonMappingMenu(&gpio14MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO14Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO14Mapping, this),
        false);
    buildButtonMappingMenu(&gpio25MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO25Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO25Mapping, this),
        false);

    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setIndex(0);
    gpMenu->setVisibility(true);

    prevButtonState = getGamepad()->state.buttons;
    prevDpadState = getGamepad()->state.dpad;
    prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;

    isMenuReady = true;
}

void BackStickMappingScreen::shutdown() {
    clearElements();
    isMenuReady = false;
    currentMenu = nullptr;
    previousMenu = nullptr;
    gpMenu = nullptr;

    if (mapMenuUp) { delete mapMenuUp; mapMenuUp = nullptr; }
    if (mapMenuDown) { delete mapMenuDown; mapMenuDown = nullptr; }
    if (mapMenuLeft) { delete mapMenuLeft; mapMenuLeft = nullptr; }
    if (mapMenuRight) { delete mapMenuRight; mapMenuRight = nullptr; }
    if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
    if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }
}

int8_t BackStickMappingScreen::update() {
    if (!isMenuReady) {
        int8_t result = exitToScreen;
        exitToScreen = -1;
        return result;
    }

    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;
    uint16_t buttonState = getGamepad()->state.buttons;
    uint8_t dpadState = getGamepad()->state.dpad;

    if (prevValues != values) {
        if (values & mapMenuUp->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_UP);
        else if (values & mapMenuDown->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_DOWN);
        else if (values & mapMenuLeft->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_LEFT);
        else if (values & mapMenuRight->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_RIGHT);
        else if (values & mapMenuSelect->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_SELECT);
        else if (values & mapMenuBack->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_BACK);
    }

    if (gamepadOptions.miniMenuGamepadInput) {
        if (prevDpadState != dpadState) {
            if (dpadState == mapMenuUp->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_UP);
            else if (dpadState == mapMenuDown->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_DOWN);
            else if (dpadState == mapMenuLeft->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_LEFT);
            else if (dpadState == mapMenuRight->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_RIGHT);
        }
        if (prevButtonState != buttonState) {
            if (buttonState == mapMenuSelect->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_SELECT);
            else if (buttonState == mapMenuBack->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_BACK);
        }
    }

    prevButtonState = buttonState;
    prevDpadState = dpadState;
    prevValues = values;

    int8_t result = exitToScreen;
    if (exitToScreen != -1) {
        exitToScreen = -1;
        return result;
    }

    return -1;
}

void BackStickMappingScreen::drawScreen() {
    if (gpMenu == nullptr) return;

    gpMenu->setVisibility(true);
}

void BackStickMappingScreen::buildButtonMappingMenu(std::vector<MenuEntry>* menu,
    std::function<int32_t()> currentValueFunc,
    std::function<void()> selectFunc,
    bool /*isGPIO22*/) {
    menu->clear();

    menu->push_back({"NONE", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::NONE});
    menu->push_back({CHAR_UP, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_UP});
    menu->push_back({CHAR_DOWN, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_DOWN});
    menu->push_back({CHAR_LEFT, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_LEFT});
    menu->push_back({CHAR_RIGHT, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_RIGHT});
    menu->push_back({CHAR_CROSS, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B1});
    menu->push_back({CHAR_CIRCLE, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B2});
    menu->push_back({CHAR_SQUARE, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B3});
    menu->push_back({CHAR_TRIANGLE, nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B4});
    menu->push_back({"L1", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L1});
    menu->push_back({"R1", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R1});
    menu->push_back({"L2", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L2});
    menu->push_back({"R2", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R2});
    menu->push_back({"L3", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L3});
    menu->push_back({"R3", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R3});
    menu->push_back({"S1", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_S1});
    menu->push_back({"S2", nullptr, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_S2});
}

void BackStickMappingScreen::selectStickType() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int stickIndex = currentMenu->at(menuIndex).optionValue;
    enterMapping(stickIndex);
}

int32_t BackStickMappingScreen::currentStickType() {
    return -1;
}

void BackStickMappingScreen::enterMapping(int stickIndex) {
    if (gpMenu == nullptr) return;

    bool usbPeripheralEnabled = isUSBPeripheralEnabled();
    int actualIndex = stickIndex;
    
    // Map menu index to GPIO index based on USB peripheral state
    // When USB peripheral is disabled: 0->GPIO15, 1->GPIO22, 2->GPIO14, 3->GPIO25
    // When USB peripheral is enabled: 0->GPIO22, 1->GPIO25
    if (usbPeripheralEnabled) {
        // When USB peripheral is enabled, GPIO15 and GPIO14 are hidden
        // Menu index 0 -> GPIO22 (actualIndex 1)
        // Menu index 1 -> GPIO25 (actualIndex 3)
        if (stickIndex == 0) actualIndex = 1; // GPIO22
        else if (stickIndex == 1) actualIndex = 3; // GPIO25
    }
    // When USB peripheral is disabled, stickIndex directly maps to actualIndex
    // 0->GPIO15(0), 1->GPIO22(1), 2->GPIO14(2), 3->GPIO25(3)

    switch (actualIndex) {
        case 0: // GPIO15: Left Plus backstick
            currentMenu = &gpio15MappingMenu;
            gpMenu->setMenuTitle("Left Plus backstick");
            currentState = STATE_MAPPING_GPIO15;
            break;
        case 1: // GPIO22: Left backstick
            currentMenu = &gpio22MappingMenu;
            gpMenu->setMenuTitle("Left backstick");
            currentState = STATE_MAPPING_GPIO22;
            break;
        case 2: // GPIO14: Right Plus backstick
            currentMenu = &gpio14MappingMenu;
            gpMenu->setMenuTitle("Right Plus backstick");
            currentState = STATE_MAPPING_GPIO14;
            break;
        case 3: // GPIO25: Right backstick
            currentMenu = &gpio25MappingMenu;
            gpMenu->setMenuTitle("Right backstick");
            currentState = STATE_MAPPING_GPIO25;
            break;
        default:
            return;
    }
    
    previousMenu = &stickSelectionMenu;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuSize(4, 4);
    gpMenu->setIndex(0);
}

void BackStickMappingScreen::selectGPIO15Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO15Action = valueToSave;

    if (prevGPIO15Action != valueToSave) {
        changesPending = true;
    }

    currentState = STATE_SELECT_STICK;
    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(18, menuLineSize);
    gpMenu->setIndex(0);

    if (changesPending) {
        saveOptions();
    }
}

int32_t BackStickMappingScreen::currentGPIO15Mapping() {
    return static_cast<int32_t>(updateGPIO15Action);
}

void BackStickMappingScreen::selectGPIO22Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO22Action = valueToSave;

    if (prevGPIO22Action != valueToSave) {
        changesPending = true;
    }

    currentState = STATE_SELECT_STICK;
    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(18, menuLineSize);
    gpMenu->setIndex(0);

    if (changesPending) {
        saveOptions();
    }
}

int32_t BackStickMappingScreen::currentGPIO22Mapping() {
    return static_cast<int32_t>(updateGPIO22Action);
}

void BackStickMappingScreen::selectGPIO14Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO14Action = valueToSave;

    if (prevGPIO14Action != valueToSave) {
        changesPending = true;
    }

    currentState = STATE_SELECT_STICK;
    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(18, menuLineSize);
    gpMenu->setIndex(0);

    if (changesPending) {
        saveOptions();
    }
}

int32_t BackStickMappingScreen::currentGPIO14Mapping() {
    return static_cast<int32_t>(updateGPIO14Action);
}

void BackStickMappingScreen::selectGPIO25Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO25Action = valueToSave;

    if (prevGPIO25Action != valueToSave) {
        changesPending = true;
    }

    currentState = STATE_SELECT_STICK;
    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(18, menuLineSize);
    gpMenu->setIndex(0);

    if (changesPending) {
        saveOptions();
    }
}

int32_t BackStickMappingScreen::currentGPIO25Mapping() {
    return static_cast<int32_t>(updateGPIO25Action);
}

void BackStickMappingScreen::saveOptions() {
    if (!changesPending) {
        return;
    }

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();
    bool saveHasChanged = false;

    if (prevGPIO15Action != updateGPIO15Action) {
        gpioMappings.pins[15].action = updateGPIO15Action;
        prevGPIO15Action = updateGPIO15Action;
        saveHasChanged = true;
    }

    if (prevGPIO22Action != updateGPIO22Action) {
        gpioMappings.pins[22].action = updateGPIO22Action;
        prevGPIO22Action = updateGPIO22Action;
        saveHasChanged = true;
    }

    if (prevGPIO14Action != updateGPIO14Action) {
        gpioMappings.pins[14].action = updateGPIO14Action;
        prevGPIO14Action = updateGPIO14Action;
        saveHasChanged = true;
    }

    if (prevGPIO25Action != updateGPIO25Action) {
        gpioMappings.pins[25].action = updateGPIO25Action;
        prevGPIO25Action = updateGPIO25Action;
        saveHasChanged = true;
    }

    if (saveHasChanged) {
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
        MainMenuScreen::flagHMLConfigRestartPending();
    }

    changesPending = false;
}

bool BackStickMappingScreen::isUSBPeripheralEnabled() {
    // Check USB peripheral enabled status from storage (web settings)
    // This reflects the actual USB host switch state in the web UI
    return Storage::getInstance().getPeripheralOptions().blockUSB0.enabled;
}

void BackStickMappingScreen::updateMenuNavigation(GpioAction action) {
    if (!isMenuReady || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = (currentMenu != nullptr) ? currentMenu->size() : 0;
    bool isFourColumnMenu = (currentMenu == &gpio15MappingMenu || currentMenu == &gpio22MappingMenu || 
                              currentMenu == &gpio14MappingMenu || currentMenu == &gpio25MappingMenu);

    switch (action) {
        case GpioAction::MENU_NAVIGATION_UP:
            if (isFourColumnMenu) {
                if (menuIndex == 0) {
                    gpMenu->setIndex(menuSize > 0 ? menuSize - 1 : 0);
                } else if (menuIndex >= 4) {
                    gpMenu->setIndex(menuIndex - 4);
                } else {
                    gpMenu->setIndex(0);
                }
            } else {
                if (menuSize == 0) break;
                if (menuIndex == 0) gpMenu->setIndex(menuSize - 1);
                else gpMenu->setIndex(menuIndex - 1);
            }
            break;
        case GpioAction::MENU_NAVIGATION_DOWN:
            if (isFourColumnMenu) {
                if (menuIndex == 0) {
                    gpMenu->setIndex(1);
                } else {
                    uint16_t column = (menuIndex - 1) % 4;
                    uint16_t nextIndex = menuIndex + 4;
                    uint16_t maxIndex = ((menuSize - 2) / 4) * 4 + column + 1;
                    if (maxIndex >= menuSize) maxIndex = menuSize - 1;
                    if (nextIndex <= maxIndex) gpMenu->setIndex(nextIndex);
                    else {
                        if (column == 0) gpMenu->setIndex(0);
                        else gpMenu->setIndex(column + 1);
                    }
                }
            } else {
                if (menuSize == 0) break;
                if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
                else gpMenu->setIndex(0);
            }
            break;
        case GpioAction::MENU_NAVIGATION_LEFT:
            if (isFourColumnMenu) {
                if (menuIndex == 0) {
                    gpMenu->setIndex(menuSize > 0 ? menuSize - 1 : 0);
                } else if (menuIndex > 0) {
                    gpMenu->setIndex(menuIndex - 1);
                }
            }
            break;
        case GpioAction::MENU_NAVIGATION_RIGHT:
            if (isFourColumnMenu) {
                if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
                else gpMenu->setIndex(0);
            }
            break;
        case GpioAction::MENU_NAVIGATION_SELECT:
            if (currentMenu == nullptr || menuIndex >= currentMenu->size()) break;
            if (currentMenu->at(menuIndex).submenu != nullptr) {
                previousMenu = currentMenu;
                currentMenu = currentMenu->at(menuIndex).submenu;
                gpMenu->setMenuData(currentMenu);
                gpMenu->setMenuTitle(previousMenu->at(menuIndex).label);
                gpMenu->setIndex(0);
            } else {
                currentMenu->at(menuIndex).action();
            }
            break;
        case GpioAction::MENU_NAVIGATION_BACK:
            if (isFourColumnMenu) {
                currentMenu = &stickSelectionMenu;
                previousMenu = nullptr;
                gpMenu->setMenuData(currentMenu);
                gpMenu->setMenuTitle("[Back stick]");
                gpMenu->setMenuSize(18, menuLineSize);
                gpMenu->setIndex(0);
                currentState = STATE_SELECT_STICK;
            } else {
                // Return to HML Config menu instead of main menu
                MainMenuScreen::flagOpenHMLConfigMenu();
                exitToScreen = DisplayMode::MAIN_MENU;
                isMenuReady = false;
            }
            break;
        default:
            break;
    }
}

