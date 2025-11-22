#include "MainMenuScreen.h"
#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "hardware/watchdog.h"
#include "system.h"
#include "storagemanager.h"
#include "GPRestartEvent.h"

namespace {
    bool hmlConfigRestartPending = false;
    bool shouldOpenHMLConfigMenu = false;
}

void MainMenuScreen::flagHMLConfigRestartPending() {
    hmlConfigRestartPending = true;
}

bool MainMenuScreen::hasHMLConfigRestartPending() {
    return hmlConfigRestartPending;
}

void MainMenuScreen::clearHMLConfigRestartPending() {
    hmlConfigRestartPending = false;
}

void MainMenuScreen::flagOpenHMLConfigMenu() {
    shouldOpenHMLConfigMenu = true;
}

bool MainMenuScreen::hasOpenHMLConfigMenuFlag() {
    return shouldOpenHMLConfigMenu;
}

void MainMenuScreen::clearOpenHMLConfigMenuFlag() {
    shouldOpenHMLConfigMenu = false;
}

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

    setMenuHome();

    // If we should open HML Config menu (e.g., returning from Back stick or Dead Zone screens)
    if (hasOpenHMLConfigMenuFlag()) {
        clearOpenHMLConfigMenuFlag();
        // Find HML Config menu item index
        for (size_t i = 0; i < mainMenu.size(); i++) {
            if (mainMenu[i].submenu == &hmlConfigMenu) {
                previousMenu = currentMenu;
                currentMenu = &hmlConfigMenu;
                gpMenu->setMenuData(currentMenu);
                gpMenu->setMenuTitle("[HML Config]");
                gpMenu->setIndex(0);
                break;
            }
        }
    }
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
        if (changeRequiresReboot) {
            if (hmlConfigRebootPrompt) {
                // Special prompt for HML Config submenu
                // [HML Config] - centered on line 0 (same position as menu title)
                std::string title = "[HML Config]";
                uint16_t titleX = (21 - title.length()) / 2;  // Same centering calculation as menu title
                getRenderer()->drawText(titleX, 0, title.c_str());
                // Empty line (line 1)
                // Reboot to apply ? - left aligned on line 2
                getRenderer()->drawText(0, 2, "Reboot to apply ?");
                // B1: Reboot - left aligned on second to last line (line 6)
                getRenderer()->drawText(0, 6, "B1: Reboot");
                // B2: Cancel - left aligned on last line (line 7)
                getRenderer()->drawText(0, 7, "B2: Cancel");
            } else {
                // Standard prompt with Yes/No choice
            getRenderer()->drawText(1, 1, "Reboot to");
            getRenderer()->drawText(1, 2, "apply settings");

            if (promptChoice) getRenderer()->drawText(5, 4, CHAR_RIGHT);
            getRenderer()->drawText(6, 4, "Yes");
            if (!promptChoice) getRenderer()->drawText(11, 4, CHAR_RIGHT);
            getRenderer()->drawText(12, 4, "No");
            }
        } else {
            getRenderer()->drawText(1, 1, "Config has changed.");
            if (changeRequiresSave && !changeRequiresReboot) {
                getRenderer()->drawText(3, 3, "Would you like");
                getRenderer()->drawText(6, 4, "to save?");
            } else if (changeRequiresSave && changeRequiresReboot) {
                getRenderer()->drawText(3, 3, "Would you like");
                getRenderer()->drawText(1, 4, "to save & restart?");
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
    // Initialize prevButtonState with current button state to prevent immediate button press detection
    // when returning from other screens with a button still pressed
    prevButtonState = getGamepad()->state.buttons;
    prevDpadState = getGamepad()->state.dpad;
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
    if (gpMenu == nullptr || !isMenuReady)
        return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = gpMenu->getDataSize();

    if (screenIsPrompting) {
        switch (action) {
            case GpioAction::MENU_NAVIGATION_UP:
            case GpioAction::MENU_NAVIGATION_DOWN:
            case GpioAction::MENU_NAVIGATION_LEFT:
            case GpioAction::MENU_NAVIGATION_RIGHT:
                if (!hmlConfigRebootPrompt) {
                    // Only toggle choice for standard prompt
                promptChoice = !promptChoice;
                }
                break;
            case GpioAction::MENU_NAVIGATION_SELECT:
                if (hmlConfigRebootPrompt) {
                    // B1 pressed: reboot
                    saveOptions();
                    EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
                    exitToScreen = DisplayMode::BUTTONS;
                    exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                } else {
                    // Standard prompt handling
                if (promptChoice) {
                    saveOptions();
                    EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
                    exitToScreen = DisplayMode::BUTTONS;
                    exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                } else {
                    screenIsPrompting = false;
                    }
                }
                break;
            case GpioAction::MENU_NAVIGATION_BACK:
                if (hmlConfigRebootPrompt) {
                    // B2 pressed: cancel reboot prompt, stay in HML Config menu
                    // Don't clear restart pending flag - user must reboot eventually
                    screenIsPrompting = false;
                    hmlConfigRebootPrompt = false;
                    changeRequiresReboot = false;
                    // Ensure we stay in HML Config menu
                    if (currentMenu != &hmlConfigMenu) {
                        // Restore HML Config menu if we're not already there
                        previousMenu = currentMenu;
                        currentMenu = &hmlConfigMenu;
                        gpMenu->setMenuData(currentMenu);
                        gpMenu->setMenuTitle("[HML Config]");
                        gpMenu->setIndex(0);
                    }
                } else {
                screenIsPrompting = false;
                }
                break;
            default:
                break;
        }
        return;
    }

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
        case GpioAction::MENU_NAVIGATION_LEFT:
            if ((menuIndex - menuLineSize) > 0) gpMenu->setIndex(menuIndex - menuLineSize);
            else gpMenu->setIndex(0);
            break;
        case GpioAction::MENU_NAVIGATION_RIGHT:
            if ((menuIndex + menuLineSize) < (menuSize - 1)) gpMenu->setIndex(menuIndex + menuLineSize);
            else gpMenu->setIndex(menuSize > 0 ? menuSize - 1 : 0);
            break;
        case GpioAction::MENU_NAVIGATION_SELECT:
            if (menuSize == 0) break;
            if (currentMenu->at(menuIndex).submenu != nullptr) {
                previousMenu = currentMenu;
                currentMenu = currentMenu->at(menuIndex).submenu;
                gpMenu->setMenuData(currentMenu);
                // Set menu title with brackets for HML Config submenu
                std::string menuTitle = previousMenu->at(menuIndex).label;
                if (currentMenu == &hmlConfigMenu) {
                    menuTitle = "[HML Config]";
                }
                gpMenu->setMenuTitle(menuTitle);
                gpMenu->setIndex(0);
            } else {
                currentMenu->at(menuIndex).action();
            }
            break;
        case GpioAction::MENU_NAVIGATION_BACK:
            if (previousMenu != nullptr) {
                if (currentMenu == &hmlConfigMenu) {
                    // Don't change menu state yet if restart is pending
                    if (hasHMLConfigRestartPending()) {
                        changeRequiresReboot = true;
                        screenIsPrompting = true;
                        hmlConfigRebootPrompt = true;  // Special prompt for HML Config
                        promptChoice = true;
                        exitToScreen = -1;
                        exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                    } else {
                        // Only change menu if no restart pending
                        currentMenu = previousMenu;
                        previousMenu = nullptr;
                        gpMenu->setMenuData(currentMenu);
                        gpMenu->setMenuSize(18, menuLineSize);
                        gpMenu->setMenuTitle(MAIN_MENU_NAME);
                        gpMenu->setIndex(0);
                    }
                } else {
                    currentMenu = previousMenu;
                    previousMenu = nullptr;
                    gpMenu->setMenuData(currentMenu);
                    gpMenu->setMenuSize(18, menuLineSize);
                    if (currentMenu == &mainMenu) {
                        gpMenu->setMenuTitle(MAIN_MENU_NAME);
                    }
                    gpMenu->setIndex(0);
                }
            } else {
                if (hasHMLConfigRestartPending()) {
                    changeRequiresReboot = true;
                    screenIsPrompting = true;
                    hmlConfigRebootPrompt = true;  // Special prompt for HML Config
                    promptChoice = true;
                    exitToScreen = -1;
                    exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                } else {
                    exitToScreen = DisplayMode::BUTTONS;
                    exitToScreenBeforePrompt = DisplayMode::BUTTONS;
                }
            }
            break;
        default:
            break;
    }
}

