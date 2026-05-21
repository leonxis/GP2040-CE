#include "BackStickMappingScreen.h"
#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"
#include "gamepad.h"
#include "MainMenuScreen.h"
#include "config.pb.h"
#include <cstring>

void BackStickMappingScreen::applySimpleAction(GpioMappingInfo* m, GpioAction a) {
    m->action = a;
    m->has_action = true;
    m->customButtonMask = 0;
    m->has_customButtonMask = false;
    m->customDpadMask = 0;
    m->has_customDpadMask = false;
    m->has_direction = false;
}

GpioMappingInfo* BackStickMappingScreen::pendingPtr(int32_t slot) {
    if (slot < 0 || slot >= SLOT_COUNT) {
        return &pendingBySlot_[0];
    }
    return &pendingBySlot_[slot];
}

GpioAction BackStickMappingScreen::mappingActionForMenu(const GpioMappingInfo& m) const {
    if (!m.has_action) {
        return GpioAction::NONE;
    }
    if (m.action == GpioAction::CUSTOM_BUTTON_COMBO) {
        return GpioAction::NONE;
    }
    return m.action;
}

static void copyMappingOrZero(GpioMappingInfo* dst, bool has, const GpioMappingInfo& src) {
    if (has) {
        *dst = src;
    } else {
        *dst = GpioMappingInfo_init_zero;
    }
}

void BackStickMappingScreen::loadPendingFromStorage() {
    AddonOptions& ao = Storage::getInstance().getAddonOptions();
    const BackButtonAddonOptions& bb = ao.backButtonAddonOptions;
    copyMappingOrZero(&pendingBySlot_[SLOT_GPIO25_EL], bb.has_leftElMapping, bb.leftElMapping);
    copyMappingOrZero(&pendingBySlot_[SLOT_GPIO24_ER], bb.has_rightErMapping, bb.rightErMapping);
    copyMappingOrZero(&pendingBySlot_[SLOT_LEFT_BACK1], bb.has_leftBack1Mapping, bb.leftBack1Mapping);
    copyMappingOrZero(&pendingBySlot_[SLOT_RIGHT_BACK1], bb.has_rightBack1Mapping, bb.rightBack1Mapping);
    copyMappingOrZero(&pendingBySlot_[SLOT_LEFT_BACK2], bb.has_leftBack2Mapping, bb.leftBack2Mapping);
    copyMappingOrZero(&pendingBySlot_[SLOT_RIGHT_BACK2], bb.has_rightBack2Mapping, bb.rightBack2Mapping);

    if (ao.has_twoKeyTouchpadOptions) {
        const TwoKeyTouchpadOptions& tk = ao.twoKeyTouchpadOptions;
        copyMappingOrZero(&pendingBySlot_[SLOT_TWOKEY_LEFT], tk.has_leftKeyMapping, tk.leftKeyMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_TWOKEY_RIGHT], tk.has_rightKeyMapping, tk.rightKeyMapping);
    } else {
        pendingBySlot_[SLOT_TWOKEY_LEFT] = GpioMappingInfo_init_zero;
        pendingBySlot_[SLOT_TWOKEY_RIGHT] = GpioMappingInfo_init_zero;
    }

    if (ao.has_fnKeyMappingOptions) {
        const FnKeyMappingOptions& fn = ao.fnKeyMappingOptions;
        copyMappingOrZero(&pendingBySlot_[SLOT_FN_LEFT], fn.has_leftFnMapping, fn.leftFnMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_FN_RIGHT], fn.has_rightFnMapping, fn.rightFnMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_MT_LEFT], fn.has_leftMtMapping, fn.leftMtMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_MT_RIGHT], fn.has_rightMtMapping, fn.rightMtMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_EXT_L2], fn.has_leftExtTriggerMapping, fn.leftExtTriggerMapping);
        copyMappingOrZero(&pendingBySlot_[SLOT_EXT_R2], fn.has_rightExtTriggerMapping, fn.rightExtTriggerMapping);
    } else {
        for (int i = SLOT_FN_LEFT; i <= SLOT_EXT_R2; i++) {
            pendingBySlot_[i] = GpioMappingInfo_init_zero;
        }
    }
}

