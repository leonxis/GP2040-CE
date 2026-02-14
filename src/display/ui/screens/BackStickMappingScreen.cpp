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
    prevGPIO24Action = gpioMappings.pins[24].action;
    updateGPIO24Action = gpioMappings.pins[24].action;
    prevGPIO25Action = gpioMappings.pins[25].action;
    updateGPIO25Action = gpioMappings.pins[25].action;
    prevGPIO26Action = gpioMappings.pins[26].action;
    updateGPIO26Action = gpioMappings.pins[26].action;
    prevGPIO27Action = gpioMappings.pins[27].action;
    updateGPIO27Action = gpioMappings.pins[27].action;

    stickSelectionMenu.clear();
    // Always show 4 back keys (same GPIOs as web: 左背键1=24, 右背键1=25, 左背键2=26, 右背键2=27)
    stickSelectionMenu.push_back({"Left back 1", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), 0});
    stickSelectionMenu.push_back({"Right back 1", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), 1});
    stickSelectionMenu.push_back({"Left back 2", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), 2});
    stickSelectionMenu.push_back({"Right back 2", nullptr, nullptr,
        std::bind(&BackStickMappingScreen::currentStickType, this),
        std::bind(&BackStickMappingScreen::selectStickType, this), 3});

    buildButtonMappingMenu(&gpio24MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO24Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO24Mapping, this),
        true);
    buildButtonMappingMenu(&gpio25MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO25Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO25Mapping, this),
        false);
    buildButtonMappingMenu(&gpio26MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO26Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO26Mapping, this),
        true);
    buildButtonMappingMenu(&gpio27MappingMenu,
        std::bind(&BackStickMappingScreen::currentGPIO27Mapping, this),
        std::bind(&BackStickMappingScreen::selectGPIO27Mapping, this),
        false);

    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setIndex(0);
    gpMenu->setVisibility(true);

    prevButtonState = getGamepad()->state.buttons;
    // Note: prevDpadState is no longer used - we read dpad directly from GPIO instead
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
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;
    uint16_t buttonState = getGamepad()->state.buttons;

    if (prevValues != values) {
        if (values & mapMenuUp->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_UP);
        else if (values & mapMenuDown->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_DOWN);
        else if (values & mapMenuLeft->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_LEFT);
        else if (values & mapMenuRight->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_RIGHT);
        else if (values & mapMenuSelect->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_SELECT);
        else if (values & mapMenuBack->pinMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_BACK);
    }

    if (gamepadOptions.miniMenuGamepadInput) {
        // For gamepad input, read dpad buttons directly from GPIO to bypass Dpad Swap conversion
        // This allows navigation to work regardless of dpadMode setting
        // Only respond to physical dpad buttons, not joystick (even if Dpad Swap is enabled)
        if (gamepad->mapDpadUp && gamepad->mapDpadDown && 
            gamepad->mapDpadLeft && gamepad->mapDpadRight) {
            // Read raw GPIO state for dpad buttons (before Dpad Swap conversion)
            bool dpadUpPressed = (values & gamepad->mapDpadUp->pinMask) != 0;
            bool dpadDownPressed = (values & gamepad->mapDpadDown->pinMask) != 0;
            bool dpadLeftPressed = (values & gamepad->mapDpadLeft->pinMask) != 0;
            bool dpadRightPressed = (values & gamepad->mapDpadRight->pinMask) != 0;
            
            // Check previous state to detect changes
            bool prevDpadUp = (prevValues & gamepad->mapDpadUp->pinMask) != 0;
            bool prevDpadDown = (prevValues & gamepad->mapDpadDown->pinMask) != 0;
            bool prevDpadLeft = (prevValues & gamepad->mapDpadLeft->pinMask) != 0;
            bool prevDpadRight = (prevValues & gamepad->mapDpadRight->pinMask) != 0;
            
            // Trigger navigation on state change (edge detection)
            if (dpadUpPressed && !prevDpadUp) updateMenuNavigation(GpioAction::MENU_NAVIGATION_UP);
            else if (dpadDownPressed && !prevDpadDown) updateMenuNavigation(GpioAction::MENU_NAVIGATION_DOWN);
            else if (dpadLeftPressed && !prevDpadLeft) updateMenuNavigation(GpioAction::MENU_NAVIGATION_LEFT);
            else if (dpadRightPressed && !prevDpadRight) updateMenuNavigation(GpioAction::MENU_NAVIGATION_RIGHT);
        }
        if (prevButtonState != buttonState) {
            if (buttonState == mapMenuSelect->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_SELECT);
            else if (buttonState == mapMenuBack->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_BACK);
        }
    }

    prevButtonState = buttonState;
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
    bool /*isGPIO26*/) {
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

    // stickIndex 0..3 = GPIO24(左背键1), GPIO25(右背键1), GPIO26(左背键2), GPIO27(右背键2)
    switch (stickIndex) {
        case 0:
            currentMenu = &gpio24MappingMenu;
            gpMenu->setMenuTitle("Left back 1");
            currentState = STATE_MAPPING_GPIO24;
            break;
        case 1:
            currentMenu = &gpio25MappingMenu;
            gpMenu->setMenuTitle("Right back 1");
            currentState = STATE_MAPPING_GPIO25;
            break;
        case 2:
            currentMenu = &gpio26MappingMenu;
            gpMenu->setMenuTitle("Left back 2");
            currentState = STATE_MAPPING_GPIO26;
            break;
        case 3:
            currentMenu = &gpio27MappingMenu;
            gpMenu->setMenuTitle("Right back 2");
            currentState = STATE_MAPPING_GPIO27;
            break;
        default:
            return;
    }

    previousMenu = &stickSelectionMenu;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuSize(4, 4);
    gpMenu->setIndex(0);
}

