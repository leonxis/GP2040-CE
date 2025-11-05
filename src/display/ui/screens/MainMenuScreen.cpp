#include "MainMenuScreen.h"
#include "hardware/watchdog.h"
#include "system.h"
#include "storagemanager.h"
#include "GPRestartEvent.h"

extern uint32_t getMillis();

void MainMenuScreen::init() {
    getRenderer()->clearScreen();
    currentMenu = &mainMenu;
    previousMenu = nullptr;

    exitToScreen = -1;

    gpMenu = new GPMenu();
    gpMenu->setRenderer(getRenderer());
    gpMenu->setPosition(8, 16);
    gpMenu->setStrokeColor(1);
    gpMenu->setFillColor(1);
    gpMenu->setMenuSize(18, menuLineSize);
    gpMenu->setViewport(this->getViewport());
    gpMenu->setShape(GPShape_Type::GP_SHAPE_SQUARE);
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle(MAIN_MENU_NAME);
    addElement(gpMenu);

    mapMenuUp = new GamepadButtonMapping(GAMEPAD_MASK_UP);
    mapMenuDown = new GamepadButtonMapping(GAMEPAD_MASK_DOWN);
    mapMenuLeft = new GamepadButtonMapping(GAMEPAD_MASK_LEFT);
    mapMenuRight = new GamepadButtonMapping(GAMEPAD_MASK_RIGHT);
    mapMenuSelect = new GamepadButtonMapping(GAMEPAD_MASK_B1);
    mapMenuBack = new GamepadButtonMapping(GAMEPAD_MASK_B2);
    mapMenuToggle = new GamepadButtonMapping(0);

    // populate the profiles menu
    uint8_t profileCount = (sizeof(Storage::getInstance().getProfileOptions().gpioMappingsSets)/sizeof(GpioMappings))+1;
    for (uint8_t profileCtr = 0; profileCtr < profileCount; profileCtr++) {
        std::string menuLabel = "";
        if (profileCtr == 0) {
            menuLabel = Storage::getInstance().getGpioMappings().profileLabel;
            // If Profile #1 has no name, set it to Profile #1
            if (menuLabel.empty()) {
                menuLabel = "Profile #" + std::to_string(profileCtr+1);
            }
        } else {
            menuLabel = Storage::getInstance().getProfileOptions().gpioMappingsSets[profileCtr-1].profileLabel;
            // Do not show other profiles if they are empty
            if (menuLabel.empty()) {
                continue;
            }
        }
        
        MenuEntry menuEntry = {menuLabel, NULL, nullptr, std::bind(&MainMenuScreen::currentProfile, this), std::bind(&MainMenuScreen::selectProfile, this), profileCtr+1};
        profilesMenu.push_back(menuEntry);
    }

    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        switch (pinMappings[pin].action) {
            case GpioAction::MENU_NAVIGATION_UP: mapMenuUp->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_DOWN: mapMenuDown->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_LEFT: mapMenuLeft->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_RIGHT: mapMenuRight->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_SELECT: mapMenuSelect->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_BACK: mapMenuBack->pinMask |= 1 << pin; break;
            case GpioAction::MENU_NAVIGATION_TOGGLE: mapMenuToggle->pinMask |= 1 << pin; break;
            default:    break;
        }
    }

    changeRequiresReboot = false;
    changeRequiresSave = false;
    prevInputMode = Storage::getInstance().GetGamepad()->getOptions().inputMode;
    updateInputMode = Storage::getInstance().GetGamepad()->getOptions().inputMode;
    
    prevDpadMode = Storage::getInstance().GetGamepad()->getOptions().dpadMode;
    updateDpadMode = Storage::getInstance().GetGamepad()->getOptions().dpadMode;
    
    prevSocdMode = Storage::getInstance().GetGamepad()->getOptions().socdMode;
    updateSocdMode = Storage::getInstance().GetGamepad()->getOptions().socdMode;
    
    prevProfile = Storage::getInstance().GetGamepad()->getOptions().profileNumber;
    updateProfile = Storage::getInstance().GetGamepad()->getOptions().profileNumber;
    
    prevFocus = Storage::getInstance().getAddonOptions().focusModeOptions.enabled;
    updateFocus = Storage::getInstance().getAddonOptions().focusModeOptions.enabled;
    
    prevTurbo = Storage::getInstance().getAddonOptions().turboOptions.enabled;
    updateTurbo = Storage::getInstance().getAddonOptions().turboOptions.enabled;

    // Initialize GPIO22 and GPIO25 mappings
    GpioMappingInfo* pinMappings = Storage::getInstance().getGpioMappings().pins;
    prevGPIO22Action = pinMappings[22].action;
    updateGPIO22Action = pinMappings[22].action;
    prevGPIO25Action = pinMappings[25].action;
    updateGPIO25Action = pinMappings[25].action;
    backStickChangesPending = false;

    // Build back stick selection menu (Left stick / Right stick)
    backStickSelectionMenu = {
        {"Left stick",  NULL, nullptr, std::bind(&MainMenuScreen::modeValue, this), std::bind(&MainMenuScreen::selectBackStickType, this), 0},
        {"Right stick", NULL, nullptr, std::bind(&MainMenuScreen::modeValue, this), std::bind(&MainMenuScreen::selectBackStickType, this), 1},
    };

    // Build button mapping menus
    buildButtonMappingMenu(&gpio22MappingMenu, 
        std::bind(&MainMenuScreen::currentGPIO22Mapping, this),
        std::bind(&MainMenuScreen::selectGPIO22Mapping, this), true);
    buildButtonMappingMenu(&gpio25MappingMenu,
        std::bind(&MainMenuScreen::currentGPIO25Mapping, this),
        std::bind(&MainMenuScreen::selectGPIO25Mapping, this), false);

    setMenuHome();
}