void BackStickMappingScreen::rebuildStickSelectionMenu() {
    stickSelectionMenu.clear();
    auto addSlot = [&](const char* label, int32_t slot) {
        stickSelectionMenu.push_back({label, nullptr, nullptr,
            std::bind(&BackStickMappingScreen::currentStickType, this),
            std::bind(&BackStickMappingScreen::selectStickType, this), slot});
    };
    auto addPair = [&](const char* leftLabel, const char* rightLabel, int32_t leftSlot, int32_t rightSlot) {
        addSlot(leftLabel, leftSlot);
        addSlot(rightLabel, rightSlot);
    };

    addPair("LBkey", "RBkey", SLOT_GPIO25_EL, SLOT_GPIO24_ER);
    addPair("L1Bkey", "R1Bkey", SLOT_LEFT_BACK1, SLOT_RIGHT_BACK1);
    addPair("L2Bkey", "R2Bkey", SLOT_LEFT_BACK2, SLOT_RIGHT_BACK2);

    AddonOptions& ao = Storage::getInstance().getAddonOptions();
    if (ao.has_twoKeyTouchpadOptions && ao.twoKeyTouchpadOptions.has_enabled && ao.twoKeyTouchpadOptions.enabled) {
        addPair("LTBkey", "RTBkey", SLOT_TWOKEY_LEFT, SLOT_TWOKEY_RIGHT);
    }
    addPair("LFNkey", "RFNkey", SLOT_FN_LEFT, SLOT_FN_RIGHT);
    addPair("LMTkey", "RMTkey", SLOT_MT_LEFT, SLOT_MT_RIGHT);
    addPair("L2key", "R2key", SLOT_EXT_L2, SLOT_EXT_R2);

    const uint16_t pairRows = (stickSelectionMenu.size() + 1) / 2;
    slotMenuRowsY_ = 4;
    if (pairRows > slotMenuRowsY_) {
        slotMenuRowsY_ = static_cast<uint8_t>(pairRows);
    }
    if (slotMenuRowsY_ > 6) {
        slotMenuRowsY_ = 6;
    }
}

void BackStickMappingScreen::buildValueMappingMenu() {
    valueMappingMenu.clear();
    auto cur = std::bind(&BackStickMappingScreen::currentEditingAction, this);
    auto sel = std::bind(&BackStickMappingScreen::selectEditingMapping, this);
    auto add = [&](const char* label, GpioAction a) {
        valueMappingMenu.push_back({label, nullptr, nullptr, cur, sel, static_cast<int32_t>(a)});
    };

    add("NONE", GpioAction::NONE);
    add(CHAR_UP, GpioAction::BUTTON_PRESS_UP);
    add(CHAR_DOWN, GpioAction::BUTTON_PRESS_DOWN);
    add(CHAR_LEFT, GpioAction::BUTTON_PRESS_LEFT);
    add(CHAR_RIGHT, GpioAction::BUTTON_PRESS_RIGHT);
    add(CHAR_CROSS, GpioAction::BUTTON_PRESS_B1);
    add(CHAR_CIRCLE, GpioAction::BUTTON_PRESS_B2);
    add(CHAR_SQUARE, GpioAction::BUTTON_PRESS_B3);
    add(CHAR_TRIANGLE, GpioAction::BUTTON_PRESS_B4);
    add("L1", GpioAction::BUTTON_PRESS_L1);
    add("R1", GpioAction::BUTTON_PRESS_R1);
    add("L2", GpioAction::BUTTON_PRESS_L2);
    add("R2", GpioAction::BUTTON_PRESS_R2);
    add("L3", GpioAction::BUTTON_PRESS_L3);
    add("R3", GpioAction::BUTTON_PRESS_R3);
    add("S1", GpioAction::BUTTON_PRESS_S1);
    add("S2", GpioAction::BUTTON_PRESS_S2);
    add("FN", GpioAction::BUTTON_PRESS_FN);
    add("TRB", GpioAction::BUTTON_PRESS_TURBO);
    add("M1", GpioAction::BUTTON_PRESS_MACRO_1);
    add("M2", GpioAction::BUTTON_PRESS_MACRO_2);
    add("M3", GpioAction::BUTTON_PRESS_MACRO_3);
    add("M4", GpioAction::BUTTON_PRESS_MACRO_4);
    add("M5", GpioAction::BUTTON_PRESS_MACRO_5);
    add("M6", GpioAction::BUTTON_PRESS_MACRO_6);
    add("Men", GpioAction::MENU_NAVIGATION_BACK);
}

