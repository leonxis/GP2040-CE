#include "DpadSwapScreen.h"

#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "GPStorageSaveEvent.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"
#include "MainMenuScreen.h"

#include <functional>

void DpadSwapScreen::init() {
    getRenderer()->clearScreen();

    isMenuReady = false;
    restartPending = false;
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
        std::bind(&DpadSwapScreen::currentMode, this), nullptr, 0});
    modeSelectionMenu.push_back({"LeftJoystick", nullptr, nullptr,
        std::bind(&DpadSwapScreen::currentMode, this), nullptr, 1});
    modeSelectionMenu.push_back({"RightJoystick", nullptr, nullptr,
        std::bind(&DpadSwapScreen::currentMode, this), nullptr, 2});
}

void DpadSwapScreen::resetInputState() {
    prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;
    prevButtonState = getGamepad()->state.buttons;
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
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    Mask_t values = gamepad->debouncedGpio;
    uint16_t buttonState = getGamepad()->state.buttons;

    auto dispatchAction = [&](GpioAction action) {
        updateMenuNavigation(action);
    };

    // Check if menu mappings are initialized before using them
    if (mapMenuUp && mapMenuDown && mapMenuLeft && mapMenuRight && mapMenuSelect && mapMenuBack) {
        // GPIO input (physical buttons) - always works regardless of dpadMode
        if (prevValues != values) {
            if (values & mapMenuUp->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
            else if (values & mapMenuDown->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
            else if (values & mapMenuLeft->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
            else if (values & mapMenuRight->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
            else if (values & mapMenuSelect->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
            else if (values & mapMenuBack->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
        }

        if (gamepadOptions.miniMenuGamepadInput) {
            // For gamepad input, read dpad buttons directly from GPIO to bypass Dpad Swap conversion
            // This allows navigation to work regardless of dpadMode setting
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
                if (dpadUpPressed && !prevDpadUp) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
                else if (dpadDownPressed && !prevDpadDown) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
                else if (dpadLeftPressed && !prevDpadLeft) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
                else if (dpadRightPressed && !prevDpadRight) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
            }
            
            // Button navigation (SELECT/BACK)
            if (prevButtonState != buttonState) {
                if (buttonState == mapMenuSelect->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
                else if (buttonState == mapMenuBack->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
            }
        }
    }

    prevValues = values;
    prevButtonState = buttonState;

    if (exitToScreen != -1) {
        int8_t result = exitToScreen;
        exitToScreen = -1;
        return result;
    }

    return -1;
}

void DpadSwapScreen::drawScreen() {
    // Ensure menu is visible and properly initialized
    if (gpMenu && currentMenu) {
        gpMenu->setVisibility(true);
        // Ensure menu data is set (in case it was cleared or not set)
        gpMenu->setMenuData(currentMenu);
        gpMenu->setMenuTitle("[Dpad Swap]");
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
            {
                uint16_t selectedIndex = gpMenu->getIndex();
                // All menu items are mode selection options
                setDpadMode(selectedIndex);
            }
            break;
        case GpioAction::MENU_NAVIGATION_BACK:
            if (restartPending) {
                MainMenuScreen::flagHMLConfigRestartPending();
                restartPending = false;
            }
            // Return to HML Config menu instead of main menu
            MainMenuScreen::flagOpenHMLConfigMenu();
            exitToScreen = DisplayMode::MAIN_MENU;
            isMenuReady = false;
            break;
        default:
            break;
    }
}


int32_t DpadSwapScreen::currentMode() {
    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    return static_cast<int32_t>(gamepadOptions.dpadMode);
}

void DpadSwapScreen::setDpadMode(int modeIndex) {
    GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

    // Check if mode is actually changing
    DpadMode newMode;
    switch (modeIndex) {
        case 0: newMode = DpadMode::DPAD_MODE_DIGITAL; break;
        case 1: newMode = DpadMode::DPAD_MODE_LEFT_ANALOG; break;
        case 2: newMode = DpadMode::DPAD_MODE_RIGHT_ANALOG; break;
        default: return;
    }

    if (gamepadOptions.dpadMode != newMode) {
        gamepadOptions.dpadMode = newMode;
        restartPending = true;
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
    }
}