void MainMenuScreen::shutdown() {
    clearElements();
    exitToScreen = -1;
}

void MainMenuScreen::drawScreen() {
    if (gpMenu == nullptr) return;
    gpMenu->setVisibility(!screenIsPrompting);

    if (!screenIsPrompting) {

    } else {
        // Check if this is a back stick reboot prompt
        if (backStickChangesPending && changeRequiresReboot) {
            getRenderer()->drawText(1, 1, "Reboot to");
            getRenderer()->drawText(1, 2, "apply settings");
            
            if (promptChoice) getRenderer()->drawText(5, 4, CHAR_RIGHT);
            getRenderer()->drawText(6, 4, "Yes");
            if (!promptChoice) getRenderer()->drawText(11, 4, CHAR_RIGHT);
            getRenderer()->drawText(12, 4, "No");
        } else {
            getRenderer()->drawText(1, 1, "Config has changed.");
            if (changeRequiresSave && !changeRequiresReboot) {
                getRenderer()->drawText(3, 3, "Would you like");
                getRenderer()->drawText(6, 4, "to save?");
            } else if (changeRequiresSave && changeRequiresReboot) {
                getRenderer()->drawText(3, 3, "Would you like");
                getRenderer()->drawText(1, 4, "to save & restart?");
            } else {

            }
            
            if (promptChoice) getRenderer()->drawText(5, 6, CHAR_RIGHT);
            getRenderer()->drawText(6, 6, "Yes");
            if (!promptChoice) getRenderer()->drawText(11, 6, CHAR_RIGHT);
            getRenderer()->drawText(12, 6, "No");
        }
    }
}

void MainMenuScreen::setMenu(std::vector<MenuEntry>* menu) {
    currentMenu = menu;
}

void MainMenuScreen::setMenuHome() {
    currentMenu = &mainMenu;
    previousMenu = nullptr;

    exitToScreen = -1;
    prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;
    isMenuReady = true;
}

int8_t MainMenuScreen::update() {
    if (isMenuReady) {
        GamepadOptions & gamepadOptions = Storage::getInstance().getGamepadOptions();
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
            else if (values & mapMenuToggle->pinMask) {
                // Menu toggle will always exit out of main menu
                exitToScreen = DisplayMode::BUTTONS;
                exitToScreenBeforePrompt = DisplayMode::BUTTONS;
            }
        }
        if (gamepadOptions.miniMenuGamepadInput == true ) {
            if (prevDpadState != dpadState ) {
                if (dpadState == mapMenuUp->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_UP);
                else if (dpadState == mapMenuDown->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_DOWN);
                else if (dpadState == mapMenuLeft->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_LEFT);
                else if (dpadState == mapMenuRight->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_RIGHT);
            }
            if ( prevButtonState != buttonState ) {
                if (buttonState == mapMenuSelect->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_SELECT);
                else if (buttonState == mapMenuBack->buttonMask) updateMenuNavigation(GpioAction::MENU_NAVIGATION_BACK);
            }
        }

        prevButtonState = buttonState;
        prevDpadState = dpadState;
        prevValues = values;

        // Core0 Event Navigations
        if (eventAction != GpioAction::NONE) {
            updateMenuNavigation(eventAction);
            eventAction = GpioAction::NONE;
        }

        if ((exitToScreen != -1) && ((changeRequiresSave) || (changeRequiresReboot))) {
            // trying to exit menu but a change requires a save/reboot
            exitToScreenBeforePrompt = exitToScreen;
            exitToScreen = -1;
            screenIsPrompting = true;
        }

        return exitToScreen;
    } else {
        return -1;
    }
}