void BackStickMappingScreen::init() {
    getRenderer()->clearScreen();

    exitToScreen = -1;
    changesPending = false;
    currentState = STATE_SELECT_SLOT;

    if (gpMenu == nullptr) {
        gpMenu = new GPMenu();
        gpMenu->setRenderer(getRenderer());
        gpMenu->setPosition(8, 16);
        addElement(gpMenu);
    } else {
        gpMenu->setRenderer(getRenderer());
    }

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

    loadPendingFromStorage();
    rebuildStickSelectionMenu();
    buildValueMappingMenu();

    currentMenu = &stickSelectionMenu;
    previousMenu = nullptr;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(2, slotMenuRowsY_);
    gpMenu->setIndex(0);
    gpMenu->setVisibility(true);

    prevButtonState = getGamepad()->state.buttons;
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
        if (gamepad->mapDpadUp && gamepad->mapDpadDown &&
            gamepad->mapDpadLeft && gamepad->mapDpadRight) {
            bool dpadUpPressed = (values & gamepad->mapDpadUp->pinMask) != 0;
            bool dpadDownPressed = (values & gamepad->mapDpadDown->pinMask) != 0;
            bool dpadLeftPressed = (values & gamepad->mapDpadLeft->pinMask) != 0;
            bool dpadRightPressed = (values & gamepad->mapDpadRight->pinMask) != 0;

            bool prevDpadUp = (prevValues & gamepad->mapDpadUp->pinMask) != 0;
            bool prevDpadDown = (prevValues & gamepad->mapDpadDown->pinMask) != 0;
            bool prevDpadLeft = (prevValues & gamepad->mapDpadLeft->pinMask) != 0;
            bool prevDpadRight = (prevValues & gamepad->mapDpadRight->pinMask) != 0;

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

void BackStickMappingScreen::selectStickType() {
    if (currentMenu == nullptr || gpMenu == nullptr) return;
    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= stickSelectionMenu.size()) return;
    int32_t slot = stickSelectionMenu[menuIndex].optionValue;
    enterMappingForSlot(slot);
}

int32_t BackStickMappingScreen::currentStickType() {
    return -1;
}

void BackStickMappingScreen::enterMappingForSlot(int32_t slot) {
    if (gpMenu == nullptr) return;
    activeSlot_ = slot;
    currentState = STATE_MAPPING_VALUE;

    const char* title = "[Map]";
    switch (slot) {
        case SLOT_GPIO25_EL: title = "LBkey"; break;
        case SLOT_GPIO24_ER: title = "RBkey"; break;
        case SLOT_LEFT_BACK1: title = "L1Bkey"; break;
        case SLOT_RIGHT_BACK1: title = "R1Bkey"; break;
        case SLOT_LEFT_BACK2: title = "L2Bkey"; break;
        case SLOT_RIGHT_BACK2: title = "R2Bkey"; break;
        case SLOT_TWOKEY_LEFT: title = "LTBkey"; break;
        case SLOT_TWOKEY_RIGHT: title = "RTBkey"; break;
        case SLOT_FN_LEFT: title = "LFNkey"; break;
        case SLOT_FN_RIGHT: title = "RFNkey"; break;
        case SLOT_MT_LEFT: title = "LMTkey"; break;
        case SLOT_MT_RIGHT: title = "RMTkey"; break;
        case SLOT_EXT_L2: title = "L2key"; break;
        case SLOT_EXT_R2: title = "R2key"; break;
        default: break;
    }

    gpMenu->setMenuTitle(title);
    currentMenu = &valueMappingMenu;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuSize(4, 4);
    gpMenu->setIndex(0);
}

void BackStickMappingScreen::backToSlotMenu() {
    currentState = STATE_SELECT_SLOT;
    currentMenu = &stickSelectionMenu;
    gpMenu->setMenuData(currentMenu);
    gpMenu->setMenuTitle("[Back stick]");
    gpMenu->setMenuSize(2, slotMenuRowsY_);
    gpMenu->setIndex(0);
}

int32_t BackStickMappingScreen::currentEditingAction() {
    return static_cast<int32_t>(mappingActionForMenu(*pendingPtr(activeSlot_)));
}

void BackStickMappingScreen::selectEditingMapping() {
    if (currentMenu == nullptr || gpMenu == nullptr || currentMenu != &valueMappingMenu) return;

    uint16_t menuIndex = gpMenu->getIndex();
    if (menuIndex >= valueMappingMenu.size()) return;

    int32_t optionValue = valueMappingMenu[menuIndex].optionValue;
    if (optionValue < 0) return;

    GpioAction valueToSave = static_cast<GpioAction>(optionValue);
    GpioMappingInfo* target = pendingPtr(activeSlot_);
    GpioMappingInfo before = *target;
    applySimpleAction(target, valueToSave);
    if (memcmp(&before, target, sizeof(GpioMappingInfo)) != 0) {
        changesPending = true;
    }

    backToSlotMenu();
    if (changesPending) {
        saveOptions();
    }
}

void BackStickMappingScreen::saveOptions() {
    if (!changesPending) {
        return;
    }

    GpioMappings& gm = Storage::getInstance().getGpioMappings();
    applySimpleAction(&gm.pins[25], GpioAction::ASSIGNED_TO_ADDON);
    applySimpleAction(&gm.pins[24], GpioAction::ASSIGNED_TO_ADDON);

    AddonOptions& ao = Storage::getInstance().getAddonOptions();
    ao.has_backButtonAddonOptions = true;
    BackButtonAddonOptions& bb = ao.backButtonAddonOptions;
    bb.leftElMapping = pendingBySlot_[SLOT_GPIO25_EL];
    bb.has_leftElMapping = true;
    bb.rightErMapping = pendingBySlot_[SLOT_GPIO24_ER];
    bb.has_rightErMapping = true;
    bb.leftBack1Mapping = pendingBySlot_[SLOT_LEFT_BACK1];
    bb.has_leftBack1Mapping = true;
    bb.rightBack1Mapping = pendingBySlot_[SLOT_RIGHT_BACK1];
    bb.has_rightBack1Mapping = true;
    bb.leftBack2Mapping = pendingBySlot_[SLOT_LEFT_BACK2];
    bb.has_leftBack2Mapping = true;
    bb.rightBack2Mapping = pendingBySlot_[SLOT_RIGHT_BACK2];
    bb.has_rightBack2Mapping = true;

    ao.has_twoKeyTouchpadOptions = true;
    TwoKeyTouchpadOptions& tk = ao.twoKeyTouchpadOptions;
    tk.leftKeyMapping = pendingBySlot_[SLOT_TWOKEY_LEFT];
    tk.has_leftKeyMapping = true;
    tk.rightKeyMapping = pendingBySlot_[SLOT_TWOKEY_RIGHT];
    tk.has_rightKeyMapping = true;

    ao.has_fnKeyMappingOptions = true;
    FnKeyMappingOptions& fn = ao.fnKeyMappingOptions;
    fn.leftFnMapping = pendingBySlot_[SLOT_FN_LEFT];
    fn.has_leftFnMapping = true;
    fn.rightFnMapping = pendingBySlot_[SLOT_FN_RIGHT];
    fn.has_rightFnMapping = true;
    fn.leftMtMapping = pendingBySlot_[SLOT_MT_LEFT];
    fn.has_leftMtMapping = true;
    fn.rightMtMapping = pendingBySlot_[SLOT_MT_RIGHT];
    fn.has_rightMtMapping = true;
    fn.leftExtTriggerMapping = pendingBySlot_[SLOT_EXT_L2];
    fn.has_leftExtTriggerMapping = true;
    fn.rightExtTriggerMapping = pendingBySlot_[SLOT_EXT_R2];
    fn.has_rightExtTriggerMapping = true;

    Storage::getInstance().setFunctionalPinMappings();
    EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, false));
    MainMenuScreen::flagHMLConfigRestartPending();

    changesPending = false;
}