void BackStickMappingScreen::selectGPIO24Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO24Action = valueToSave;

    if (prevGPIO24Action != valueToSave) {
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

int32_t BackStickMappingScreen::currentGPIO24Mapping() {
    return static_cast<int32_t>(updateGPIO24Action);
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

void BackStickMappingScreen::selectGPIO26Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO26Action = valueToSave;

    if (prevGPIO26Action != valueToSave) {
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

int32_t BackStickMappingScreen::currentGPIO26Mapping() {
    return static_cast<int32_t>(updateGPIO26Action);
}

void BackStickMappingScreen::selectGPIO27Mapping() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= currentMenu->size()) return;

    int32_t optionValue = currentMenu->at(menuIndex).optionValue;
    if (optionValue == -1) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    updateGPIO27Action = valueToSave;

    if (prevGPIO27Action != valueToSave) {
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

int32_t BackStickMappingScreen::currentGPIO27Mapping() {
    return static_cast<int32_t>(updateGPIO27Action);
}

void BackStickMappingScreen::saveOptions() {
    if (!changesPending) {
        return;
    }

    GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();
    bool saveHasChanged = false;

    if (prevGPIO24Action != updateGPIO24Action) {
        gpioMappings.pins[24].action = updateGPIO24Action;
        prevGPIO24Action = updateGPIO24Action;
        saveHasChanged = true;
    }

    if (prevGPIO25Action != updateGPIO25Action) {
        gpioMappings.pins[25].action = updateGPIO25Action;
        prevGPIO25Action = updateGPIO25Action;
        saveHasChanged = true;
    }

    if (prevGPIO26Action != updateGPIO26Action) {
        gpioMappings.pins[26].action = updateGPIO26Action;
        prevGPIO26Action = updateGPIO26Action;
        saveHasChanged = true;
    }

    if (prevGPIO27Action != updateGPIO27Action) {
        gpioMappings.pins[27].action = updateGPIO27Action;
        prevGPIO27Action = updateGPIO27Action;
        saveHasChanged = true;
    }

    if (saveHasChanged) {
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
        MainMenuScreen::flagHMLConfigRestartPending();
    }

    changesPending = false;
}

void BackStickMappingScreen::updateMenuNavigation(GpioAction action) {
    if (!isMenuReady || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = (currentMenu != nullptr) ? currentMenu->size() : 0;
    bool isFourColumnMenu = (currentMenu == &gpio24MappingMenu || currentMenu == &gpio25MappingMenu ||
                              currentMenu == &gpio26MappingMenu || currentMenu == &gpio27MappingMenu);

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