void MainMenuScreen::updateEventMenuNavigation(GpioAction action) {
    eventAction = action;
}

void MainMenuScreen::updateMenuNavigation(GpioAction action) {
    if (gpMenu == nullptr)
        return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = gpMenu->getDataSize();

    if (isMenuReady) {
        if (screenIsPrompting) {
            switch(action) {
                case GpioAction::MENU_NAVIGATION_UP:
                case GpioAction::MENU_NAVIGATION_DOWN:
                case GpioAction::MENU_NAVIGATION_LEFT:
                case GpioAction::MENU_NAVIGATION_RIGHT:
                    promptChoice = !promptChoice;
                    break;
                case GpioAction::MENU_NAVIGATION_SELECT:
                    if (promptChoice) {
                        // Save and reboot
                        saveOptions();
                        // Trigger reboot
                        EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
                        exitToScreen = DisplayMode::BUTTONS;
                        exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                    } else {
                        // Cancel - stay in back stick selection menu
                        screenIsPrompting = false;
                        backStickChangesPending = false; // Reset flag
                        // Don't exit, just cancel the prompt
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_BACK:
                    // Cancel - stay in back stick selection menu
                    screenIsPrompting = false;
                    backStickChangesPending = false; // Reset flag
                    break;
                default:
                    break;
            }
        } else {
            switch (action) { 
                case GpioAction::MENU_NAVIGATION_UP:
                    if ( menuIndex == 0 ) {
                        gpMenu->setIndex(menuSize-1);
                    } else {
                        gpMenu->setIndex(menuIndex-1);
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_DOWN:
                    if (menuIndex < menuSize-1) {
                        gpMenu->setIndex(menuIndex+1);
                    } else {
                        gpMenu->setIndex(0);
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_LEFT:
                    if ((menuIndex-menuLineSize) > 0) {
                        gpMenu->setIndex(menuIndex - menuLineSize);
                    } else {
                        gpMenu->setIndex(0);
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_RIGHT:
                    if ((menuIndex+menuLineSize) < (menuSize-1)) {
                        gpMenu->setIndex(menuIndex + menuLineSize);
                    } else {
                        gpMenu->setIndex(menuSize-1);
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_SELECT:
                    if (currentMenu->at(menuIndex).submenu != nullptr) {
                        previousMenu = currentMenu;
                        currentMenu = currentMenu->at(menuIndex).submenu;
                        gpMenu->setMenuData(currentMenu);
                        gpMenu->setMenuTitle(previousMenu->at(menuIndex).label);
                        gpMenu->setIndex(0);
                    } else {
                        // Execute the action (which may change menus)
                        currentMenu->at(menuIndex).action();
                    }
                    break;
                case GpioAction::MENU_NAVIGATION_BACK:
                    if (previousMenu != nullptr) {
                        // Check if we're exiting from back stick selection menu to main menu
                        if (currentMenu == &backStickSelectionMenu && previousMenu == &mainMenu) {
                            exitBackStickSelection();
                        } else {
                            // Normal back navigation
                            currentMenu = previousMenu;
                            previousMenu = nullptr;
                            gpMenu->setMenuData(currentMenu);
                            if (currentMenu == &backStickSelectionMenu) {
                                gpMenu->setMenuTitle("Back stick");
                            } else if (currentMenu == &mainMenu) {
                                gpMenu->setMenuTitle(MAIN_MENU_NAME);
                            }
                            gpMenu->setIndex(0);
                        }
                    } else {
                        exitToScreen = DisplayMode::BUTTONS;
                        exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void MainMenuScreen::chooseAndReturn() {
    if (previousMenu != nullptr) {
        currentMenu = previousMenu;
        previousMenu = nullptr;
        gpMenu->setMenuData(currentMenu);
        gpMenu->setMenuTitle(MAIN_MENU_NAME);
        gpMenu->setIndex(0);
    } else {
        exitToScreen = DisplayMode::BUTTONS;
        exitToScreenBeforePrompt = DisplayMode::BUTTONS;
        isPressed = false;
    }
}

void MainMenuScreen::saveAndExit() {
    saveOptions();
    exitToScreen = DisplayMode::BUTTONS;
}

void MainMenuScreen::exitOnly() {
    exitToScreen = DisplayMode::BUTTONS;
}

int32_t MainMenuScreen::modeValue() {
    return -1;
}

void MainMenuScreen::selectInputMode() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        InputMode valueToSave = (InputMode)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevInputMode = Storage::getInstance().GetGamepad()->getOptions().inputMode;
        updateInputMode = valueToSave;

        chooseAndReturn();

        if (prevInputMode != valueToSave) {
            // input mode requires a save and reboot
            changeRequiresReboot = true;
            changeRequiresSave = true;
        }
    }
}

int32_t MainMenuScreen::currentInputMode() {
    return updateInputMode;
}

void MainMenuScreen::selectDPadMode() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        DpadMode valueToSave = (DpadMode)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevDpadMode = Storage::getInstance().GetGamepad()->getOptions().dpadMode;
        updateDpadMode = valueToSave;

        chooseAndReturn();

        if (prevDpadMode != valueToSave) changeRequiresSave = true;
    }
}

int32_t MainMenuScreen::currentDpadMode() {
    return updateDpadMode;
}

void MainMenuScreen::selectSOCDMode() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        SOCDMode valueToSave = (SOCDMode)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevSocdMode = Storage::getInstance().GetGamepad()->getOptions().socdMode;
        updateSocdMode = valueToSave;

        chooseAndReturn();

        if (prevSocdMode != valueToSave) changeRequiresSave = true;
    }
}

int32_t MainMenuScreen::currentSOCDMode() {
    return updateSocdMode;
}

void MainMenuScreen::resetOptions() {
    if (changeRequiresSave) {
        if (prevInputMode != updateInputMode) updateInputMode = prevInputMode;
        if (prevDpadMode != updateDpadMode) updateDpadMode = prevDpadMode;
        if (prevSocdMode != updateSocdMode) updateSocdMode = prevSocdMode;
        if (prevProfile != updateProfile) updateProfile = prevProfile;
        if (prevFocus != updateFocus) updateFocus = prevFocus;
        if (prevTurbo != updateTurbo) updateTurbo = prevTurbo;
        if (prevGPIO22Action != updateGPIO22Action) updateGPIO22Action = prevGPIO22Action;
        if (prevGPIO25Action != updateGPIO25Action) updateGPIO25Action = prevGPIO25Action;
    }

    changeRequiresSave = false;
    changeRequiresReboot = false;
    screenIsPrompting = false;
}

void MainMenuScreen::saveOptions() {
    GamepadOptions& options = Storage::getInstance().getGamepadOptions();
    AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();

    if (changeRequiresSave) {
        bool saveHasChanged = false;
        if (prevInputMode != updateInputMode) {
            options.inputMode = updateInputMode;
            saveHasChanged = true;
        }
        if (prevDpadMode != updateDpadMode) {
            options.dpadMode = updateDpadMode;
            saveHasChanged = true;
        }
        if (prevSocdMode != updateSocdMode) {
            options.socdMode = updateSocdMode;
            saveHasChanged = true;
        }
        if (prevProfile != updateProfile) {
            options.profileNumber = updateProfile;
            saveHasChanged = true;
        }
        if ((bool)prevFocus != (bool)updateFocus) {
            addonOptions.focusModeOptions.enabled = (bool)updateFocus;
            saveHasChanged = true;
        }
        if ((bool)prevTurbo != (bool)updateTurbo) {
            addonOptions.turboOptions.enabled = (bool)updateTurbo;
            saveHasChanged = true;
        }

        // Save GPIO22 and GPIO25 mappings
        GpioMappings& gpioMappings = Storage::getInstance().getGpioMappings();
        if (prevGPIO22Action != updateGPIO22Action) {
            gpioMappings.pins[22].action = updateGPIO22Action;
            saveHasChanged = true;
        }
        if (prevGPIO25Action != updateGPIO25Action) {
            gpioMappings.pins[25].action = updateGPIO25Action;
            saveHasChanged = true;
        }

        if (saveHasChanged) {
            EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, changeRequiresReboot));
        }
        changeRequiresSave = false;
        changeRequiresReboot = false;
    }

    screenIsPrompting = false;

    if (exitToScreenBeforePrompt != -1) {
        exitToScreen = exitToScreenBeforePrompt;
        exitToScreenBeforePrompt = -1;
    }
}

void MainMenuScreen::selectProfile() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        uint8_t valueToSave = currentMenu->at(gpMenu->getIndex()).optionValue;
        prevProfile = Storage::getInstance().GetGamepad()->getOptions().profileNumber;
        updateProfile = valueToSave;

        chooseAndReturn();

        if (prevProfile != valueToSave) changeRequiresSave = true;
    }
}