void BackStickMappingScreen::updateMenuNavigation(GpioAction action) {
    if (!isMenuReady || gpMenu == nullptr) return;

    uint16_t menuIndex = gpMenu->getIndex();
    uint16_t menuSize = (currentMenu != nullptr) ? currentMenu->size() : 0;
    const bool isValueMappingMenu = (currentMenu == &valueMappingMenu);
    const bool isTwoColumnSlotMenu = (currentMenu == &stickSelectionMenu);

    switch (action) {
        case GpioAction::MENU_NAVIGATION_UP:
            if (isValueMappingMenu) {
                if (menuIndex == 0) {
                    gpMenu->setIndex(menuSize > 0 ? menuSize - 1 : 0);
                } else if (menuIndex >= 4) {
                    gpMenu->setIndex(menuIndex - 4);
                } else {
                    gpMenu->setIndex(0);
                }
            } else if (isTwoColumnSlotMenu && menuSize >= 2) {
                const uint16_t col = menuIndex % 2;
                const uint16_t row = menuIndex / 2;
                const uint16_t numRows = (menuSize + 1) / 2;
                const uint16_t newRow = (row + numRows - 1) % numRows;
                gpMenu->setIndex(static_cast<uint16_t>(newRow * 2 + col));
            } else {
                if (menuSize == 0) break;
                if (menuIndex == 0) gpMenu->setIndex(menuSize - 1);
                else gpMenu->setIndex(menuIndex - 1);
            }
            break;
        case GpioAction::MENU_NAVIGATION_DOWN:
            if (isValueMappingMenu) {
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
            } else if (isTwoColumnSlotMenu && menuSize >= 2) {
                const uint16_t col = menuIndex % 2;
                const uint16_t row = menuIndex / 2;
                const uint16_t numRows = (menuSize + 1) / 2;
                const uint16_t newRow = (row + 1) % numRows;
                gpMenu->setIndex(static_cast<uint16_t>(newRow * 2 + col));
            } else {
                if (menuSize == 0) break;
                if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
                else gpMenu->setIndex(0);
            }
            break;
        case GpioAction::MENU_NAVIGATION_LEFT:
            if (isValueMappingMenu) {
                if (menuIndex == 0) {
                    gpMenu->setIndex(menuSize > 0 ? menuSize - 1 : 0);
                } else if (menuIndex > 0) {
                    gpMenu->setIndex(menuIndex - 1);
                }
            } else if (isTwoColumnSlotMenu && menuSize >= 2) {
                const uint16_t col = menuIndex % 2;
                if (col == 1) {
                    gpMenu->setIndex(menuIndex - 1);
                }
            }
            break;
        case GpioAction::MENU_NAVIGATION_RIGHT:
            if (isValueMappingMenu) {
                if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
                else gpMenu->setIndex(0);
            } else if (isTwoColumnSlotMenu && menuSize >= 2) {
                const uint16_t col = menuIndex % 2;
                if (col == 0 && menuIndex + 1 < menuSize) {
                    gpMenu->setIndex(menuIndex + 1);
                }
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
            if (isValueMappingMenu) {
                backToSlotMenu();
            } else {
                MainMenuScreen::flagOpenHMLConfigMenu();
                exitToScreen = DisplayMode::MAIN_MENU;
                isMenuReady = false;
            }
            break;
        default:
            break;
    }
}