void MainMenuScreen::chooseAndReturn() {
    if (previousMenu != nullptr) {
        currentMenu = previousMenu;
        previousMenu = nullptr;
        gpMenu->setMenuData(currentMenu);
        // Restore menu size to default (single column)
        gpMenu->setMenuSize(18, menuLineSize);
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
    }

    changeRequiresSave = false;
    changeRequiresReboot = false;
    clearHMLConfigRestartPending();
    screenIsPrompting = false;
    hmlConfigRebootPrompt = false;
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

        if (saveHasChanged) {
            EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, changeRequiresReboot));
        }
        changeRequiresSave = false;
        changeRequiresReboot = false;
    }

    screenIsPrompting = false;

    // Clear restart pending flag when saving (user confirmed reboot)
    if (hasHMLConfigRestartPending()) {
        clearHMLConfigRestartPending();
    }

    hmlConfigRebootPrompt = false;

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

void MainMenuScreen::startStickCalibration() {
    // Only allow calibration if analog input is enabled
    if (!Storage::getInstance().getAddonOptions().analogOptions.enabled) {
        return;
    }
    exitToScreen = DisplayMode::STICK_CALIBRATION;
    exitToScreenBeforePrompt = DisplayMode::STICK_CALIBRATION;
}

void MainMenuScreen::openBackStickMapping() {
    exitToScreen = DisplayMode::BACK_STICK_MAPPING;
    exitToScreenBeforePrompt = DisplayMode::BACK_STICK_MAPPING;
}

void MainMenuScreen::openDeadzoneMenu() {
    if (!Storage::getInstance().getAddonOptions().analogOptions.enabled) {
        return;
    }
    exitToScreen = DisplayMode::ANALOG_DEADZONE;
    exitToScreenBeforePrompt = DisplayMode::ANALOG_DEADZONE;
}

void MainMenuScreen::openAPMTest() {
    exitToScreen = DisplayMode::APM_TEST;
    exitToScreenBeforePrompt = DisplayMode::APM_TEST;
}