int32_t MainMenuScreen::currentProfile() {
    return updateProfile;
}

void MainMenuScreen::selectFocusMode() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        bool valueToSave = (bool)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevFocus = Storage::getInstance().getAddonOptions().focusModeOptions.enabled;
        updateFocus = valueToSave;

        chooseAndReturn();

        if (prevFocus != valueToSave) changeRequiresSave = true;
    }
}

int32_t MainMenuScreen::currentFocusMode() {
    return updateFocus;
}

void MainMenuScreen::selectTurboMode() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        bool valueToSave = (bool)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevTurbo = Storage::getInstance().getAddonOptions().turboOptions.enabled;
        updateTurbo = valueToSave;

        chooseAndReturn();

        if (prevTurbo != valueToSave) changeRequiresSave = true;
    }
}

int32_t MainMenuScreen::currentTurboMode() {
    return updateTurbo;
}

std::string MainMenuScreen::getGpioActionName(GpioAction action) {
    switch (action) {
        case GpioAction::NONE: return "None";
        case GpioAction::BUTTON_PRESS_UP: return "Up";
        case GpioAction::BUTTON_PRESS_DOWN: return "Down";
        case GpioAction::BUTTON_PRESS_LEFT: return "Left";
        case GpioAction::BUTTON_PRESS_RIGHT: return "Right";
        case GpioAction::BUTTON_PRESS_B1: return "B1";
        case GpioAction::BUTTON_PRESS_B2: return "B2";
        case GpioAction::BUTTON_PRESS_B3: return "B3";
        case GpioAction::BUTTON_PRESS_B4: return "B4";
        case GpioAction::BUTTON_PRESS_L1: return "L1";
        case GpioAction::BUTTON_PRESS_R1: return "R1";
        case GpioAction::BUTTON_PRESS_L2: return "L2";
        case GpioAction::BUTTON_PRESS_R2: return "R2";
        case GpioAction::BUTTON_PRESS_S1: return "S1";
        case GpioAction::BUTTON_PRESS_S2: return "S2";
        case GpioAction::BUTTON_PRESS_A1: return "A1";
        case GpioAction::BUTTON_PRESS_A2: return "A2";
        case GpioAction::BUTTON_PRESS_L3: return "L3";
        case GpioAction::BUTTON_PRESS_R3: return "R3";
        case GpioAction::BUTTON_PRESS_FN: return "FN";
        case GpioAction::BUTTON_PRESS_A3: return "A3";
        case GpioAction::BUTTON_PRESS_A4: return "A4";
        case GpioAction::BUTTON_PRESS_E1: return "E1";
        case GpioAction::BUTTON_PRESS_E2: return "E2";
        case GpioAction::BUTTON_PRESS_E3: return "E3";
        case GpioAction::BUTTON_PRESS_E4: return "E4";
        case GpioAction::BUTTON_PRESS_E5: return "E5";
        case GpioAction::BUTTON_PRESS_E6: return "E6";
        case GpioAction::BUTTON_PRESS_E7: return "E7";
        case GpioAction::BUTTON_PRESS_E8: return "E8";
        case GpioAction::BUTTON_PRESS_E9: return "E9";
        case GpioAction::BUTTON_PRESS_E10: return "E10";
        case GpioAction::BUTTON_PRESS_E11: return "E11";
        case GpioAction::BUTTON_PRESS_E12: return "E12";
        default: return "Other";
    }
}

void MainMenuScreen::buildButtonMappingMenu(std::vector<MenuEntry>* menu, std::function<int32_t()> currentValueFunc, std::function<void()> selectFunc, bool isGPIO22) {
    menu->clear();
    // Common button actions - using provided current value and select functions
    // Format labels for better alignment (pad with spaces to ensure consistent width)
    menu->push_back({"NONE",     NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::NONE});
    menu->push_back({"UP",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_UP});
    menu->push_back({"DOWN",     NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_DOWN});
    menu->push_back({"LEFT",     NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_LEFT});
    menu->push_back({"RIGHT",    NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_RIGHT});
    menu->push_back({"B1",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B1});
    menu->push_back({"B2",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B2});
    menu->push_back({"B3",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B3});
    menu->push_back({"B4",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_B4});
    menu->push_back({"L1",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L1});
    menu->push_back({"R1",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R1});
    menu->push_back({"L2",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L2});
    menu->push_back({"R2",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R2});
    menu->push_back({"S1",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_S1});
    menu->push_back({"S2",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_S2});
    menu->push_back({"A1",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_A1});
    menu->push_back({"A2",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_A2});
    menu->push_back({"L3",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_L3});
    menu->push_back({"R3",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_R3});
    menu->push_back({"FN",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_FN});
    menu->push_back({"A3",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_A3});
    menu->push_back({"A4",       NULL, nullptr, currentValueFunc, selectFunc, (int32_t)GpioAction::BUTTON_PRESS_A4});
}

void MainMenuScreen::selectGPIO22Mapping() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        GpioAction valueToSave = (GpioAction)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevGPIO22Action = Storage::getInstance().getGpioMappings().pins[22].action;
        updateGPIO22Action = valueToSave;

        // Return to back stick selection menu instead of main menu
        if (previousMenu != nullptr) {
            currentMenu = previousMenu;
            previousMenu = nullptr;
            gpMenu->setMenuData(currentMenu);
            gpMenu->setMenuTitle("Back stick");
            gpMenu->setIndex(0);
        }

        if (prevGPIO22Action != valueToSave) {
            backStickChangesPending = true;
            changeRequiresSave = true;
            changeRequiresReboot = true; // GPIO mapping changes require reboot
        }
    }
}

int32_t MainMenuScreen::currentGPIO22Mapping() {
    // Return the current action value (not index) so GPMenu can show "*" on the selected item
    return (int32_t)updateGPIO22Action;
}

void MainMenuScreen::selectGPIO25Mapping() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        GpioAction valueToSave = (GpioAction)currentMenu->at(gpMenu->getIndex()).optionValue;
        prevGPIO25Action = Storage::getInstance().getGpioMappings().pins[25].action;
        updateGPIO25Action = valueToSave;

        // Return to back stick selection menu instead of main menu
        if (previousMenu != nullptr) {
            currentMenu = previousMenu;
            previousMenu = nullptr;
            gpMenu->setMenuData(currentMenu);
            gpMenu->setMenuTitle("Back stick");
            gpMenu->setIndex(0);
        }

        if (prevGPIO25Action != valueToSave) {
            backStickChangesPending = true;
            changeRequiresSave = true;
            changeRequiresReboot = true; // GPIO mapping changes require reboot
        }
    }
}

int32_t MainMenuScreen::currentGPIO25Mapping() {
    // Return the current action value (not index) so GPMenu can show "*" on the selected item
    return (int32_t)updateGPIO25Action;
}

void MainMenuScreen::selectBackStickType() {
    if (currentMenu->at(gpMenu->getIndex()).optionValue != -1) {
        bool isGPIO22 = (currentMenu->at(gpMenu->getIndex()).optionValue == 0); // 0 = Left stick
        enterBackStickMapping(isGPIO22);
    }
}

int32_t MainMenuScreen::currentBackStickType() {
    return -1; // No current selection needed for this menu
}

void MainMenuScreen::enterBackStickMapping(bool isGPIO22) {
    // Set previous menu to backStickSelectionMenu
    previousMenu = currentMenu;
    
    // Navigate to the appropriate mapping menu
    if (isGPIO22) {
        currentMenu = &gpio22MappingMenu;
        gpMenu->setMenuTitle("Left stick");
    } else {
        currentMenu = &gpio25MappingMenu;
        gpMenu->setMenuTitle("Right stick");
    }
    
    gpMenu->setMenuData(currentMenu);
    gpMenu->setIndex(0);
}

void MainMenuScreen::exitBackStickSelection() {
    // Check if there are pending changes
    if (backStickChangesPending) {
        // Show reboot prompt
        screenIsPrompting = true;
        promptChoice = true; // Default to Yes (reboot)
        exitToScreenBeforePrompt = DisplayMode::BUTTONS;
        exitToScreen = -1;
    } else {
        // No changes, just exit
        currentMenu = previousMenu;
        previousMenu = nullptr;
        gpMenu->setMenuData(currentMenu);
        gpMenu->setMenuTitle(MAIN_MENU_NAME);
        gpMenu->setIndex(0);
    }
}
